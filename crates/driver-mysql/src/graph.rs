use crate::{error, id, normalize, parse_table, quote};
use choscordb_driver_api::{
    ErrorKind, MetadataAvailability, ObjectGraph, ObjectGraphColumn, ObjectGraphEdge,
    ObjectGraphTable, ObjectId, Result,
};
use mysql_async::{Conn, prelude::Queryable};
use std::collections::{BTreeMap, BTreeSet};

const MAX_GRAPH_TABLES: usize = 64;
const MAX_GRAPH_COLUMNS: usize = 256;
const MAX_GRAPH_TOTAL_COLUMNS: usize = 1024;
const MAX_GRAPH_TEXT_BYTES: usize = 32 * 1024;
const MAX_GRAPH_FK_COLUMNS: usize = 256;

type ForeignRow = (
    String,         // constraint schema
    String,         // source schema
    String,         // source table
    String,         // constraint name
    String,         // source column
    String,         // target schema
    String,         // target table
    Option<String>, // target column
    u64,            // ordinal position
);

fn unavailable(graph: &mut ObjectGraph, reason: &str, warning: String) {
    graph.availability = MetadataAvailability::Unavailable;
    if graph.reason.is_empty() {
        graph.reason = reason.into();
    }
    graph.warnings.push(warning);
}

fn table_id(schema: &str, table: &str) -> ObjectId {
    id(&[schema, table])
}

fn qualified(schema: &str, table: &str) -> String {
    format!("{}.{}", quote(schema), quote(table))
}

fn table_text_bytes(table: &ObjectGraphTable) -> usize {
    table.qualified_name.len()
        + table
            .columns
            .iter()
            .map(|column| column.name.len() + column.database_type.len())
            .sum::<usize>()
}

fn empty_table(schema: &str, table: &str) -> ObjectGraphTable {
    ObjectGraphTable {
        id: table_id(schema, table),
        qualified_name: qualified(schema, table),
        columns: Vec::new(),
    }
}

async fn table_columns(
    conn: &mut Conn,
    schema: &str,
    table: &str,
    remaining_columns: usize,
    remaining_text_bytes: usize,
) -> Result<ObjectGraphTable> {
    let name = qualified(schema, table);
    if remaining_columns == 0 || name.len() >= remaining_text_bytes {
        return Err(error(
            ErrorKind::ResourceLimit,
            "MySQL ER diagram budget reached",
        ));
    }
    // Check the complete type lengths on the server before fetching any
    // COLUMN_TYPE strings. A single large ENUM definition can exceed the
    // entire graph text budget even when its table has only two columns.
    let admission: Option<(u64, u64)> = conn
        .exec_first(
            "SELECT COUNT(*), CAST(COALESCE(SUM(OCTET_LENGTH(COLUMN_NAME) + \
             OCTET_LENGTH(COLUMN_TYPE)), 0) AS UNSIGNED) \
             FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = ? AND TABLE_NAME = ?",
            (schema, table),
        )
        .await
        .map_err(normalize)?;
    let (count, text_bytes) = admission.ok_or_else(|| {
        error(
            ErrorKind::Query,
            "MySQL ER diagram table columns are unavailable",
        )
    })?;
    if count == 0 {
        return Err(error(
            ErrorKind::Query,
            "MySQL ER diagram table columns are unavailable",
        ));
    }
    if count > MAX_GRAPH_COLUMNS as u64
        || count > remaining_columns as u64
        || text_bytes > (remaining_text_bytes - name.len()) as u64
    {
        return Err(error(
            ErrorKind::ResourceLimit,
            "MySQL ER diagram budget reached",
        ));
    }
    let rows: Vec<(String, String, String)> = conn
        .exec(
            "SELECT COLUMN_NAME, COLUMN_TYPE, COLUMN_KEY FROM information_schema.COLUMNS \
             WHERE TABLE_SCHEMA = ? AND TABLE_NAME = ? AND \
             OCTET_LENGTH(COLUMN_NAME) + OCTET_LENGTH(COLUMN_TYPE) <= ? \
             ORDER BY ORDINAL_POSITION LIMIT ?",
            (
                schema,
                table,
                (remaining_text_bytes - name.len()) as u64,
                (MAX_GRAPH_COLUMNS + 1) as u64,
            ),
        )
        .await
        .map_err(normalize)?;
    if rows.is_empty() {
        return Err(error(
            ErrorKind::Query,
            "MySQL ER diagram table columns are unavailable",
        ));
    }
    if rows.len() != count as usize || rows.len() > MAX_GRAPH_COLUMNS {
        return Err(error(
            ErrorKind::ResourceLimit,
            "MySQL ER diagram metadata changed or exceeded the budget",
        ));
    }
    // Mark every declared FK source column on a displayed table. These
    // constraints may point outside the selected table's one-hop graph.
    let foreign_keys: BTreeSet<String> = conn
        .exec::<String, _, _>(
            "SELECT DISTINCT COLUMN_NAME FROM information_schema.KEY_COLUMN_USAGE \
             WHERE TABLE_SCHEMA = ? AND TABLE_NAME = ? AND REFERENCED_TABLE_NAME IS NOT NULL \
             LIMIT ?",
            (schema, table, (MAX_GRAPH_COLUMNS + 1) as u64),
        )
        .await
        .map_err(normalize)?
        .into_iter()
        .collect();
    Ok(ObjectGraphTable {
        id: table_id(schema, table),
        qualified_name: name,
        columns: rows
            .into_iter()
            .map(|(name, database_type, key)| ObjectGraphColumn {
                foreign_key: foreign_keys.contains(&name),
                name,
                database_type,
                primary_key: key == "PRI",
            })
            .collect(),
    })
}

pub(crate) async fn object_graph(conn: &mut Conn, object: &ObjectId) -> Result<ObjectGraph> {
    let (schema, table) = parse_table(object)?;
    let kind: Option<String> = conn
        .exec_first(
            "SELECT TABLE_TYPE FROM information_schema.TABLES WHERE TABLE_SCHEMA = ? AND TABLE_NAME = ?",
            (&schema, &table),
        )
        .await
        .map_err(normalize)?;
    match kind.as_deref() {
        Some("BASE TABLE") => {}
        Some(_) => {
            return Ok(ObjectGraph {
                availability: MetadataAvailability::Unsupported,
                reason: "ER diagrams are available for tables only".into(),
                ..Default::default()
            });
        }
        None => {
            return Err(error(
                ErrorKind::Query,
                "MySQL ER diagram table is unavailable",
            ));
        }
    }
    let selected = match table_columns(
        conn,
        &schema,
        &table,
        MAX_GRAPH_TOTAL_COLUMNS,
        MAX_GRAPH_TEXT_BYTES,
    )
    .await
    {
        Ok(table) => table,
        Err(failure) if failure.kind == ErrorKind::ResourceLimit => {
            return Ok(ObjectGraph {
                availability: MetadataAvailability::Unavailable,
                reason: "ER diagram is incomplete: selected-table column limit reached".into(),
                warnings: vec![format!(
                    "Columns of {} exceed the diagram limit",
                    qualified(&schema, &table)
                )],
                tables: vec![empty_table(&schema, &table)],
                ..Default::default()
            });
        }
        Err(failure) => return Err(failure),
    };
    let mut graph = ObjectGraph {
        tables: vec![selected],
        ..Default::default()
    };
    let mut total_columns = graph.tables[0].columns.len();
    let mut total_text_bytes = table_text_bytes(&graph.tables[0]);
    if total_columns > MAX_GRAPH_TOTAL_COLUMNS || total_text_bytes > MAX_GRAPH_TEXT_BYTES {
        unavailable(
            &mut graph,
            "ER diagram is incomplete: selected-table metadata limit reached",
            format!(
                "Columns of {} exceed the diagram budget",
                qualified(&schema, &table)
            ),
        );
        graph.tables[0].columns.clear();
        return Ok(graph);
    }
    // KEY_COLUMN_USAGE contains the paired source and referenced columns. The
    // predicates read only constraints touching the selected table, so graph
    // discovery cannot expand to a second hop.
    let mut rows: Vec<ForeignRow> = conn
        .exec(
            "SELECT CONSTRAINT_SCHEMA, TABLE_SCHEMA, TABLE_NAME, CONSTRAINT_NAME, COLUMN_NAME, \
             REFERENCED_TABLE_SCHEMA, REFERENCED_TABLE_NAME, REFERENCED_COLUMN_NAME, ORDINAL_POSITION \
             FROM information_schema.KEY_COLUMN_USAGE \
             WHERE REFERENCED_TABLE_NAME IS NOT NULL AND \
             ((TABLE_SCHEMA = ? AND TABLE_NAME = ?) OR \
              (REFERENCED_TABLE_SCHEMA = ? AND REFERENCED_TABLE_NAME = ?)) \
             ORDER BY CONSTRAINT_SCHEMA, TABLE_SCHEMA, TABLE_NAME, CONSTRAINT_NAME, ORDINAL_POSITION \
             LIMIT ?",
            (&schema, &table, &schema, &table, (MAX_GRAPH_FK_COLUMNS + 1) as u64),
        )
        .await
        .map_err(normalize)?;
    if rows.len() > MAX_GRAPH_FK_COLUMNS {
        let limited = &rows[MAX_GRAPH_FK_COLUMNS];
        let cutoff = (
            limited.0.clone(),
            limited.1.clone(),
            limited.2.clone(),
            limited.3.clone(),
        );
        let warning = format!(
            "Relationship {} on {} and later relationships exceed the diagram limit",
            quote(&limited.3),
            qualified(&limited.1, &limited.2)
        );
        rows.truncate(MAX_GRAPH_FK_COLUMNS);
        // The last retained row may be one column of a composite constraint.
        // Keep only complete constraints when the bounded query cuts through one.
        while rows.last().is_some_and(|row| {
            (&row.0, &row.1, &row.2, &row.3) == (&cutoff.0, &cutoff.1, &cutoff.2, &cutoff.3)
        }) {
            rows.pop();
        }
        unavailable(
            &mut graph,
            "ER diagram is incomplete: relationship limit reached",
            warning,
        );
    }
    let mut edges = BTreeMap::<(String, String, String, String), ObjectGraphEdge>::new();
    let mut related = BTreeSet::<(String, String)>::new();
    let mut unknown_targets = BTreeSet::<String>::new();
    for (
        constraint_schema,
        source_schema,
        source_table,
        constraint_name,
        source_column,
        target_schema,
        target_table,
        target_column,
        _position,
    ) in rows
    {
        let key = (
            constraint_schema.clone(),
            source_schema.clone(),
            source_table.clone(),
            constraint_name.clone(),
        );
        let edge = edges.entry(key).or_insert_with(|| ObjectGraphEdge {
            id: format!(
                "{}.{}.{}",
                quote(&constraint_schema),
                quote(&source_table),
                quote(&constraint_name)
            ),
            source_id: table_id(&source_schema, &source_table),
            target_id: table_id(&target_schema, &target_table),
            source_columns: Vec::new(),
            target_columns: Vec::new(),
        });
        edge.source_columns.push(source_column);
        if let Some(target_column) = target_column {
            edge.target_columns.push(target_column);
        } else {
            unknown_targets.insert(edge.id.clone());
        }
        for pair in [
            (&source_schema, &source_table),
            (&target_schema, &target_table),
        ] {
            if pair.0 != &schema || pair.1 != &table {
                related.insert((pair.0.clone(), pair.1.clone()));
            }
        }
    }
    let mut metadata_budget_exhausted = false;
    for (index, (related_schema, related_table)) in related.into_iter().enumerate() {
        if index + 1 >= MAX_GRAPH_TABLES {
            let related_id = table_id(&related_schema, &related_table);
            let missed = edges
                .values()
                .filter(|edge| edge.source_id == related_id || edge.target_id == related_id)
                .map(|edge| edge.id.as_str())
                .collect::<Vec<_>>()
                .join(", ");
            unavailable(
                &mut graph,
                "ER diagram is incomplete: related-table limit reached",
                format!(
                    "Related table {} is beyond the diagram limit; relationships {} cannot be drawn",
                    qualified(&related_schema, &related_table),
                    missed
                ),
            );
            continue;
        }
        if metadata_budget_exhausted {
            unavailable(
                &mut graph,
                "ER diagram is incomplete: aggregate metadata limit reached",
                format!(
                    "Columns of {} are unavailable after the diagram budget was reached",
                    qualified(&related_schema, &related_table)
                ),
            );
            graph
                .tables
                .push(empty_table(&related_schema, &related_table));
            continue;
        }
        match table_columns(
            conn,
            &related_schema,
            &related_table,
            MAX_GRAPH_TOTAL_COLUMNS - total_columns,
            MAX_GRAPH_TEXT_BYTES - total_text_bytes,
        )
        .await
        {
            Ok(table) => {
                let next_columns = total_columns + table.columns.len();
                let next_text_bytes = total_text_bytes + table_text_bytes(&table);
                if next_columns > MAX_GRAPH_TOTAL_COLUMNS || next_text_bytes > MAX_GRAPH_TEXT_BYTES
                {
                    metadata_budget_exhausted = true;
                    unavailable(
                        &mut graph,
                        "ER diagram is incomplete: aggregate metadata limit reached",
                        format!(
                            "Columns of {} exceed the diagram budget",
                            qualified(&related_schema, &related_table)
                        ),
                    );
                    graph
                        .tables
                        .push(empty_table(&related_schema, &related_table));
                } else {
                    total_columns = next_columns;
                    total_text_bytes = next_text_bytes;
                    graph.tables.push(table);
                }
            }
            Err(failure)
                if matches!(
                    failure.kind,
                    ErrorKind::Connection | ErrorKind::Disconnected | ErrorKind::Timeout
                ) =>
            {
                return Err(failure);
            }
            Err(failure) if failure.kind == ErrorKind::ResourceLimit => {
                metadata_budget_exhausted = true;
                unavailable(
                    &mut graph,
                    "ER diagram is incomplete: aggregate metadata limit reached",
                    format!(
                        "Columns of {} exceed the diagram budget",
                        qualified(&related_schema, &related_table)
                    ),
                );
                graph
                    .tables
                    .push(empty_table(&related_schema, &related_table));
            }
            Err(_) => {
                unavailable(
                    &mut graph,
                    "ER diagram is incomplete: a related table is unavailable",
                    format!(
                        "Related table {} is unavailable",
                        qualified(&related_schema, &related_table)
                    ),
                );
                graph
                    .tables
                    .push(empty_table(&related_schema, &related_table));
            }
        }
    }
    for mut edge in edges.into_values() {
        if unknown_targets.contains(&edge.id) {
            edge.target_columns.clear();
            unavailable(
                &mut graph,
                "ER diagram is incomplete: a foreign-key target column is unknown",
                format!("Relationship {} has an unresolved target column", edge.id),
            );
        }
        if let Some(source) = graph
            .tables
            .iter_mut()
            .find(|table| table.id == edge.source_id)
        {
            for column in &mut source.columns {
                if edge.source_columns.contains(&column.name) {
                    column.foreign_key = true;
                }
            }
        }
        graph.edges.push(edge);
    }
    Ok(graph)
}
