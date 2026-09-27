use crate::{error, id, normalize, parse_table, quote};
use choscordb_driver_api::*;
use mysql_async::{Conn, prelude::Queryable};

type ForeignKeyCatalogRow = (
    String,
    String,
    String,
    String,
    String,
    Option<String>,
    Option<String>,
);

pub(crate) async fn inspect_target(conn: &mut Conn, object: &ObjectId) -> Result<EditTarget> {
    let (database, table) = parse_table(object)?;
    let mut target = EditTarget {
        qualified_name: format!("{}.{}", quote(&database), quote(&table)),
        parameter_style: "?".into(),
        ..Default::default()
    };
    let kind: Option<(String, Option<String>)> = conn.exec_first(
        "SELECT TABLE_TYPE, ENGINE FROM information_schema.TABLES WHERE TABLE_SCHEMA = ? AND TABLE_NAME = ?",
        (&database, &table),
    ).await.map_err(normalize)?;
    if !kind
        .is_some_and(|(kind, engine)| kind == "BASE TABLE" && engine.as_deref() == Some("InnoDB"))
    {
        target.reason = "Only transactional InnoDB base tables can be edited".into();
        return Ok(target);
    }
    let columns: Vec<(String, String, String, String, String)> = conn.exec(
        "SELECT COLUMN_NAME, COLUMN_TYPE, IS_NULLABLE, EXTRA, COLUMN_KEY FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = ? AND TABLE_NAME = ? ORDER BY ORDINAL_POSITION",
        (&database, &table),
    ).await.map_err(normalize)?;
    for (name, database_type, nullable, extra, key) in columns {
        let key = key == "PRI";
        if key {
            target.key_columns.push(name.clone());
        }
        target.columns.push(EditColumn {
            name,
            database_type,
            nullable: nullable == "YES",
            generated: extra.contains("VIRTUAL GENERATED") || extra.contains("STORED GENERATED"),
            key,
        });
    }
    if target.key_columns.is_empty() {
        target.reason = "No stable primary key; inserts only".into();
    }
    Ok(target)
}

// Adapt MySQL backtick identifiers to the shared conservative shape recognizer.
// Reject escape-dependent and double-quoted syntax rather than guessing modes.
fn query_shape(sql: &str, columns: &[String]) -> Option<SimpleSelect> {
    let mut translated = String::new();
    let mut chars = sql.chars().peekable();
    while let Some(ch) = chars.next() {
        match ch {
            '\\' | '"' | '#' => return None,
            '`' => {
                translated.push('"');
                loop {
                    match chars.next()? {
                        '`' if chars.peek() == Some(&'`') => {
                            chars.next();
                            translated.push('`');
                        }
                        '`' => {
                            translated.push('"');
                            break;
                        }
                        '"' => translated.push_str("\"\""),
                        c => translated.push(c),
                    }
                }
            }
            '\'' => {
                translated.push(ch);
                loop {
                    let ch = chars.next()?;
                    if ch == '\\' {
                        return None;
                    }
                    translated.push(ch);
                    if ch == '\'' {
                        if chars.peek() == Some(&'\'') {
                            translated.push(chars.next()?);
                        } else {
                            break;
                        }
                    }
                }
            }
            c => translated.push(c),
        }
    }
    simple_select(&translated, columns)
}

pub(crate) async fn inspect_query(
    conn: &mut Conn,
    sql: &str,
    columns: Vec<String>,
) -> Result<EditQueryTarget> {
    let mut output = EditQueryTarget {
        reason: "Query shape cannot be proven editable".into(),
        ..Default::default()
    };
    let Some(shape) = query_shape(sql, &columns) else {
        return Ok(output);
    };
    let database = match shape.schema {
        Some(database) => database,
        None => conn
            .query_first::<String, _>("SELECT DATABASE()")
            .await
            .map_err(normalize)?
            .ok_or_else(|| error(ErrorKind::Query, "No MySQL database selected"))?,
    };
    let target = inspect_target(conn, &id(&[&database, &shape.table])).await?;
    let sources = shape.source_columns;
    if target.columns.is_empty()
        || sources.len() != columns.len()
        || sources.iter().any(|source| {
            !source.is_empty() && !target.columns.iter().any(|column| column.name == *source)
        })
    {
        return Ok(output);
    }
    if sources
        .iter()
        .filter(|s| !s.is_empty())
        .any(|source| sources.iter().filter(|other| *other == source).count() > 1)
    {
        output.reason = "Duplicate source columns are ambiguous".into();
        return Ok(output);
    }
    if target.key_columns.is_empty() || target.key_columns.iter().any(|key| !sources.contains(key))
    {
        output.reason = "All primary key columns must be present".into();
        return Ok(output);
    }
    output.target = target;
    output.source_columns = sources;
    output.reason.clear();
    Ok(output)
}
fn enum_labels(database_type: &str) -> Option<Vec<String>> {
    let mut text = database_type
        .strip_prefix("enum(")?
        .strip_suffix(')')?
        .chars()
        .peekable();
    let mut labels = Vec::new();
    loop {
        if text.next()? != '\'' {
            return None;
        }
        let mut label = String::new();
        loop {
            match text.next()? {
                '\'' if text.peek() == Some(&'\'') => {
                    text.next();
                    label.push('\'');
                }
                '\'' => break,
                // COLUMN_TYPE quoting depends on server SQL mode. Without a
                // catalog encoding guarantee, escaped labels stay text-editable.
                '\\' => return None,
                ch => label.push(ch),
            }
        }
        labels.push(label);
        match text.next() {
            Some(',') => continue,
            None => return Some(labels),
            _ => return None,
        }
    }
}
pub(crate) async fn result_cells(
    conn: &mut Conn,
    object: Option<&ObjectId>,
    sql: &str,
    result_columns: Vec<String>,
) -> Result<Vec<ResultCellMetadata>> {
    let mut output = vec![ResultCellMetadata::default(); result_columns.len()];
    let (database, table, sources) = if let Some(object) = object {
        let (database, table) = parse_table(object)?;
        (database, table, result_columns.clone())
    } else {
        let Some(shape) = query_shape(sql, &result_columns) else {
            return Ok(output);
        };
        let database = match shape.schema {
            Some(name) => name,
            None => match conn
                .query_first::<String, _>("SELECT DATABASE()")
                .await
                .map_err(normalize)?
            {
                Some(name) => name,
                None => return Ok(output),
            },
        };
        (database, shape.table, shape.source_columns)
    };
    let kind: Option<String> = conn.exec_first("SELECT TABLE_TYPE FROM information_schema.TABLES WHERE TABLE_SCHEMA=? AND TABLE_NAME=?", (&database, &table)).await.map_err(normalize)?;
    if kind.as_deref() != Some("BASE TABLE") {
        return Ok(output);
    }
    let columns: Vec<(String,String,String,String)> = conn.exec("SELECT COLUMN_NAME, DATA_TYPE, COLUMN_TYPE, IS_NULLABLE FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=? AND TABLE_NAME=? ORDER BY ORDINAL_POSITION LIMIT 10001", (&database, &table)).await.map_err(normalize)?;
    if columns.len() > 10_000 {
        return Err(error(
            ErrorKind::ResourceLimit,
            "MySQL metadata exceeds the display limit",
        ));
    }
    if columns
        .iter()
        .map(|(name, kind, definition, nullable)| {
            name.len() + kind.len() + definition.len() + nullable.len()
        })
        .sum::<usize>()
        > 1024 * 1024
    {
        return Err(error(
            ErrorKind::ResourceLimit,
            "MySQL metadata exceeds the display limit",
        ));
    }
    let columns: std::collections::HashMap<_, _> = columns
        .into_iter()
        .map(|(name, data_type, column_type, nullable)| (name, (data_type, column_type, nullable)))
        .collect();
    let fk_rows: Vec<ForeignKeyCatalogRow> = conn.exec("SELECT k.CONSTRAINT_NAME, k.COLUMN_NAME, k.REFERENCED_TABLE_SCHEMA, k.REFERENCED_TABLE_NAME, k.REFERENCED_COLUMN_NAME, t.TABLE_TYPE, tc.COLUMN_NAME FROM information_schema.KEY_COLUMN_USAGE k LEFT JOIN information_schema.TABLES t ON t.TABLE_SCHEMA=k.REFERENCED_TABLE_SCHEMA AND t.TABLE_NAME=k.REFERENCED_TABLE_NAME LEFT JOIN information_schema.COLUMNS tc ON tc.TABLE_SCHEMA=k.REFERENCED_TABLE_SCHEMA AND tc.TABLE_NAME=k.REFERENCED_TABLE_NAME AND tc.COLUMN_NAME=k.REFERENCED_COLUMN_NAME WHERE k.TABLE_SCHEMA=? AND k.TABLE_NAME=? AND k.REFERENCED_TABLE_NAME IS NOT NULL ORDER BY k.CONSTRAINT_NAME,k.ORDINAL_POSITION LIMIT 10001", (&database, &table)).await.map_err(normalize)?;
    if fk_rows.len() > 10_000 {
        return Err(error(
            ErrorKind::ResourceLimit,
            "MySQL metadata exceeds the display limit",
        ));
    }
    if fk_rows
        .iter()
        .map(|(constraint, source, schema, table, column, kind, found)| {
            constraint.len()
                + source.len()
                + schema.len()
                + table.len()
                + column.len()
                + kind.as_ref().map_or(0, String::len)
                + found.as_ref().map_or(0, String::len)
        })
        .sum::<usize>()
        > 1024 * 1024
    {
        return Err(error(
            ErrorKind::ResourceLimit,
            "MySQL metadata exceeds the display limit",
        ));
    }
    let mut groups =
        std::collections::HashMap::<String, Vec<(String, String, String, String, bool)>>::new();
    for (constraint, from, target_schema, target_table, target_column, kind, found_column) in
        fk_rows
    {
        groups.entry(constraint).or_default().push((
            from,
            target_schema,
            target_table,
            target_column,
            kind.as_deref() == Some("BASE TABLE") && found_column.is_some(),
        ));
    }
    let mut fks = std::collections::HashMap::<String, Vec<(String, String, String, bool)>>::new();
    for rows in groups.into_values() {
        if rows.len() == 1 {
            let (from, schema, table, column, supported) = rows.into_iter().next().unwrap();
            fks.entry(from)
                .or_default()
                .push((schema, table, column, supported));
        } else {
            for (from, _, _, _, _) in rows {
                fks.entry(from).or_default().push((
                    String::new(),
                    String::new(),
                    String::new(),
                    false,
                ));
            }
        }
    }
    let source_object = id(&[&database, &table]).0;
    let qualified = format!("{}.{}", quote(&database), quote(&table));
    for (meta, source) in output.iter_mut().zip(&sources) {
        if source.is_empty() || sources.iter().filter(|name| *name == source).count() != 1 {
            continue;
        }
        let Some((data_type, column_type, nullable)) = columns.get(source) else {
            continue;
        };
        meta.source_column = source.clone();
        meta.source_object = source_object.clone();
        meta.source_qualified_name = qualified.clone();
        meta.nullable = Some(nullable == "YES");
        if data_type == "enum" {
            meta.enum_choices = enum_labels(column_type).unwrap_or_default();
        }
        if let Some(entries) = fks.get(source)
            && entries.len() == 1
        {
            let (target_schema, target_table, target_column, supported) = &entries[0];
            if *supported {
                meta.fk_target_object = id(&[target_schema, target_table]).0;
                meta.fk_target_qualified_name =
                    format!("{}.{}", quote(target_schema), quote(target_table));
                meta.fk_target_column = target_column.clone();
            }
        }
    }
    Ok(output)
}

fn bound_value(value: Value) -> Result<mysql_async::Value> {
    use mysql_async::Value as M;
    Ok(match value {
        Value::Null => M::NULL,
        Value::Bool(value) => M::Int(i64::from(value)),
        Value::Integer(value) => M::Int(value),
        Value::Real(value) if value.is_finite() => M::Double(value),
        Value::Real(_) | Value::Deferred { .. } => {
            return Err(error(
                ErrorKind::InvalidInput,
                "Deferred or nonfinite values cannot be edited",
            ));
        }
        Value::Binary(value) => M::Bytes(value),
        Value::Text(value)
        | Value::Decimal(value)
        | Value::Date(value)
        | Value::Time(value)
        | Value::Timestamp(value)
        | Value::Uuid(value)
        | Value::Json(value) => M::Bytes(value.into_bytes()),
    })
}

pub(crate) async fn apply_batch(conn: &mut Conn, batch: EditBatch) -> Result<EditBatchSummary> {
    let autocommit: Option<u8> = conn
        .query_first("SELECT @@session.autocommit")
        .await
        .map_err(normalize)?;
    if crate::transaction_active(conn).unwrap_or(false) || autocommit != Some(1) {
        return Err(error(
            ErrorKind::InvalidInput,
            "Commit or roll back the manual transaction before applying edits",
        ));
    }
    // Bind and validate every statement before opening the owned transaction.
    // Restrict this API to DML: MySQL DDL implicitly commits transactions.
    let mut statements = Vec::with_capacity(batch.statements.len());
    for edit in batch.statements {
        if !edit.sql.split_whitespace().next().is_some_and(|word| {
            ["INSERT", "UPDATE", "DELETE"]
                .iter()
                .any(|allowed| word.eq_ignore_ascii_case(allowed))
        }) {
            return Err(error(
                ErrorKind::InvalidInput,
                "Edit batches accept only INSERT, UPDATE, and DELETE",
            ));
        }
        let params = edit
            .params
            .into_iter()
            .map(bound_value)
            .collect::<Result<Vec<_>>>()?;
        statements.push((edit.sql, params, edit.expected_rows));
    }
    let mut tx = conn
        .start_transaction(mysql_async::TxOpts::default())
        .await
        .map_err(normalize)?;
    let result = async {
        let mut affected_rows = Vec::with_capacity(statements.len());
        for (sql, params, expected) in statements {
            tx.exec_drop(sql, mysql_async::Params::Positional(params))
                .await
                .map_err(normalize)?;
            let count = tx.affected_rows();
            if expected.is_some_and(|expected| expected != count) {
                return Err(error(
                    ErrorKind::Query,
                    "Edit conflict: the original row has changed",
                ));
            }
            affected_rows.push(count);
        }
        Ok(EditBatchSummary { affected_rows })
    }
    .await;
    match result {
        Ok(summary) => {
            tx.commit().await.map_err(normalize)?;
            Ok(summary)
        }
        Err(failure) => {
            tx.rollback().await.map_err(normalize)?;
            Err(failure)
        }
    }
}
