use choscordb_driver_api::{
    DriverError, ErrorKind, MAX_SCHEMA_GRAPH_TABLES, MetadataAvailability, ObjectGraph,
    ObjectGraphColumn, ObjectGraphEdge, ObjectGraphTable, ObjectId, Result, SchemaGraphBuilder,
};
use std::collections::{BTreeSet, HashMap};
use tokio_postgres::GenericClient;

const MAX_EDGES: usize = 128;
const MAX_COLUMNS: usize = 4096;
const MAX_TEXT: usize = 1024 * 1024;

fn incomplete(graph: &mut ObjectGraph, reason: String) {
    graph.availability = MetadataAvailability::Unavailable;
    graph.warnings.push(reason.clone());
    if graph.reason.is_empty() {
        graph.reason = reason;
    }
}

fn quoted(name: &str) -> String {
    format!("\"{}\"", name.replace('"', "\"\""))
}

async fn schema_graph<C: GenericClient + Sync>(
    client: &C,
    schema: Option<u32>,
) -> Result<ObjectGraph> {
    let limit = i64::try_from(MAX_SCHEMA_GRAPH_TABLES + 1).map_err(|_| {
        DriverError::new(
            ErrorKind::ResourceLimit,
            "The diagram table limit is too large",
        )
    })?;
    let tables = client
        .query(
            "SELECT c.oid, n.nspname::text, c.relname::text FROM pg_catalog.pg_class c JOIN pg_catalog.pg_namespace n ON n.oid=c.relnamespace WHERE (c.relnamespace=$1 OR ($1 IS NULL AND n.nspname NOT LIKE 'pg\\_%' AND n.nspname <> 'information_schema')) AND c.relkind IN ('r','p') AND NOT c.relispartition ORDER BY n.nspname, c.relname LIMIT $2",
            &[&schema, &limit],
        )
        .await
        .map_err(crate::normalize)?;
    let mut graph = SchemaGraphBuilder::default();
    let mut kept = Vec::new();
    for row in &tables {
        let oid: u32 = row.get(0);
        let name = format!("{}.{}", quoted(row.get(1)), quoted(row.get(2)));
        if !graph.table(ObjectId(format!("pg:relation:{oid}")), name) {
            break;
        }
        kept.push(oid);
    }
    let columns = client
        .query(
            "SELECT a.attrelid, a.attname::text, pg_catalog.format_type(a.atttypid,a.atttypmod), EXISTS (SELECT 1 FROM pg_catalog.pg_constraint p WHERE p.conrelid=a.attrelid AND p.contype='p' AND a.attnum=ANY(p.conkey)) FROM pg_catalog.pg_attribute a WHERE a.attrelid=ANY($1) AND a.attnum>0 AND NOT a.attisdropped ORDER BY a.attrelid, a.attnum",
            &[&kept],
        )
        .await
        .map_err(crate::normalize)?;
    for row in columns {
        let table: u32 = row.get(0);
        graph.column(
            &format!("pg:relation:{table}"),
            row.get(1),
            row.get(2),
            row.get(3),
        );
    }
    let keys = client
        .query(
            "SELECT k.oid, k.conrelid, k.confrelid, s.attname::text, t.attname::text FROM pg_catalog.pg_constraint k CROSS JOIN LATERAL unnest(k.conkey, k.confkey) WITH ORDINALITY AS u(source_number, target_number, position) JOIN pg_catalog.pg_attribute s ON s.attrelid=k.conrelid AND s.attnum=u.source_number JOIN pg_catalog.pg_attribute t ON t.attrelid=k.confrelid AND t.attnum=u.target_number WHERE k.contype='f' AND k.conrelid=ANY($1) ORDER BY k.oid, u.position",
            &[&kept],
        )
        .await
        .map_err(crate::normalize)?;
    for row in keys {
        let constraint: u32 = row.get(0);
        let source: u32 = row.get(1);
        let target: u32 = row.get(2);
        graph.foreign_key(
            &format!("pg:constraint:{constraint}"),
            &format!("pg:relation:{source}"),
            row.get(3),
            &format!("pg:relation:{target}"),
            row.get(4),
        );
    }
    Ok(graph.finish())
}

pub(crate) async fn load_object_graph<C: GenericClient + Sync>(
    client: &C,
    object: &ObjectId,
) -> Result<ObjectGraph> {
    for (prefix, whole_database) in [("pg:schema:", false), ("pg:database:", true)] {
        let Some(raw) = object.0.strip_prefix(prefix) else {
            continue;
        };
        let oid = raw
            .parse::<u32>()
            .ok()
            .filter(|oid| *oid != 0 && raw == oid.to_string())
            .ok_or_else(|| {
                DriverError::new(
                    ErrorKind::InvalidInput,
                    "Invalid PostgreSQL schema identifier",
                )
            })?;
        return schema_graph(client, (!whole_database).then_some(oid)).await;
    }
    let Some(raw) = object.0.strip_prefix("pg:relation:") else {
        return Err(DriverError::new(
            ErrorKind::InvalidInput,
            "ERD requires a PostgreSQL table",
        ));
    };
    let oid = raw.parse::<u32>().map_err(|_| {
        DriverError::new(
            ErrorKind::InvalidInput,
            "Invalid PostgreSQL table identifier",
        )
    })?;
    if oid == 0 || raw != oid.to_string() {
        return Err(DriverError::new(
            ErrorKind::InvalidInput,
            "Invalid PostgreSQL table identifier",
        ));
    }
    let selected = client
        .query_opt(
            "SELECT relkind::text FROM pg_catalog.pg_class WHERE oid=$1",
            &[&oid],
        )
        .await
        .map_err(crate::normalize)?;
    if !selected.is_some_and(|r| matches!(r.get::<_, &str>(0), "r" | "p")) {
        return Err(DriverError::new(
            ErrorKind::InvalidInput,
            "ERD requires an available PostgreSQL table",
        ));
    }
    let mut graph = ObjectGraph::default();
    let rows = client.query("SELECT oid, conrelid, confrelid, conkey, confkey FROM pg_catalog.pg_constraint WHERE contype='f' AND (conrelid=$1 OR confrelid=$1) ORDER BY oid LIMIT 129", &[&oid]).await.map_err(crate::normalize)?;
    if rows.len() > MAX_EDGES {
        let first_omitted: u32 = rows[MAX_EDGES].get(0);
        incomplete(
            &mut graph,
            format!(
                "More than {MAX_EDGES} direct foreign keys; diagram is incomplete starting at pg:constraint:{first_omitted}"
            ),
        );
    }
    let mut ids = BTreeSet::from([oid]);
    let mut constraints = Vec::new();
    for row in rows.into_iter().take(MAX_EDGES) {
        let constraint_oid: u32 = row.get(0);
        let source: u32 = row.get(1);
        let target: u32 = row.get(2);
        let from: Vec<i16> = row.get(3);
        let to: Vec<i16> = row.get(4);
        ids.insert(source);
        ids.insert(target);
        constraints.push((constraint_oid, source, target, from, to));
    }
    let ids: Vec<u32> = std::iter::once(oid)
        .chain(ids.into_iter().filter(|id| *id != oid))
        .collect();
    let mut columns = HashMap::<(u32, i16), String>::new();
    let mut text_bytes = 0usize;
    for table_oid in ids {
        let id = ObjectId(format!("pg:relation:{table_oid}"));
        let relation = client.query_opt("SELECT n.nspname::text, c.relname::text FROM pg_catalog.pg_class c JOIN pg_catalog.pg_namespace n ON n.oid=c.relnamespace WHERE c.oid=$1 AND c.relkind IN ('r','p')", &[&table_oid]).await.map_err(crate::normalize)?;
        let Some(relation) = relation else {
            incomplete(&mut graph, format!("Related table {id:?} is unavailable"));
            graph.tables.push(ObjectGraphTable {
                id,
                qualified_name: format!("Unavailable table {table_oid}"),
                columns: Vec::new(),
            });
            continue;
        };
        let schema: String = relation.get(0);
        let name: String = relation.get(1);
        let qualified_name = format!(
            "\"{}\".\"{}\"",
            schema.replace('"', "\"\""),
            name.replace('"', "\"\"")
        );
        text_bytes = text_bytes.saturating_add(qualified_name.len());
        if graph.tables.iter().map(|t| t.columns.len()).sum::<usize>() >= MAX_COLUMNS
            || text_bytes >= MAX_TEXT
        {
            incomplete(
                &mut graph,
                format!("Diagram metadata limit reached at {}", qualified_name),
            );
            graph.tables.push(ObjectGraphTable {
                id,
                qualified_name,
                columns: Vec::new(),
            });
            continue;
        }
        let rows = match client.query("SELECT a.attnum, a.attname::text, pg_catalog.format_type(a.atttypid,a.atttypmod), \
            EXISTS (SELECT 1 FROM pg_catalog.pg_constraint p WHERE p.conrelid=$1 AND p.contype='p' AND a.attnum=ANY(p.conkey)), \
            EXISTS (SELECT 1 FROM pg_catalog.pg_constraint f WHERE f.conrelid=$1 AND f.contype='f' AND a.attnum=ANY(f.conkey)) \
            FROM pg_catalog.pg_attribute a WHERE a.attrelid=$1 AND a.attnum>0 AND NOT a.attisdropped ORDER BY a.attnum LIMIT 4097", &[&table_oid]).await {
            Ok(rows) => rows,
            Err(error) if table_oid != oid => {
                incomplete(&mut graph, format!("Columns for {qualified_name} are unavailable: {}", error));
                graph.tables.push(ObjectGraphTable { id, qualified_name, columns: Vec::new() });
                continue;
            }
            Err(error) => return Err(crate::normalize(error)),
        };
        let mut table = ObjectGraphTable {
            id,
            qualified_name,
            columns: Vec::new(),
        };
        if rows.len() > MAX_COLUMNS
            || graph.tables.iter().map(|t| t.columns.len()).sum::<usize>() + rows.len()
                > MAX_COLUMNS
        {
            incomplete(
                &mut graph,
                format!("Column limit reached at {}", table.qualified_name),
            );
        } else {
            for row in rows {
                let attnum: i16 = row.get(0);
                let column = ObjectGraphColumn {
                    name: row.get(1),
                    database_type: row.get(2),
                    primary_key: row.get(3),
                    foreign_key: row.get(4),
                };
                let next_bytes =
                    text_bytes.saturating_add(column.name.len() + column.database_type.len());
                if next_bytes > MAX_TEXT {
                    incomplete(
                        &mut graph,
                        format!("Diagram metadata limit reached at {}", table.qualified_name),
                    );
                    break;
                }
                text_bytes = next_bytes;
                columns.insert((table_oid, attnum), column.name.clone());
                table.columns.push(column);
            }
        }
        graph.tables.push(table);
    }
    for (constraint_oid, source, target, from, to) in constraints {
        let expected = from.len();
        let mapped_target = to.len();
        let mut source_columns = Vec::new();
        let mut target_columns = Vec::new();
        for attnum in from {
            if let Some(name) = columns.get(&(source, attnum)) {
                source_columns.push(name.clone());
            }
        }
        for attnum in to {
            if let Some(name) = columns.get(&(target, attnum)) {
                target_columns.push(name.clone());
            }
        }
        if expected != mapped_target
            || source_columns.len() != expected
            || target_columns.len() != mapped_target
            || source_columns.is_empty()
        {
            incomplete(
                &mut graph,
                format!(
                    "Foreign key pg:constraint:{constraint_oid} has an unavailable column endpoint"
                ),
            );
            source_columns.clear();
            target_columns.clear();
        }
        graph.edges.push(ObjectGraphEdge {
            id: format!("pg:constraint:{constraint_oid}"),
            source_id: ObjectId(format!("pg:relation:{source}")),
            target_id: ObjectId(format!("pg:relation:{target}")),
            source_columns,
            target_columns,
        });
    }
    Ok(graph)
}

#[cfg(test)]
mod live_tests {
    use super::*;

    #[test]
    fn one_hop_graph_preserves_composite_parallel_and_self_fks() {
        let Ok(url) = std::env::var("CHOSCORDB_TEST_POSTGRES") else {
            return;
        };
        tokio::runtime::Runtime::new().unwrap().block_on(async {
            let (client, connection) = tokio_postgres::connect(&url, tokio_postgres::NoTls)
                .await.unwrap();
            let task = tokio::spawn(connection);
            client.batch_execute("BEGIN; CREATE SCHEMA erd_graph_a; CREATE SCHEMA erd_graph_b; \
                CREATE TABLE erd_graph_b.parent (id int PRIMARY KEY, code int, note text, UNIQUE(id,code)); \
                CREATE TABLE erd_graph_a.focus (id int PRIMARY KEY, parent_id int, parent_code int, alt_id int, self_id int, note text, \
                  FOREIGN KEY(parent_id,parent_code) REFERENCES erd_graph_b.parent(id,code), \
                  FOREIGN KEY(alt_id) REFERENCES erd_graph_b.parent(id), \
                  FOREIGN KEY(self_id) REFERENCES erd_graph_a.focus(id)); \
                CREATE TABLE erd_graph_b.child (id int PRIMARY KEY, focus_id int REFERENCES erd_graph_a.focus(id)); \
                CREATE TABLE erd_graph_b.grandchild (id int PRIMARY KEY, child_id int REFERENCES erd_graph_b.child(id));")
                .await.unwrap();
            let oid: u32 = client.query_one("SELECT 'erd_graph_a.focus'::regclass::oid", &[]).await.unwrap().get(0);
            let graph = load_object_graph(&client, &ObjectId(format!("pg:relation:{oid}"))).await.unwrap();
            assert_eq!(graph.tables.len(), 3);
            assert_eq!(graph.edges.len(), 4);
            assert!(graph.tables.iter().any(|t| t.qualified_name == "\"erd_graph_a\".\"focus\"" && t.columns.len() == 6 && t.columns.iter().any(|c| c.name == "parent_id" && c.foreign_key)));
            assert!(!graph.tables.iter().any(|t| t.qualified_name.contains("grandchild")));
            assert!(graph.edges.iter().any(|e| e.source_columns == ["parent_id", "parent_code"] && e.target_columns == ["id", "code"]));
            assert!(graph.edges.iter().any(|e| e.source_columns == ["alt_id"] && e.target_columns == ["id"]));
            assert!(graph.edges.iter().any(|e| e.source_columns == ["self_id"] && e.target_columns == ["id"] && e.source_id == e.target_id));
            assert!(graph.edges.iter().any(|e| e.source_columns == ["focus_id"] && e.target_columns == ["id"]));
            client.batch_execute("ROLLBACK").await.unwrap();
            drop(client);
            task.await.unwrap().unwrap();
        });
    }
}
