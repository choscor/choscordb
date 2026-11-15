use crate::{
    MetadataAvailability, ObjectGraph, ObjectGraphColumn, ObjectGraphEdge, ObjectGraphTable,
    ObjectId,
};
use std::collections::{BTreeMap, HashMap};

pub const MAX_SCHEMA_GRAPH_TABLES: usize = 500;
pub const MAX_SCHEMA_GRAPH_COLUMNS: usize = 20_000;

#[derive(Default)]
pub struct SchemaGraphBuilder {
    tables: Vec<ObjectGraphTable>,
    index: HashMap<String, usize>,
    edges: BTreeMap<String, ObjectGraphEdge>,
    columns: usize,
    tables_cut: bool,
    columns_cut: bool,
}

impl SchemaGraphBuilder {
    pub fn table(&mut self, id: ObjectId, qualified_name: String) -> bool {
        if self.tables.len() >= MAX_SCHEMA_GRAPH_TABLES {
            self.tables_cut = true;
            return false;
        }
        self.index.insert(id.0.clone(), self.tables.len());
        self.tables.push(ObjectGraphTable {
            id,
            qualified_name,
            columns: Vec::new(),
        });
        true
    }

    pub fn column(&mut self, table: &str, name: &str, database_type: &str, primary_key: bool) {
        let Some(&position) = self.index.get(table) else {
            return;
        };
        if self.columns >= MAX_SCHEMA_GRAPH_COLUMNS {
            self.columns_cut = true;
            return;
        }
        self.columns += 1;
        self.tables[position].columns.push(ObjectGraphColumn {
            name: name.into(),
            database_type: database_type.into(),
            primary_key,
            foreign_key: false,
        });
    }

    pub fn foreign_key(
        &mut self,
        constraint: &str,
        source_table: &str,
        source_column: &str,
        target_table: &str,
        target_column: &str,
    ) {
        let edge = self
            .edges
            .entry(format!("{source_table}\u{0}{constraint}"))
            .or_insert_with(|| ObjectGraphEdge {
                id: constraint.into(),
                source_id: ObjectId(source_table.into()),
                target_id: ObjectId(target_table.into()),
                source_columns: Vec::new(),
                target_columns: Vec::new(),
            });
        edge.source_columns.push(source_column.into());
        edge.target_columns.push(target_column.into());
    }

    pub fn finish(mut self) -> ObjectGraph {
        let mut graph = ObjectGraph::default();
        let mut outside = 0usize;
        for edge in std::mem::take(&mut self.edges).into_values() {
            let (Some(&source), true) = (
                self.index.get(&edge.source_id.0),
                self.index.contains_key(&edge.target_id.0),
            ) else {
                outside += 1;
                continue;
            };
            for column in &mut self.tables[source].columns {
                if edge.source_columns.contains(&column.name) {
                    column.foreign_key = true;
                }
            }
            graph.edges.push(edge);
        }
        if self.tables_cut {
            graph.availability = MetadataAvailability::Unavailable;
            graph.reason = format!("The diagram shows the first {MAX_SCHEMA_GRAPH_TABLES} tables.");
        }
        if self.columns_cut {
            graph.availability = MetadataAvailability::Unavailable;
            graph.warnings.push(format!(
                "The diagram shows the first {MAX_SCHEMA_GRAPH_COLUMNS} columns."
            ));
        }
        if outside > 0 {
            graph.warnings.push(if outside == 1 {
                "1 foreign key points to a table outside the diagram.".into()
            } else {
                format!("{outside} foreign keys point to tables outside the diagram.")
            });
        }
        graph.tables = self.tables;
        graph
    }
}
