//! Lazy, OID-addressed PostgreSQL catalog navigation.
use choscordb_driver_api::{
    Column, DriverError, ErrorKind, ObjectId, ObjectKind, Result, SchemaObject,
};
use tokio_postgres::{GenericClient, Row};

const MAX_OBJECTS: usize = 10_000;
const MAX_TEXT: usize = 1024 * 1024;
fn invalid() -> DriverError {
    DriverError::new(
        ErrorKind::InvalidInput,
        "Invalid PostgreSQL object identifier",
    )
}
fn limit() -> DriverError {
    DriverError::new(
        ErrorKind::ResourceLimit,
        "PostgreSQL metadata exceeds the display limit",
    )
}
fn parse(id: &ObjectId) -> Result<(&str, u32)> {
    let mut parts = id.0.split(':');
    if parts.next() != Some("pg") {
        return Err(invalid());
    }
    let kind = parts.next().ok_or_else(invalid)?;
    if !matches!(
        kind,
        "database" | "schema" | "relation" | "constraint" | "index"
    ) {
        return Err(invalid());
    }
    let raw = parts.next().ok_or_else(invalid)?;
    let oid = raw.parse::<u32>().map_err(|_| invalid())?;
    if oid == 0 || raw != oid.to_string() || parts.next().is_some() {
        return Err(invalid());
    }
    Ok((kind, oid))
}
fn quote(name: &str) -> String {
    format!("\"{}\"", name.replace('"', "\"\""))
}
fn text(row: &Row, field: &str) -> Result<String> {
    let value: &str = row.try_get(field).map_err(crate::normalize)?;
    if value.len() > MAX_TEXT {
        return Err(limit());
    }
    Ok(value.to_owned())
}
fn check(rows: &[Row]) -> Result<()> {
    if rows.len() > MAX_OBJECTS {
        Err(limit())
    } else {
        Ok(())
    }
}
fn object(
    id: String,
    parent: Option<ObjectId>,
    name: String,
    qualified_name: String,
    kind: ObjectKind,
    has_children: bool,
) -> SchemaObject {
    SchemaObject {
        id: ObjectId(id),
        parent,
        name,
        qualified_name,
        kind,
        has_children,
        column: None,
    }
}

pub(crate) async fn load_metadata<C: GenericClient + Sync>(
    client: &C,
    parent: Option<ObjectId>,
) -> Result<Vec<SchemaObject>> {
    let Some(ref parent_id) = parent else {
        let row = client.query_one("SELECT oid, datname::text AS name FROM pg_catalog.pg_database WHERE datname = current_database()", &[]).await.map_err(crate::normalize)?;
        let oid: u32 = row.get("oid");
        let name = text(&row, "name")?;
        return Ok(vec![object(
            format!("pg:database:{oid}"),
            None,
            name.clone(),
            quote(&name),
            ObjectKind::Database,
            true,
        )]);
    };
    let (kind, oid) = parse(parent_id)?;
    let sql = match kind {
        "database" => {
            "SELECT n.oid, n.nspname::text AS name FROM pg_catalog.pg_namespace n WHERE EXISTS (SELECT 1 FROM pg_catalog.pg_database d WHERE d.oid=$1 AND d.datname=current_database()) ORDER BY n.nspname LIMIT 10001"
        }
        "schema" => {
            "SELECT c.oid, c.relname::text AS name, n.nspname::text AS schema, c.relkind::text AS kind FROM pg_catalog.pg_class c JOIN pg_catalog.pg_namespace n ON n.oid=c.relnamespace WHERE n.oid=$1 AND c.relkind IN ('r','p','v','m','f') ORDER BY c.relname LIMIT 10001"
        }
        "relation" => return relation_children(client, parent_id, oid).await,
        _ => return Ok(Vec::new()),
    };
    let rows = client.query(sql, &[&oid]).await.map_err(crate::normalize)?;
    check(&rows)?;
    rows.iter()
        .map(|row| {
            let child: u32 = row.get("oid");
            let name = text(row, "name")?;
            if kind == "database" {
                Ok(object(
                    format!("pg:schema:{child}"),
                    parent.clone(),
                    name.clone(),
                    quote(&name),
                    ObjectKind::Schema,
                    true,
                ))
            } else {
                let schema = text(row, "schema")?;
                let k: &str = row.get("kind");
                Ok(object(
                    format!("pg:relation:{child}"),
                    parent.clone(),
                    name.clone(),
                    format!("{}.{}", quote(&schema), quote(&name)),
                    if matches!(k, "v" | "m") {
                        ObjectKind::View
                    } else {
                        ObjectKind::Table
                    },
                    true,
                ))
            }
        })
        .collect()
}

async fn relation_children<C: GenericClient + Sync>(
    client: &C,
    parent: &ObjectId,
    oid: u32,
) -> Result<Vec<SchemaObject>> {
    let rows = client.query("SELECT a.attnum, a.attname::text AS name, pg_catalog.format_type(a.atttypid,a.atttypmod) AS datatype, a.atttypid, a.atttypmod, a.attnotnull, n.nspname::text AS schema, c.relname::text AS relation FROM pg_catalog.pg_attribute a JOIN pg_catalog.pg_class c ON c.oid=a.attrelid JOIN pg_catalog.pg_namespace n ON n.oid=c.relnamespace WHERE a.attrelid=$1 AND a.attnum>0 AND NOT a.attisdropped ORDER BY a.attnum LIMIT 10001", &[&oid]).await.map_err(crate::normalize)?;
    check(&rows)?;
    let mut result = Vec::with_capacity(rows.len());
    for row in rows {
        let name = text(&row, "name")?;
        let typ: u32 = row.get("atttypid");
        let modifier: i32 = row.get("atttypmod");
        let (precision, scale) = numeric_modifiers(typ, modifier);
        let mut child = object(
            format!("pg:column:{oid}:{}", row.get::<_, i16>("attnum")),
            Some(parent.clone()),
            name.clone(),
            format!(
                "{}.{}.{}",
                quote(&text(&row, "schema")?),
                quote(&text(&row, "relation")?),
                quote(&name)
            ),
            ObjectKind::Column,
            false,
        );
        child.column = Some(Column {
            name,
            database_type: text(&row, "datatype")?,
            precision,
            scale,
            timezone: matches!(typ, 1184 | 1266).then(|| "with time zone".into()),
            nullable: Some(!row.get::<_, bool>("attnotnull")),
        });
        result.push(child);
    }
    let rows=client.query("SELECT oid, conname::text AS name, contype::text AS kind FROM pg_catalog.pg_constraint WHERE conrelid=$1 AND contype IN ('p','f','u') ORDER BY conname LIMIT 10001", &[&oid]).await.map_err(crate::normalize)?;
    check(&rows)?;
    for row in rows {
        let id: u32 = row.get("oid");
        let name = text(&row, "name")?;
        let kind = match row.get::<_, &str>("kind") {
            "p" => ObjectKind::PrimaryKey,
            "f" => ObjectKind::ForeignKey,
            _ => ObjectKind::UniqueKey,
        };
        result.push(object(
            format!("pg:constraint:{id}"),
            Some(parent.clone()),
            name.clone(),
            quote(&name),
            kind,
            false,
        ));
    }
    let rows=client.query("SELECT c.oid,c.relname::text AS name,n.nspname::text AS schema FROM pg_catalog.pg_index i JOIN pg_catalog.pg_class c ON c.oid=i.indexrelid JOIN pg_catalog.pg_namespace n ON n.oid=c.relnamespace WHERE i.indrelid=$1 ORDER BY c.relname LIMIT 10001", &[&oid]).await.map_err(crate::normalize)?;
    check(&rows)?;
    for row in rows {
        let id: u32 = row.get("oid");
        let name = text(&row, "name")?;
        result.push(object(
            format!("pg:index:{id}"),
            Some(parent.clone()),
            name.clone(),
            format!("{}.{}", quote(&text(&row, "schema")?), quote(&name)),
            ObjectKind::Index,
            false,
        ));
    }
    if result.len() > MAX_OBJECTS {
        return Err(limit());
    }
    Ok(result)
}
fn numeric_modifiers(typ: u32, modifier: i32) -> (Option<u32>, Option<i32>) {
    if typ == 1700 && modifier >= 4 {
        let m = modifier - 4;
        (
            Some(((m >> 16) & 65535) as u32),
            Some(((m & 2047) ^ 1024) - 1024),
        )
    } else {
        (None, None)
    }
}

pub(crate) async fn object_ddl<C: GenericClient + Sync>(
    client: &C,
    object: &ObjectId,
) -> Result<String> {
    let (kind, oid) = parse(object)?;
    let sql = match kind {
        "index" => {
            "SELECT pg_catalog.pg_get_indexdef(c.oid) AS ddl FROM pg_catalog.pg_class c WHERE c.oid=$1 AND c.relkind IN ('i','I')"
        }
        "constraint" => {
            "SELECT 'ALTER TABLE ' || pg_catalog.quote_ident(n.nspname) || '.' || pg_catalog.quote_ident(t.relname) || ' ADD CONSTRAINT ' || pg_catalog.quote_ident(c.conname) || ' ' || pg_catalog.pg_get_constraintdef(c.oid) AS ddl FROM pg_catalog.pg_constraint c JOIN pg_catalog.pg_class t ON t.oid=c.conrelid JOIN pg_catalog.pg_namespace n ON n.oid=t.relnamespace WHERE c.oid=$1"
        }
        "schema" => {
            "SELECT 'CREATE SCHEMA ' || pg_catalog.quote_ident(nspname) AS ddl FROM pg_catalog.pg_namespace WHERE oid=$1"
        }
        "relation" => return relation_ddl(client, oid).await,
        _ => {
            return Err(DriverError::new(
                ErrorKind::Unsupported,
                "DDL is unavailable for this PostgreSQL object",
            ));
        }
    };
    let row = client
        .query_opt(sql, &[&oid])
        .await
        .map_err(crate::normalize)?
        .ok_or_else(|| {
            DriverError::new(ErrorKind::StaleHandle, "PostgreSQL object no longer exists")
        })?;
    let mut ddl = text(&row, "ddl")?;
    ddl.push(';');
    Ok(ddl)
}

async fn relation_ddl<C: GenericClient + Sync>(client: &C, oid: u32) -> Result<String> {
    let row=client.query_opt("SELECT c.relkind::text AS kind,n.nspname::text AS schema,c.relname::text AS name,c.relpersistence::text AS persistence,c.relispartition,c.relrowsecurity,c.reloptions IS NOT NULL AS options,EXISTS(SELECT 1 FROM pg_catalog.pg_inherits WHERE inhrelid=c.oid) AS inherited FROM pg_catalog.pg_class c JOIN pg_catalog.pg_namespace n ON n.oid=c.relnamespace WHERE c.oid=$1",&[&oid]).await.map_err(crate::normalize)?.ok_or_else(||DriverError::new(ErrorKind::StaleHandle,"PostgreSQL relation no longer exists"))?;
    let kind: &str = row.get("kind");
    let name = format!(
        "{}.{}",
        quote(&text(&row, "schema")?),
        quote(&text(&row, "name")?)
    );
    if kind == "v" || kind == "m" {
        if row.get::<_, bool>("options") {
            return Err(DriverError::new(
                ErrorKind::Unsupported,
                "DDL for view storage or security options is unavailable",
            ));
        }
        let def = client
            .query_one(
                "SELECT pg_catalog.pg_get_viewdef($1::oid, false) AS ddl",
                &[&oid],
            )
            .await
            .map_err(crate::normalize)?;
        return Ok(format!(
            "CREATE {}VIEW {name} AS\n{}",
            if kind == "m" { "MATERIALIZED " } else { "" },
            text(&def, "ddl")?
        ));
    }
    if kind != "r"
        || row.get::<_, bool>("relispartition")
        || row.get::<_, bool>("inherited")
        || row.get::<_, bool>("relrowsecurity")
        || row.get::<_, bool>("options")
    {
        return Err(DriverError::new(
            ErrorKind::Unsupported,
            "DDL reconstruction for partitioned, inherited, foreign, or policy/storage-customized tables is unavailable",
        ));
    }
    let rows=client.query("SELECT a.attname::text AS name,pg_catalog.format_type(a.atttypid,a.atttypmod) AS datatype,a.attnotnull,a.attidentity::text AS identity,a.attgenerated::text AS generated,COALESCE(pg_catalog.pg_get_expr(d.adbin,d.adrelid),'') AS expression,a.attcollation<>t.typcollation AS custom_collation FROM pg_catalog.pg_attribute a JOIN pg_catalog.pg_type t ON t.oid=a.atttypid LEFT JOIN pg_catalog.pg_attrdef d ON d.adrelid=a.attrelid AND d.adnum=a.attnum WHERE a.attrelid=$1 AND a.attnum>0 AND NOT a.attisdropped ORDER BY a.attnum LIMIT 10001",&[&oid]).await.map_err(crate::normalize)?;
    check(&rows)?;
    let mut definitions = Vec::new();
    let mut total = 0usize;
    for row in rows {
        if row.get::<_, bool>("custom_collation") {
            return Err(DriverError::new(
                ErrorKind::Unsupported,
                "DDL for custom column collations is unavailable",
            ));
        }
        let mut def = format!(
            "  {} {}",
            quote(&text(&row, "name")?),
            text(&row, "datatype")?
        );
        let identity: &str = row.get("identity");
        let generated: &str = row.get("generated");
        let expr = text(&row, "expression")?;
        if !identity.is_empty() {
            let sequence=client.query_opt("SELECT s.seqstart,s.seqincrement,s.seqmax,s.seqmin,s.seqcache,s.seqcycle FROM pg_catalog.pg_sequence s JOIN pg_catalog.pg_depend d ON d.objid=s.seqrelid AND d.classid='pg_catalog.pg_class'::regclass WHERE d.refclassid='pg_catalog.pg_class'::regclass AND d.refobjid=$1 AND d.refobjsubid=(SELECT attnum FROM pg_catalog.pg_attribute WHERE attrelid=$1 AND attname=$2) AND d.deptype='i'",&[&oid,&row.get::<_,&str>("name")]).await.map_err(crate::normalize)?.ok_or_else(||DriverError::new(ErrorKind::Unsupported,"Identity sequence metadata is unavailable"))?;
            def.push_str(&format!(" GENERATED {} AS IDENTITY (START WITH {} INCREMENT BY {} MINVALUE {} MAXVALUE {} CACHE {} {})",if identity=="a" {"ALWAYS"} else {"BY DEFAULT"},sequence.get::<_,i64>("seqstart"),sequence.get::<_,i64>("seqincrement"),sequence.get::<_,i64>("seqmin"),sequence.get::<_,i64>("seqmax"),sequence.get::<_,i64>("seqcache"),if sequence.get::<_,bool>("seqcycle") {"CYCLE"} else {"NO CYCLE"}));
        }
        if generated == "s" {
            def.push_str(&format!(" GENERATED ALWAYS AS ({expr}) STORED"));
        } else if !generated.is_empty() {
            return Err(DriverError::new(
                ErrorKind::Unsupported,
                "Unknown generated column kind",
            ));
        } else if !expr.is_empty() {
            def.push_str(&format!(" DEFAULT {expr}"));
        }
        if row.get::<_, bool>("attnotnull") {
            def.push_str(" NOT NULL");
        }
        total = total.saturating_add(def.len());
        if total > MAX_TEXT {
            return Err(limit());
        }
        definitions.push(def);
    }
    let rows=client.query("SELECT conname::text AS name,pg_catalog.pg_get_constraintdef(oid,false) AS ddl FROM pg_catalog.pg_constraint WHERE conrelid=$1 ORDER BY conname LIMIT 10001",&[&oid]).await.map_err(crate::normalize)?;
    check(&rows)?;
    for row in rows {
        let def = format!(
            "  CONSTRAINT {} {}",
            quote(&text(&row, "name")?),
            text(&row, "ddl")?
        );
        total = total.saturating_add(def.len());
        if total > MAX_TEXT {
            return Err(limit());
        }
        definitions.push(def);
    }
    let persistence = match row.get::<_, &str>("persistence") {
        "u" => "UNLOGGED ",
        "t" => "TEMPORARY ",
        _ => "",
    };
    let ddl = format!(
        "CREATE {persistence}TABLE {name} (\n{}\n);",
        definitions.join(",\n")
    );
    if ddl.len() > MAX_TEXT {
        return Err(limit());
    }
    Ok(ddl)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn ids_and_numeric_modifiers() {
        assert!(parse(&ObjectId("pg:relation:42".into())).is_ok());
        for id in [
            "pg:relation:0",
            "pg:relation:042",
            "pg:relation:42:1",
            "pg:evil:42",
            "pg:relation:1;DROP",
        ] {
            assert!(parse(&ObjectId(id.into())).is_err());
        }
        assert_eq!(
            numeric_modifiers(1700, 4 + (12 << 16) + 2046),
            (Some(12), Some(-2))
        );
        assert_eq!(quote("a\"b"), "\"a\"\"b\"");
    }
}

#[cfg(test)]
mod live_tests {
    use super::*;
    #[test]
    fn quoted_catalog_tree_and_ddl() {
        tokio::runtime::Runtime::new().unwrap().block_on(async {
        let Ok(url) = std::env::var("CHOSCORDB_TEST_POSTGRES") else {
            return;
        };
        let (client, connection) = tokio_postgres::connect(&url, tokio_postgres::NoTls)
            .await
            .unwrap();
        let task = tokio::spawn(connection);
        client.batch_execute("BEGIN; CREATE SCHEMA \"metadata odd\"; CREATE TABLE \"metadata odd\".\"a\"\"b\" (id bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY, amount numeric(12,-2), stamp timestamptz, label text DEFAULT 'x', doubled numeric GENERATED ALWAYS AS (amount * 2) STORED, UNIQUE(label)); CREATE INDEX \"index odd\" ON \"metadata odd\".\"a\"\"b\" (amount); CREATE VIEW \"metadata odd\".\"view odd\" AS SELECT amount FROM \"metadata odd\".\"a\"\"b\";").await.unwrap();
        let database = load_metadata(&client, None).await.unwrap().remove(0);
        let schema = load_metadata(&client, Some(database.id))
            .await
            .unwrap()
            .into_iter()
            .find(|o| o.name == "metadata odd")
            .unwrap();
        let relations = load_metadata(&client, Some(schema.id)).await.unwrap();
        let table = relations.iter().find(|o| o.name == "a\"b").unwrap();
        assert_eq!(table.qualified_name, "\"metadata odd\".\"a\"\"b\"");
        let children = load_metadata(&client, Some(table.id.clone()))
            .await
            .unwrap();
        let amount = children
            .iter()
            .find(|o| o.name == "amount")
            .unwrap()
            .column
            .as_ref()
            .unwrap();
        assert_eq!((amount.precision, amount.scale), (Some(12), Some(-2)));
        assert!(children.iter().any(|o| o.kind == ObjectKind::PrimaryKey));
        assert!(children.iter().any(|o| o.kind == ObjectKind::UniqueKey));
        let index = children.iter().find(|o| o.name == "index odd").unwrap();
        assert!(
            object_ddl(&client, &index.id)
                .await
                .unwrap()
                .contains("CREATE INDEX")
        );
        let ddl = object_ddl(&client, &table.id).await.unwrap();
        assert!(ddl.contains("GENERATED ALWAYS AS IDENTITY"));
        assert!(ddl.contains("numeric(12,-2)"));
        assert!(ddl.contains("STORED"));
        let view = relations
            .iter()
            .find(|o| o.kind == ObjectKind::View)
            .unwrap();
        assert!(
            object_ddl(&client, &view.id)
                .await
                .unwrap()
                .contains("CREATE VIEW")
        );
        client.batch_execute("ROLLBACK").await.unwrap();
        drop(client);
        task.await.unwrap().unwrap();
        });
    }
}
