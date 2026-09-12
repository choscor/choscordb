use super::normalize;
use choscordb_driver_api::*;
use rusqlite::{Connection, params};
fn quote(s: &str) -> String {
    format!("\"{}\"", s.replace('"', "\"\""))
}
fn id(parts: &[&str]) -> ObjectId {
    ObjectId(serde_json::to_string(parts).expect("strings serialize"))
}
fn parts(id: &ObjectId) -> Result<Vec<String>> {
    serde_json::from_str(&id.0)
        .map_err(|_| DriverError::new(ErrorKind::InvalidInput, "Invalid SQLite object identifier"))
}
fn object(
    parts: &[&str],
    parent: Option<ObjectId>,
    name: String,
    qualified_name: String,
    kind: ObjectKind,
    has_children: bool,
) -> SchemaObject {
    SchemaObject {
        id: id(parts),
        parent,
        name,
        qualified_name,
        kind,
        has_children,
        column: None,
    }
}
pub fn load(db: &Connection, parent: Option<ObjectId>) -> Result<Vec<SchemaObject>> {
    let Some(parent) = parent else {
        let mut q = db.prepare("PRAGMA database_list").map_err(normalize)?;
        return q
            .query_map([], |r| r.get::<_, String>(1))
            .map_err(normalize)?
            .map(|n| {
                n.map(|n| {
                    object(
                        &[&n],
                        None,
                        n.clone(),
                        quote(&n),
                        ObjectKind::Database,
                        true,
                    )
                })
                .map_err(normalize)
            })
            .collect();
    };
    let names = parts(&parent)?;
    match names.as_slice() {
        [schema] => {
            let sql = format!(
                "SELECT name,type FROM {}.sqlite_schema WHERE type IN ('table','view') AND name NOT LIKE 'sqlite_%' ORDER BY name",
                quote(schema)
            );
            let mut q = db.prepare(&sql).map_err(normalize)?;
            q.query_map([], |r| Ok((r.get::<_, String>(0)?, r.get::<_, String>(1)?)))
                .map_err(normalize)?
                .map(|r| {
                    let (name, kind) = r.map_err(normalize)?;
                    Ok(object(
                        &[schema, &name],
                        Some(parent.clone()),
                        name.clone(),
                        format!("{}.{}", quote(schema), quote(&name)),
                        if kind == "view" {
                            ObjectKind::View
                        } else {
                            ObjectKind::Table
                        },
                        true,
                    ))
                })
                .collect()
        }
        [schema, table] => {
            let (without_rowid, strict): (bool, bool) = db
                .query_row(
                    "SELECT wr,strict FROM pragma_table_list WHERE schema=?1 AND name=?2",
                    params![schema, table],
                    |row| Ok((row.get(0)?, row.get(1)?)),
                )
                .map_err(normalize)?;
            let has_primary_index: bool = db
                .query_row(
                    "SELECT EXISTS(SELECT 1 FROM pragma_index_list(?1,?2) WHERE origin='pk')",
                    params![table, schema],
                    |row| row.get(0),
                )
                .map_err(normalize)?;
            let mut output = Vec::new();
            let mut q = db
                .prepare("SELECT name,type,\"notnull\",pk FROM pragma_table_xinfo(?1,?2)")
                .map_err(normalize)?;
            let mut rows = q.query(params![table, schema]).map_err(normalize)?;
            while let Some(r) = rows.next().map_err(normalize)? {
                let name: String = r.get(0).map_err(normalize)?;
                let ty: String = r.get(1).map_err(normalize)?;
                let pk: i64 = r.get(3).map_err(normalize)?;
                let mut obj = object(
                    &[schema, table, "column", &name],
                    Some(parent.clone()),
                    name.clone(),
                    format!("{}.{}.{}", quote(schema), quote(table), quote(&name)),
                    ObjectKind::Column,
                    false,
                );
                let rowid_alias =
                    pk > 0 && ty.eq_ignore_ascii_case("INTEGER") && !has_primary_index;
                let required_primary_key = pk > 0 && (without_rowid || strict || rowid_alias);
                obj.column = Some(Column {
                    name: name.clone(),
                    database_type: ty,
                    precision: None,
                    scale: None,
                    timezone: None,
                    nullable: Some(
                        r.get::<_, i64>(2).map_err(normalize)? == 0 && !required_primary_key,
                    ),
                });
                output.push(obj);
                if pk > 0 {
                    output.push(object(
                        &[schema, table, "pk", &name],
                        Some(parent.clone()),
                        name.clone(),
                        quote(&name),
                        ObjectKind::PrimaryKey,
                        false,
                    ));
                }
            }
            let mut q = db
                .prepare("SELECT name,\"unique\" FROM pragma_index_list(?1,?2)")
                .map_err(normalize)?;
            let mut rows = q.query(params![table, schema]).map_err(normalize)?;
            while let Some(r) = rows.next().map_err(normalize)? {
                let name: String = r.get(0).map_err(normalize)?;
                output.push(object(
                    &[schema, table, "index", &name],
                    Some(parent.clone()),
                    name.clone(),
                    format!("{}.{}", quote(schema), quote(&name)),
                    ObjectKind::Index,
                    false,
                ));
                if r.get::<_, i64>(1).map_err(normalize)? != 0 {
                    output.push(object(
                        &[schema, table, "unique", &name],
                        Some(parent.clone()),
                        name.clone(),
                        quote(&name),
                        ObjectKind::UniqueKey,
                        false,
                    ));
                }
            }
            let mut q = db
                .prepare("SELECT id,\"from\" FROM pragma_foreign_key_list(?1,?2)")
                .map_err(normalize)?;
            let mut rows = q.query(params![table, schema]).map_err(normalize)?;
            while let Some(r) = rows.next().map_err(normalize)? {
                let key = r.get::<_, i64>(0).map_err(normalize)?.to_string();
                let name: String = r.get(1).map_err(normalize)?;
                output.push(object(
                    &[schema, table, "fk", &key, &name],
                    Some(parent.clone()),
                    name.clone(),
                    quote(&name),
                    ObjectKind::ForeignKey,
                    false,
                ));
            }
            Ok(output)
        }
        _ => Ok(vec![]),
    }
}
pub fn ddl(db: &Connection, object: ObjectId) -> Result<String> {
    let p = parts(&object)?;
    let [schema, name] = p.as_slice() else {
        return Err(DriverError::new(
            ErrorKind::Unsupported,
            "DDL requires a table or view",
        ));
    };
    db.query_row(
        &format!(
            "SELECT sql FROM {}.sqlite_schema WHERE name=?1",
            quote(schema)
        ),
        [name],
        |r| r.get(0),
    )
    .map_err(normalize)
}
