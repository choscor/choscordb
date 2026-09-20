use super::normalize;
use choscordb_driver_api::*;
use rusqlite::{Connection, OptionalExtension, params};
fn quote(s: &str) -> String {
    format!("\"{}\"", s.replace('"', "\"\""))
}
fn parts(id: &ObjectId) -> Result<Vec<String>> {
    serde_json::from_str(&id.0)
        .map_err(|_| DriverError::new(ErrorKind::InvalidInput, "Invalid SQLite object identifier"))
}
pub(crate) fn edit_target(db: &Connection, id: &ObjectId) -> Result<EditTarget> {
    let p = parts(id)?;
    let [schema, table] = p.as_slice() else {
        return Err(DriverError::new(
            ErrorKind::InvalidInput,
            "A table identifier is required",
        ));
    };
    let kind: String = db
        .query_row(
            &format!(
                "SELECT type FROM {}.sqlite_schema WHERE name=?1",
                quote(schema)
            ),
            [table],
            |r| r.get(0),
        )
        .map_err(normalize)?;
    let mut target = EditTarget {
        qualified_name: format!("{}.{}", quote(schema), quote(table)),
        parameter_style: "?".into(),
        ..Default::default()
    };
    if kind != "table" {
        target.reason = "Only base tables can be edited".into();
        return Ok(target);
    }
    let (without_rowid, strict): (bool, bool) = db
        .query_row(
            "SELECT wr,strict FROM pragma_table_list WHERE schema=?1 AND name=?2",
            params![schema, table],
            |r| Ok((r.get(0)?, r.get(1)?)),
        )
        .map_err(normalize)?;
    let has_primary_index: bool = db
        .query_row(
            "SELECT EXISTS(SELECT 1 FROM pragma_index_list(?1,?2) WHERE origin='pk')",
            params![table, schema],
            |r| r.get(0),
        )
        .map_err(normalize)?;
    let mut keys = Vec::<(i64, String)>::new();
    let mut statement = db
        .prepare(
            "SELECT name,type,\"notnull\",pk,hidden FROM pragma_table_xinfo(?1,?2) ORDER BY cid",
        )
        .map_err(normalize)?;
    let rows = statement
        .query_map(params![table, schema], |r| {
            Ok((
                r.get::<_, String>(0)?,
                r.get::<_, String>(1)?,
                r.get::<_, i64>(2)?,
                r.get::<_, i64>(3)?,
                r.get::<_, i64>(4)?,
            ))
        })
        .map_err(normalize)?;
    for row in rows {
        let (name, database_type, notnull, pk, hidden) = row.map_err(normalize)?;
        if hidden == 1 {
            continue;
        }
        let rowid_alias =
            pk > 0 && database_type.eq_ignore_ascii_case("INTEGER") && !has_primary_index;
        let nullable = notnull == 0 && !(pk > 0 && (without_rowid || strict || rowid_alias));
        if pk > 0 {
            keys.push((pk, name.clone()));
        }
        target.columns.push(EditColumn {
            name,
            database_type,
            nullable,
            generated: hidden != 0,
            key: false,
        });
    }
    keys.sort_by_key(|(position, _)| *position);
    target.key_columns = keys.into_iter().map(|(_, name)| name).collect();
    if target.key_columns.iter().any(|name| {
        target
            .columns
            .iter()
            .any(|column| column.name == *name && column.nullable)
    }) {
        target.key_columns.clear();
    }
    if target.key_columns.is_empty() {
        let mut indexes = db.prepare("SELECT name FROM pragma_index_list(?1,?2) WHERE \"unique\"=1 AND partial=0 ORDER BY seq").map_err(normalize)?;
        let names = indexes
            .query_map(params![table, schema], |r| r.get::<_, String>(0))
            .map_err(normalize)?;
        for name in names {
            let name = name.map_err(normalize)?;
            let mut columns = Vec::new();
            let mut valid = true;
            let mut info = db
                .prepare("SELECT name FROM pragma_index_xinfo(?1,?2) WHERE key=1 ORDER BY seqno")
                .map_err(normalize)?;
            let rows = info
                .query_map(params![name, schema], |r| r.get::<_, Option<String>>(0))
                .map_err(normalize)?;
            for row in rows {
                let Some(column) = row.map_err(normalize)? else {
                    valid = false;
                    break;
                };
                if !target
                    .columns
                    .iter()
                    .any(|c| c.name == column && !c.nullable && !c.generated)
                {
                    valid = false;
                    break;
                }
                columns.push(column);
            }
            if valid && !columns.is_empty() {
                target.key_columns = columns;
                break;
            }
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
pub(crate) fn edit_query(
    db: &Connection,
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
    if shape.schema.is_none() {
        let shadowed: bool = db
            .query_row(
                "SELECT EXISTS(SELECT 1 FROM temp.sqlite_schema WHERE name=?1)",
                [&shape.table],
                |r| r.get(0),
            )
            .map_err(normalize)?;
        if shadowed {
            return Ok(output);
        }
    }
    let schema = shape.schema.unwrap_or_else(|| "main".into());
    let id = ObjectId(
        serde_json::to_string(&[&schema, &shape.table])
            .map_err(|_| DriverError::new(ErrorKind::Internal, "Cannot identify table"))?,
    );
    let target = match edit_target(db, &id) {
        Ok(target) => target,
        Err(_) => return Ok(output),
    };
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
const MAX_METADATA_BYTES: usize = 1024 * 1024;
fn limit() -> DriverError {
    DriverError::new(
        ErrorKind::ResourceLimit,
        "SQLite metadata exceeds the display limit",
    )
}
fn quoted_bytes(name: &str) -> usize {
    name.len()
        .saturating_add(name.bytes().filter(|b| *b == b'"').count())
        .saturating_add(2)
}
fn append_quoted(output: &mut String, name: &str) {
    output.push('"');
    for c in name.chars() {
        if c == '"' {
            output.push('"');
        }
        output.push(c);
    }
    output.push('"');
}
#[derive(Default)]
struct ByteCounter(usize);
impl std::io::Write for ByteCounter {
    fn write(&mut self, bytes: &[u8]) -> std::io::Result<usize> {
        self.0 = self.0.saturating_add(bytes.len());
        Ok(bytes.len())
    }
    fn flush(&mut self) -> std::io::Result<()> {
        Ok(())
    }
}
impl std::fmt::Write for ByteCounter {
    fn write_str(&mut self, text: &str) -> std::fmt::Result {
        self.0 = self.0.saturating_add(text.len());
        Ok(())
    }
}
struct Budget {
    bytes: usize,
    objects: usize,
}
impl Budget {
    fn new() -> Self {
        Self {
            bytes: std::mem::size_of::<Vec<SchemaObject>>(),
            objects: 0,
        }
    }
    fn reserve(&mut self, bytes: usize) -> Result<()> {
        self.bytes = self
            .bytes
            .checked_add(bytes)
            .filter(|n| *n <= MAX_METADATA_BYTES)
            .ok_or_else(limit)?;
        Ok(())
    }
    #[allow(clippy::too_many_arguments)]
    fn object(
        &mut self,
        parts: &[&str],
        parent: Option<&ObjectId>,
        name: &str,
        qualified: &[&str],
        kind: ObjectKind,
        has_children: bool,
    ) -> Result<SchemaObject> {
        if self.objects == 10_000 {
            return Err(limit());
        }
        let mut encoded = ByteCounter::default();
        serde_json::to_writer(&mut encoded, parts).expect("strings serialize");
        let qualified_bytes = qualified.iter().map(|s| quoted_bytes(s)).sum::<usize>()
            + qualified.len().saturating_sub(1);
        // Reserve every owned identity copy before cloning a long parent into
        // another row. Fixed object storage includes its inline optional Column
        // and Vec headers; their separately allocated payloads are charged below.
        self.reserve(
            std::mem::size_of::<SchemaObject>()
                + encoded.0
                + parent.map_or(0, |p| p.0.len())
                + name.len()
                + qualified_bytes,
        )?;
        self.objects += 1;
        let mut id = Vec::with_capacity(encoded.0);
        serde_json::to_writer(&mut id, parts).expect("strings serialize");
        let mut qualified_name = String::with_capacity(qualified_bytes);
        for (i, part) in qualified.iter().enumerate() {
            if i > 0 {
                qualified_name.push('.');
            }
            append_quoted(&mut qualified_name, part);
        }
        Ok(SchemaObject {
            id: ObjectId(String::from_utf8(id).expect("JSON is UTF-8")),
            parent: parent.cloned(),
            name: name.to_owned(),
            qualified_name,
            kind,
            has_children,
            column: None,
            properties: Vec::new(),
        })
    }
    fn available(
        &mut self,
        object: &mut SchemaObject,
        name: &str,
        value: impl std::fmt::Display,
    ) -> Result<()> {
        let mut length = ByteCounter::default();
        std::fmt::write(&mut length, format_args!("{value}")).expect("metadata formats");
        self.reserve(std::mem::size_of::<MetadataProperty>() + name.len() + length.0)?;
        let mut text = String::with_capacity(length.0);
        std::fmt::write(&mut text, format_args!("{value}")).expect("metadata formats");
        object.properties.reserve_exact(1);
        object.properties.push(MetadataProperty {
            name: name.into(),
            value: text,
            availability: MetadataAvailability::Available,
            reason: String::new(),
        });
        Ok(())
    }
    fn property(&mut self, object: &mut SchemaObject, property: MetadataProperty) -> Result<()> {
        self.reserve(
            std::mem::size_of::<MetadataProperty>()
                + property.name.capacity()
                + property.value.capacity()
                + property.reason.capacity(),
        )?;
        object.properties.reserve_exact(1);
        object.properties.push(property);
        Ok(())
    }
}
fn push(output: &mut Vec<SchemaObject>, object: SchemaObject) {
    // Budget::object has already reserved this element; avoid geometric spare capacity.
    output.reserve_exact(1);
    output.push(object);
}
fn load_inner(db: &Connection, parent: Option<ObjectId>) -> Result<Vec<SchemaObject>> {
    let mut budget = Budget::new();
    let mut output = Vec::new();
    let Some(parent) = parent else {
        let mut q = db.prepare("PRAGMA database_list").map_err(normalize)?;
        let mut rows = q.query([]).map_err(normalize)?;
        while let Some(row) = rows.next().map_err(normalize)? {
            let name: &str = row
                .get_ref(1)
                .map_err(normalize)?
                .as_str()
                .map_err(|_| limit())?;
            push(
                &mut output,
                budget.object(&[name], None, name, &[name], ObjectKind::Database, true)?,
            );
        }
        return Ok(output);
    };
    let names = parts(&parent)?;
    match names.as_slice() {
        [schema] => {
            for (label, key) in [("Tables", "table"), ("Views", "view"), ("Indexes", "index")] {
                push(
                    &mut output,
                    budget.object(
                        &[schema, "group", key],
                        Some(&parent),
                        label,
                        &[schema],
                        ObjectKind::Group,
                        true,
                    )?,
                );
            }
            Ok(output)
        }
        [schema, marker, requested]
            if marker == "group" && matches!(requested.as_str(), "table" | "view" | "index") =>
        {
            if requested == "index" {
                let sql = format!(
                    "SELECT name,tbl_name,sql FROM {}.sqlite_schema WHERE type='index' ORDER BY name LIMIT 10001",
                    quote(schema)
                );
                let mut q = db.prepare(&sql).map_err(normalize)?;
                let mut rows = q.query([]).map_err(normalize)?;
                while let Some(row) = rows.next().map_err(normalize)? {
                    let name: String = row.get(0).map_err(normalize)?;
                    let table: String = row.get(1).map_err(normalize)?;
                    let definition: Option<String> = row.get(2).map_err(normalize)?;
                    let mut index = budget.object(
                        &[schema, &table, "index", &name],
                        Some(&parent),
                        &name,
                        &[schema, &name],
                        ObjectKind::Index,
                        false,
                    )?;
                    budget.available(
                        &mut index,
                        "Table",
                        format!("{}.{}", quote(schema), quote(&table)),
                    )?;
                    let (unique, partial): (bool, bool) = db
                        .query_row(
                            "SELECT \"unique\",partial FROM pragma_index_list(?1,?2) WHERE name=?3",
                            params![table, schema, name],
                            |r| Ok((r.get(0)?, r.get(1)?)),
                        )
                        .map_err(normalize)?;
                    budget.available(&mut index, "Unique", unique)?;
                    budget.available(&mut index, "Partial", partial)?;
                    budget.property(
                        &mut index,
                        match definition {
                            Some(ddl) => MetadataProperty::available("Definition", ddl),
                            None => MetadataProperty {
                                name: "Definition".into(),
                                value: String::new(),
                                availability: MetadataAvailability::Unavailable,
                                reason: "SQLite creates this index implicitly for a constraint"
                                    .into(),
                            },
                        },
                    )?;
                    push(&mut output, index);
                }
                return Ok(output);
            }
            let sql = format!(
                "SELECT name,type FROM {}.sqlite_schema WHERE type=?1 AND name NOT LIKE 'sqlite_%' ORDER BY name LIMIT 10001",
                quote(schema)
            );
            let mut q = db.prepare(&sql).map_err(normalize)?;
            let mut rows = q.query([requested]).map_err(normalize)?;
            while let Some(row) = rows.next().map_err(normalize)? {
                let name: &str = row
                    .get_ref(0)
                    .map_err(normalize)?
                    .as_str()
                    .map_err(|_| limit())?;
                let kind: &str = row
                    .get_ref(1)
                    .map_err(normalize)?
                    .as_str()
                    .map_err(|_| limit())?;
                push(
                    &mut output,
                    budget.object(
                        &[schema, name],
                        Some(&parent),
                        name,
                        &[schema, name],
                        if kind == "view" {
                            ObjectKind::View
                        } else {
                            ObjectKind::Table
                        },
                        true,
                    )?,
                );
            }
            Ok(output)
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
            let mut q = db
                .prepare("SELECT name,type,\"notnull\",pk,dflt_value,hidden FROM pragma_table_xinfo(?1,?2) LIMIT 10001")
                .map_err(normalize)?;
            let mut rows = q.query(params![table, schema]).map_err(normalize)?;
            while let Some(r) = rows.next().map_err(normalize)? {
                let name: String = r.get(0).map_err(normalize)?;
                let ty: String = r.get(1).map_err(normalize)?;
                let pk: i64 = r.get(3).map_err(normalize)?;
                let mut obj = budget.object(
                    &[schema, table, "column", &name],
                    Some(&parent),
                    &name,
                    &[schema, table, &name],
                    ObjectKind::Column,
                    false,
                )?;
                let rowid_alias =
                    pk > 0 && ty.eq_ignore_ascii_case("INTEGER") && !has_primary_index;
                let required_primary_key = pk > 0 && (without_rowid || strict || rowid_alias);
                budget.reserve(name.len() + ty.capacity())?;
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
                budget.available(
                    &mut obj,
                    "Default",
                    r.get::<_, Option<String>>(4)
                        .map_err(normalize)?
                        .unwrap_or_else(|| "No default".into()),
                )?;
                budget.available(&mut obj, "Primary key position", pk)?;
                budget.available(
                    &mut obj,
                    "Generated",
                    match r.get::<_, i64>(5).map_err(normalize)? {
                        2 => "Virtual",
                        3 => "Stored",
                        1 => "Hidden",
                        _ => "No",
                    },
                )?;
                push(&mut output, obj);
                if pk > 0 {
                    push(
                        &mut output,
                        budget.object(
                            &[schema, table, "pk", &name],
                            Some(&parent),
                            &name,
                            &[&name],
                            ObjectKind::PrimaryKey,
                            false,
                        )?,
                    );
                }
            }
            let mut q = db
                .prepare("SELECT name,\"unique\" FROM pragma_index_list(?1,?2)")
                .map_err(normalize)?;
            let mut rows = q.query(params![table, schema]).map_err(normalize)?;
            while let Some(r) = rows.next().map_err(normalize)? {
                let name: String = r.get(0).map_err(normalize)?;
                push(
                    &mut output,
                    budget.object(
                        &[schema, table, "index", &name],
                        Some(&parent),
                        &name,
                        &[schema, &name],
                        ObjectKind::Index,
                        false,
                    )?,
                );
                if r.get::<_, i64>(1).map_err(normalize)? != 0 {
                    push(
                        &mut output,
                        budget.object(
                            &[schema, table, "unique", &name],
                            Some(&parent),
                            &name,
                            &[&name],
                            ObjectKind::UniqueKey,
                            false,
                        )?,
                    );
                }
            }
            let mut q = db
                .prepare("SELECT id,\"from\" FROM pragma_foreign_key_list(?1,?2)")
                .map_err(normalize)?;
            let mut rows = q.query(params![table, schema]).map_err(normalize)?;
            while let Some(r) = rows.next().map_err(normalize)? {
                let key = r.get::<_, i64>(0).map_err(normalize)?.to_string();
                let name: String = r.get(1).map_err(normalize)?;
                push(
                    &mut output,
                    budget.object(
                        &[schema, table, "fk", &key, &name],
                        Some(&parent),
                        &name,
                        &[&name],
                        ObjectKind::ForeignKey,
                        false,
                    )?,
                );
            }
            for obj in &mut output {
                let p = parts(&obj.id)?;
                match obj.kind {
                    ObjectKind::Index | ObjectKind::UniqueKey => {
                        let name = &p[3];
                        let (unique, partial): (bool, bool) = db.query_row("SELECT \"unique\",partial FROM pragma_index_list(?1,?2) WHERE name=?3", params![table, schema, name], |r| Ok((r.get(0)?, r.get(1)?))).map_err(normalize)?;
                        budget.available(obj, "Unique", unique)?;
                        budget.available(obj, "Partial", partial)?;
                        let mut q = db.prepare("SELECT name,cid FROM pragma_index_xinfo(?1,?2) WHERE key=1 ORDER BY seqno LIMIT 10001").map_err(normalize)?;
                        let mut rows = q.query(params![name, schema]).map_err(normalize)?;
                        let mut columns = String::new();
                        while let Some(row) = rows.next().map_err(normalize)? {
                            let borrowed = row.get_ref(0).map_err(normalize)?;
                            let text = match borrowed {
                                rusqlite::types::ValueRef::Null => None,
                                _ => Some(borrowed.as_str().map_err(|_| limit())?),
                            };
                            let fallback = if row.get::<_, i64>(1).map_err(normalize)? == -1 {
                                "rowid"
                            } else {
                                "Expression (see definition)"
                            };
                            let additional = text.map_or(fallback.len(), quoted_bytes)
                                + if columns.is_empty() { 0 } else { 2 };
                            let needed = budget
                                .bytes
                                .saturating_add(std::mem::size_of::<MetadataProperty>())
                                .saturating_add("Columns".len())
                                .saturating_add(columns.len())
                                .saturating_add(additional);
                            if needed > MAX_METADATA_BYTES {
                                return Err(limit());
                            }
                            columns.reserve_exact(additional);
                            if !columns.is_empty() {
                                columns.push_str(", ");
                            }
                            if let Some(text) = text {
                                append_quoted(&mut columns, text);
                            } else {
                                columns.push_str(fallback);
                            }
                        }
                        budget.available(obj, "Columns", columns)?;
                        let ddl: Option<String> = db
                            .query_row(
                                &format!(
                                    "SELECT sql FROM {}.sqlite_schema WHERE name=?1",
                                    quote(schema)
                                ),
                                [name],
                                |r| r.get::<_, Option<String>>(0),
                            )
                            .optional()
                            .map_err(normalize)?
                            .flatten();
                        budget.property(
                            obj,
                            match ddl {
                                Some(ddl) => MetadataProperty::available("Definition", ddl),
                                None => MetadataProperty {
                                    name: "Definition".into(),
                                    value: String::new(),
                                    availability: MetadataAvailability::Unavailable,
                                    reason: "SQLite creates this index implicitly for a constraint"
                                        .into(),
                                },
                            },
                        )?;
                    }
                    ObjectKind::ForeignKey => {
                        let (target, column, update, delete): (String, Option<String>, String, String) = db.query_row("SELECT \"table\",\"to\",on_update,on_delete FROM pragma_foreign_key_list(?1,?2) WHERE id=?3 AND \"from\"=?4", params![table,schema,p[3].parse::<i64>().map_err(|_| DriverError::new(ErrorKind::InvalidInput,"Invalid foreign key identifier"))?,p[4]], |r| Ok((r.get(0)?,r.get(1)?,r.get(2)?,r.get(3)?))).map_err(normalize)?;
                        budget.available(obj, "Columns", quote(&obj.name))?;
                        budget.available(
                            obj,
                            "References",
                            format!(
                                "{}.{} ({})",
                                quote(schema),
                                quote(&target),
                                column
                                    .map(|v| quote(&v))
                                    .unwrap_or_else(|| "Primary key".into())
                            ),
                        )?;
                        budget.available(obj, "On update", update)?;
                        budget.available(obj, "On delete", delete)?;
                    }
                    ObjectKind::PrimaryKey => budget.available(obj, "Columns", quote(&obj.name))?,
                    _ => {}
                }
            }
            Ok(output)
        }
        _ => Ok(vec![]),
    }
}
pub fn ddl(db: &Connection, object: ObjectId) -> Result<String> {
    let p = parts(&object)?;
    let (schema, name) = match p.as_slice() {
        [schema, name] => (schema, name),
        [schema, _, marker, name] if marker == "index" => (schema, name),
        _ => {
            return Err(DriverError::new(
                ErrorKind::Unsupported,
                "DDL requires a table, view, or explicit index",
            ));
        }
    };
    let mut statement = db
        .prepare(&format!(
            "SELECT sql FROM {}.sqlite_schema WHERE name=?1",
            quote(schema)
        ))
        .map_err(normalize)?;
    let mut rows = statement.query([name]).map_err(normalize)?;
    let row = rows.next().map_err(normalize)?.ok_or_else(|| {
        DriverError::new(ErrorKind::StaleHandle, "SQLite object no longer exists")
    })?;
    let text = row.get_ref(0).map_err(normalize)?.as_str().map_err(|_| {
        DriverError::new(
            ErrorKind::Unsupported,
            "SQLite does not expose a definition for this object",
        )
    })?;
    if text.len() > 1024 * 1024 {
        return Err(DriverError::new(
            ErrorKind::ResourceLimit,
            "SQLite DDL exceeds the display limit",
        ));
    }
    Ok(text.to_owned())
}

// Bound aggregate metadata transferred to the application, including property text.
pub fn load(db: &Connection, parent: Option<ObjectId>) -> Result<Vec<SchemaObject>> {
    if let Some(ref parent) = parent {
        let names = parts(parent)?;
        if let Some(schema) = names.first() {
            let sql = if names.len() == 1 || names.get(1).is_some_and(|name| name == "group") {
                format!(
                    "SELECT COALESCE(SUM(length(CAST(name AS BLOB))),0) FROM {}.sqlite_schema",
                    quote(schema)
                )
            } else {
                format!(
                    "SELECT COALESCE(SUM(length(CAST(name AS BLOB)) + COALESCE(length(CAST(sql AS BLOB)),0)),0) FROM {}.sqlite_schema WHERE tbl_name=?1",
                    quote(schema)
                )
            };
            let size: i64 = if names.len() == 1 || names.get(1).is_some_and(|name| name == "group")
            {
                db.query_row(&sql, [], |r| r.get(0))
            } else {
                db.query_row(&sql, [&names[1]], |r| r.get(0))
            }
            .map_err(normalize)?;
            if size > 1024 * 1024 {
                return Err(DriverError::new(
                    ErrorKind::ResourceLimit,
                    "SQLite metadata exceeds the display limit",
                ));
            }
        }
    }
    load_inner(db, parent)
}
