//! Lazy, OID-addressed PostgreSQL catalog navigation.
use choscordb_driver_api::{
    Column, DriverError, EditColumn, EditQueryTarget, EditTarget, ErrorKind, MetadataProperty,
    ObjectId, ObjectKind, Result, SchemaObject, simple_select,
};
use futures_util::TryStreamExt;
use tokio_postgres::types::{FromSql, ToSql, Type};
use tokio_postgres::{GenericClient, Row};
struct RawMetadata<'a>(&'a [u8]);
impl<'a> FromSql<'a> for RawMetadata<'a> {
    fn from_sql(
        _: &Type,
        raw: &'a [u8],
    ) -> std::result::Result<Self, Box<dyn std::error::Error + Sync + Send>> {
        Ok(Self(raw))
    }
    fn accepts(_: &Type) -> bool {
        true
    }
}
async fn bounded_query<C: GenericClient + Sync>(
    client: &C,
    sql: &str,
    parameters: &[&(dyn ToSql + Sync)],
) -> Result<Vec<Row>> {
    let stream = client
        .query_raw(sql, parameters.iter().copied())
        .await
        .map_err(crate::normalize)?;
    tokio::pin!(stream);
    let mut rows = Vec::new();
    let mut bytes = 0usize;
    while let Some(row) = stream.try_next().await.map_err(crate::normalize)? {
        for i in 0..row.len() {
            bytes = bytes.saturating_add(
                row.try_get::<_, Option<RawMetadata<'_>>>(i)
                    .map_err(crate::normalize)?
                    .map_or(0, |v| v.0.len()),
            );
        }
        if rows.len() == MAX_OBJECTS || bytes > MAX_TEXT {
            return Err(limit());
        }
        rows.push(row);
    }
    Ok(rows)
}

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
        "database" | "schema" | "relation" | "constraint" | "index" | "sequence" | "function"
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
fn group(id: &ObjectId) -> Option<(u32, &str)> {
    let mut parts = id.0.split(':');
    if parts.next()? != "pg" || parts.next()? != "group" {
        return None;
    }
    let oid = parts.next()?.parse::<u32>().ok()?;
    let kind = parts.next()?;
    if oid == 0
        || parts.next().is_some()
        || !matches!(kind, "table" | "view" | "index" | "sequence" | "function")
    {
        return None;
    }
    Some((oid, kind))
}
fn quote(name: &str) -> String {
    format!("\"{}\"", name.replace('"', "\"\""))
}
pub(crate) async fn edit_target<C: GenericClient + Sync>(
    client: &C,
    object: &ObjectId,
) -> Result<EditTarget> {
    let (kind, oid) = parse(object)?;
    if kind != "relation" {
        return Err(invalid());
    }
    let relation = client.query_one("SELECT n.nspname::text, c.relname::text, c.relkind::text FROM pg_catalog.pg_class c JOIN pg_catalog.pg_namespace n ON n.oid=c.relnamespace WHERE c.oid=$1", &[&oid]).await.map_err(crate::normalize)?;
    let schema: String = relation.get(0);
    let table: String = relation.get(1);
    let relation_kind: String = relation.get(2);
    let mut target = EditTarget {
        qualified_name: format!("{}.{}", quote(&schema), quote(&table)),
        parameter_style: "$".into(),
        ..Default::default()
    };
    if !matches!(relation_kind.as_str(), "r" | "p") {
        target.reason = "Only base tables can be edited".into();
        return Ok(target);
    }
    let rows = bounded_query(client, "SELECT attname::text, pg_catalog.format_type(atttypid,atttypmod), attnotnull, attgenerated::text FROM pg_catalog.pg_attribute WHERE attrelid=$1 AND attnum>0 AND NOT attisdropped ORDER BY attnum LIMIT 10001", &[&oid]).await?;
    check(&rows)?;
    for row in rows {
        target.columns.push(EditColumn {
            name: row.get(0),
            database_type: row.get(1),
            nullable: !row.get::<_, bool>(2),
            generated: !row.get::<_, &str>(3).is_empty(),
            key: false,
        });
    }
    let indexes = bounded_query(client, "SELECT i.indexrelid, i.indisprimary FROM pg_catalog.pg_index i WHERE i.indrelid=$1 AND i.indisunique AND i.indisvalid AND i.indisready AND i.indpred IS NULL AND i.indexprs IS NULL ORDER BY i.indisprimary DESC, i.indexrelid LIMIT 10001", &[&oid]).await?;
    check(&indexes)?;
    for index in indexes {
        let index_oid: u32 = index.get(0);
        let primary: bool = index.get(1);
        let rows = bounded_query(client, "SELECT a.attname::text, a.attnotnull FROM pg_catalog.pg_index i JOIN LATERAL unnest(i.indkey) WITH ORDINALITY AS k(attnum,ordinality) ON k.ordinality<=i.indnkeyatts JOIN pg_catalog.pg_attribute a ON a.attrelid=i.indrelid AND a.attnum=k.attnum WHERE i.indexrelid=$1 ORDER BY k.ordinality", &[&index_oid]).await?;
        let mut columns = Vec::new();
        let mut valid = true;
        for row in rows {
            let name: String = row.get(0);
            if !primary && !row.get::<_, bool>(1) {
                valid = false;
                break;
            }
            columns.push(name);
        }
        if valid && !columns.is_empty() {
            target.key_columns = columns;
            break;
        }
    }
    for column in &mut target.columns {
        column.key = target.key_columns.contains(&column.name);
    }
    if target.key_columns.is_empty() {
        target.reason = "No stable primary or nonnullable unique key; inserts only".into();
    }
    Ok(target)
}
pub(crate) async fn edit_query<C: GenericClient + Sync>(
    client: &C,
    sql: &str,
    result_columns: Vec<String>,
) -> Result<EditQueryTarget> {
    let mut output = EditQueryTarget {
        reason: "Query shape cannot be proven editable".into(),
        ..Default::default()
    };
    let Some(shape) = simple_select(sql, &result_columns) else {
        return Ok(output);
    };
    let relation_name = match &shape.schema {
        Some(schema) => format!("{}.{}", quote(schema), quote(&shape.table)),
        None => quote(&shape.table),
    };
    let oid: Option<u32> = client
        .query_one("SELECT pg_catalog.to_regclass($1)::oid", &[&relation_name])
        .await
        .map_err(crate::normalize)?
        .get(0);
    let Some(oid) = oid else {
        return Ok(output);
    };
    let target = edit_target(client, &ObjectId(format!("pg:relation:{oid}"))).await?;
    if target.columns.is_empty() {
        return Ok(output);
    }
    let sources = shape.source_columns;
    if sources.len() != result_columns.len()
        || sources.iter().any(|source| {
            !source.is_empty() && !target.columns.iter().any(|column| column.name == *source)
        })
    {
        return Ok(output);
    }
    if sources
        .iter()
        .filter(|source| !source.is_empty())
        .any(|source| sources.iter().filter(|other| *other == source).count() > 1)
    {
        output.reason = "Duplicate source columns are ambiguous".into();
        return Ok(output);
    }
    if target.key_columns.is_empty() || target.key_columns.iter().any(|key| !sources.contains(key))
    {
        output.reason = "All stable key columns must be present".into();
        return Ok(output);
    }
    output.target = target;
    output.source_columns = sources;
    output.reason.clear();
    Ok(output)
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
        properties: Vec::new(),
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
    if let Some((schema_oid, group_kind)) = group(parent_id) {
        return group_children(client, parent_id, schema_oid, group_kind).await;
    }
    let (kind, oid) = parse(parent_id)?;
    if kind == "schema" {
        let exists = client
            .query_opt(
                "SELECT oid FROM pg_catalog.pg_namespace WHERE oid=$1",
                &[&oid],
            )
            .await
            .map_err(crate::normalize)?;
        if exists.is_none() {
            return Err(DriverError::new(
                ErrorKind::StaleHandle,
                "PostgreSQL schema no longer exists",
            ));
        }
        return Ok([
            ("Tables", "table"),
            ("Views", "view"),
            ("Indexes", "index"),
            ("Sequences", "sequence"),
            ("Functions", "function"),
        ]
        .into_iter()
        .map(|(label, key)| {
            object(
                format!("pg:group:{oid}:{key}"),
                parent.clone(),
                label.into(),
                String::new(),
                ObjectKind::Group,
                true,
            )
        })
        .collect());
    }
    let sql = match kind {
        "database" => {
            "SELECT n.oid, n.nspname::text AS name FROM pg_catalog.pg_namespace n WHERE EXISTS (SELECT 1 FROM pg_catalog.pg_database d WHERE d.oid=$1 AND d.datname=current_database()) ORDER BY n.nspname LIMIT 10001"
        }
        "relation" => return relation_children(client, parent_id, oid).await,
        _ => return Ok(Vec::new()),
    };
    let rows = bounded_query(client, sql, &[&oid]).await?;
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

async fn group_children<C: GenericClient + Sync>(
    client: &C,
    parent: &ObjectId,
    schema_oid: u32,
    group_kind: &str,
) -> Result<Vec<SchemaObject>> {
    let sql = match group_kind {
        "table" => {
            "SELECT c.oid,c.relname::text AS name,n.nspname::text AS schema,c.relkind::text AS kind FROM pg_catalog.pg_class c JOIN pg_catalog.pg_namespace n ON n.oid=c.relnamespace WHERE n.oid=$1 AND c.relkind IN ('r','p','f') ORDER BY c.relname LIMIT 10001"
        }
        "view" => {
            "SELECT c.oid,c.relname::text AS name,n.nspname::text AS schema,c.relkind::text AS kind FROM pg_catalog.pg_class c JOIN pg_catalog.pg_namespace n ON n.oid=c.relnamespace WHERE n.oid=$1 AND c.relkind IN ('v','m') ORDER BY c.relname LIMIT 10001"
        }
        "index" => {
            "SELECT c.oid,c.relname::text AS name,n.nspname::text AS schema,c.relkind::text AS kind FROM pg_catalog.pg_class c JOIN pg_catalog.pg_namespace n ON n.oid=c.relnamespace WHERE n.oid=$1 AND c.relkind IN ('i','I') ORDER BY c.relname LIMIT 10001"
        }
        "sequence" => {
            "SELECT c.oid,c.relname::text AS name,n.nspname::text AS schema,c.relkind::text AS kind FROM pg_catalog.pg_class c JOIN pg_catalog.pg_namespace n ON n.oid=c.relnamespace WHERE n.oid=$1 AND c.relkind='S' ORDER BY c.relname LIMIT 10001"
        }
        "function" => {
            "SELECT p.oid,p.proname::text AS name,n.nspname::text AS schema,pg_catalog.pg_get_function_identity_arguments(p.oid) AS signature FROM pg_catalog.pg_proc p JOIN pg_catalog.pg_namespace n ON n.oid=p.pronamespace WHERE n.oid=$1 AND p.prokind='f' ORDER BY p.proname,p.oid LIMIT 10001"
        }
        _ => return Err(invalid()),
    };
    let rows = bounded_query(client, sql, &[&schema_oid]).await?;
    let mut objects = Vec::with_capacity(rows.len());
    for row in rows {
        let oid: u32 = row.get("oid");
        let name = text(&row, "name")?;
        let schema = text(&row, "schema")?;
        let (id_kind, kind, label, qualified) = if group_kind == "function" {
            let signature = text(&row, "signature")?;
            (
                "function",
                ObjectKind::Function,
                format!("{name}({signature})"),
                format!("{}.{}({signature})", quote(&schema), quote(&name)),
            )
        } else {
            let kind = match group_kind {
                "table" => ObjectKind::Table,
                "view" => ObjectKind::View,
                "index" => ObjectKind::Index,
                _ => ObjectKind::Sequence,
            };
            (
                if group_kind == "index" {
                    "index"
                } else if group_kind == "sequence" {
                    "sequence"
                } else {
                    "relation"
                },
                kind,
                name.clone(),
                format!("{}.{}", quote(&schema), quote(&name)),
            )
        };
        let mut item = object(
            format!("pg:{id_kind}:{oid}"),
            Some(parent.clone()),
            label,
            qualified,
            kind,
            matches!(group_kind, "table" | "view"),
        );
        if group_kind == "index" {
            let definition = client
                .query_one(
                    "SELECT pg_catalog.pg_get_indexdef($1::oid) AS definition",
                    &[&oid],
                )
                .await
                .map_err(crate::normalize)?;
            item.properties.push(MetadataProperty::available(
                "Definition",
                text(&definition, "definition")?,
            ));
        } else if group_kind == "sequence" {
            let sequence = client.query_one("SELECT s.seqstart,s.seqincrement,s.seqmin,s.seqmax,s.seqcache,s.seqcycle FROM pg_catalog.pg_sequence s WHERE s.seqrelid=$1", &[&oid]).await.map_err(crate::normalize)?;
            for (label, value) in [
                ("Start", sequence.get::<_, i64>("seqstart")),
                ("Increment", sequence.get::<_, i64>("seqincrement")),
                ("Minimum", sequence.get::<_, i64>("seqmin")),
                ("Maximum", sequence.get::<_, i64>("seqmax")),
                ("Cache", sequence.get::<_, i64>("seqcache")),
            ] {
                item.properties
                    .push(MetadataProperty::available(label, value));
            }
            item.properties.push(MetadataProperty::available(
                "Cycle",
                sequence.get::<_, bool>("seqcycle"),
            ));
        }
        objects.push(item);
    }
    Ok(objects)
}

async fn relation_children<C: GenericClient + Sync>(
    client: &C,
    parent: &ObjectId,
    oid: u32,
) -> Result<Vec<SchemaObject>> {
    if client
        .query_opt(
            "SELECT oid FROM pg_catalog.pg_class WHERE oid=$1 AND relkind IN ('r','p','v','m','f')",
            &[&oid],
        )
        .await
        .map_err(crate::normalize)?
        .is_none()
    {
        return Err(DriverError::new(
            ErrorKind::StaleHandle,
            "PostgreSQL relation no longer exists",
        ));
    }
    let rows = bounded_query(client, "SELECT a.attnum, a.attname::text AS name, pg_catalog.format_type(a.atttypid,a.atttypmod) AS datatype, a.atttypid, a.atttypmod, a.attnotnull, COALESCE(pg_catalog.pg_get_expr(d.adbin,d.adrelid),'No default') AS default_expression, a.attidentity::text AS identity, a.attgenerated::text AS generated, n.nspname::text AS schema, c.relname::text AS relation FROM pg_catalog.pg_attribute a JOIN pg_catalog.pg_class c ON c.oid=a.attrelid JOIN pg_catalog.pg_namespace n ON n.oid=c.relnamespace LEFT JOIN pg_catalog.pg_attrdef d ON d.adrelid=a.attrelid AND d.adnum=a.attnum WHERE a.attrelid=$1 AND a.attnum>0 AND NOT a.attisdropped ORDER BY a.attnum LIMIT 10001", &[&oid]).await?;
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
        child.properties.push(MetadataProperty::available(
            "Default",
            text(&row, "default_expression")?,
        ));
        child.properties.push(MetadataProperty::available(
            "Identity",
            match row.get::<_, &str>("identity") {
                "a" => "Always",
                "d" => "By default",
                _ => "No",
            },
        ));
        child.properties.push(MetadataProperty::available(
            "Generated",
            match row.get::<_, &str>("generated") {
                "s" => "Stored",
                "v" => "Virtual",
                _ => "No",
            },
        ));
        result.push(child);
    }
    let rows=bounded_query(client, "SELECT oid, conname::text AS name, contype::text AS kind, pg_catalog.pg_get_constraintdef(oid,false) AS definition FROM pg_catalog.pg_constraint WHERE conrelid=$1 AND contype IN ('p','f','u') ORDER BY conname LIMIT 10001", &[&oid]).await?;
    check(&rows)?;
    for row in rows {
        let id: u32 = row.get("oid");
        let name = text(&row, "name")?;
        let kind = match row.get::<_, &str>("kind") {
            "p" => ObjectKind::PrimaryKey,
            "f" => ObjectKind::ForeignKey,
            _ => ObjectKind::UniqueKey,
        };
        let mut child = object(
            format!("pg:constraint:{id}"),
            Some(parent.clone()),
            name.clone(),
            quote(&name),
            kind,
            false,
        );
        child.properties.push(MetadataProperty::available(
            "Definition",
            text(&row, "definition")?,
        ));
        result.push(child);
    }
    let rows=bounded_query(client, "SELECT c.oid,c.relname::text AS name,n.nspname::text AS schema,i.indisunique,i.indisvalid,i.indpred IS NOT NULL AS partial,pg_catalog.pg_get_indexdef(c.oid) AS definition FROM pg_catalog.pg_index i JOIN pg_catalog.pg_class c ON c.oid=i.indexrelid JOIN pg_catalog.pg_namespace n ON n.oid=c.relnamespace WHERE i.indrelid=$1 ORDER BY c.relname LIMIT 10001", &[&oid]).await?;
    check(&rows)?;
    for row in rows {
        let id: u32 = row.get("oid");
        let name = text(&row, "name")?;
        let mut child = object(
            format!("pg:index:{id}"),
            Some(parent.clone()),
            name.clone(),
            format!("{}.{}", quote(&text(&row, "schema")?), quote(&name)),
            ObjectKind::Index,
            false,
        );
        child.properties.push(MetadataProperty::available(
            "Unique",
            row.get::<_, bool>("indisunique"),
        ));
        child.properties.push(MetadataProperty::available(
            "Valid",
            row.get::<_, bool>("indisvalid"),
        ));
        child.properties.push(MetadataProperty::available(
            "Partial",
            row.get::<_, bool>("partial"),
        ));
        child.properties.push(MetadataProperty::available(
            "Definition",
            text(&row, "definition")?,
        ));
        result.push(child);
    }
    if result.len() > MAX_OBJECTS
        || result
            .iter()
            .map(|o| {
                o.id.0.len()
                    + o.name.len()
                    + o.qualified_name.len()
                    + o.column.as_ref().map_or(0, |c| c.database_type.len())
                    + o.properties
                        .iter()
                        .map(|p| p.name.len() + p.value.len() + p.reason.len())
                        .sum::<usize>()
            })
            .sum::<usize>()
            > MAX_TEXT
    {
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
        "sequence" => {
            "SELECT 'CREATE SEQUENCE ' || pg_catalog.quote_ident(n.nspname) || '.' || pg_catalog.quote_ident(c.relname) || ' INCREMENT BY ' || s.seqincrement::text || ' MINVALUE ' || s.seqmin::text || ' MAXVALUE ' || s.seqmax::text || ' START WITH ' || s.seqstart::text || ' CACHE ' || s.seqcache::text || CASE WHEN s.seqcycle THEN ' CYCLE' ELSE ' NO CYCLE' END AS ddl FROM pg_catalog.pg_class c JOIN pg_catalog.pg_namespace n ON n.oid=c.relnamespace JOIN pg_catalog.pg_sequence s ON s.seqrelid=c.oid WHERE c.oid=$1"
        }
        "function" => {
            "SELECT pg_catalog.pg_get_functiondef(p.oid) AS ddl FROM pg_catalog.pg_proc p WHERE p.oid=$1 AND p.prokind='f'"
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
    let rows=bounded_query(client, "SELECT a.attname::text AS name,pg_catalog.format_type(a.atttypid,a.atttypmod) AS datatype,a.attnotnull,a.attidentity::text AS identity,a.attgenerated::text AS generated,COALESCE(pg_catalog.pg_get_expr(d.adbin,d.adrelid),'') AS expression,a.attcollation<>t.typcollation AS custom_collation FROM pg_catalog.pg_attribute a JOIN pg_catalog.pg_type t ON t.oid=a.atttypid LEFT JOIN pg_catalog.pg_attrdef d ON d.adrelid=a.attrelid AND d.adnum=a.attnum WHERE a.attrelid=$1 AND a.attnum>0 AND NOT a.attisdropped ORDER BY a.attnum LIMIT 10001",&[&oid]).await?;
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
    let rows=bounded_query(client, "SELECT conname::text AS name,pg_catalog.pg_get_constraintdef(oid,false) AS ddl FROM pg_catalog.pg_constraint WHERE conrelid=$1 ORDER BY conname LIMIT 10001",&[&oid]).await?;
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
        client.batch_execute("CREATE SEQUENCE \"metadata odd\".counter; CREATE FUNCTION \"metadata odd\".overloaded(value integer) RETURNS integer LANGUAGE sql AS 'SELECT value'; CREATE FUNCTION \"metadata odd\".overloaded(value text) RETURNS text LANGUAGE sql AS 'SELECT value';").await.unwrap();
        let database = load_metadata(&client, None).await.unwrap().remove(0);
        let schema = load_metadata(&client, Some(database.id))
            .await
            .unwrap()
            .into_iter()
            .find(|o| o.name == "metadata odd")
            .unwrap();
        let groups = load_metadata(&client, Some(schema.id)).await.unwrap();
        assert_eq!(groups.iter().map(|o| o.name.as_str()).collect::<Vec<_>>(), vec!["Tables", "Views", "Indexes", "Sequences", "Functions"]);
        let relations = load_metadata(&client, Some(groups[0].id.clone())).await.unwrap();
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
        assert_eq!(children.iter().find(|o|o.name=="label").unwrap().properties.iter().find(|p|p.name=="Default").map(|p|p.value.as_str()), Some("'x'::text"));
        assert!(children.iter().find(|o|o.name=="index odd").unwrap().properties.iter().any(|p|p.name=="Definition" && p.value.contains("USING btree (amount)")));
        assert!(children.iter().any(|o| o.kind == ObjectKind::PrimaryKey));
        assert!(children.iter().any(|o| o.kind == ObjectKind::UniqueKey));
        let index = children.iter().find(|o| o.name == "index odd").unwrap();
        let schema_indexes = load_metadata(&client, Some(groups[2].id.clone())).await.unwrap();
        assert_eq!(schema_indexes.iter().find(|o| o.name == "index odd").unwrap().id, index.id);
        let sequences = load_metadata(&client, Some(groups[3].id.clone())).await.unwrap();
        let sequence = sequences.iter().find(|o| o.name == "counter").unwrap();
        assert_eq!(sequence.kind, ObjectKind::Sequence);
        assert!(object_ddl(&client, &sequence.id).await.unwrap().contains("CREATE SEQUENCE"));
        let functions = load_metadata(&client, Some(groups[4].id.clone())).await.unwrap();
        let overloads: Vec<_> = functions.iter().filter(|o| o.name.starts_with("overloaded(")).collect();
        assert_eq!(overloads.len(), 2);
        assert_ne!(overloads[0].qualified_name, overloads[1].qualified_name);
        assert_ne!(overloads[0].id, overloads[1].id);
        assert!(object_ddl(&client, &overloads[0].id).await.unwrap().contains("CREATE OR REPLACE FUNCTION"));
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
        let views = load_metadata(&client, Some(groups[1].id.clone())).await.unwrap();
        let view = views
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
