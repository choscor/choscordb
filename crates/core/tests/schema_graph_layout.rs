use choscordb_core::{GraphCell, schema_graph_layout};
use choscordb_driver_api::{ObjectGraph, ObjectGraphEdge, ObjectGraphTable, ObjectId};

fn graph(tables: &[&str], edges: &[(&str, &str)]) -> ObjectGraph {
    ObjectGraph {
        tables: tables
            .iter()
            .map(|name| ObjectGraphTable {
                id: ObjectId((*name).into()),
                qualified_name: (*name).into(),
                columns: Vec::new(),
            })
            .collect(),
        edges: edges
            .iter()
            .map(|(source, target)| ObjectGraphEdge {
                id: format!("{source}->{target}"),
                source_id: ObjectId((*source).into()),
                target_id: ObjectId((*target).into()),
                source_columns: Vec::new(),
                target_columns: Vec::new(),
            })
            .collect(),
        ..ObjectGraph::default()
    }
}

fn cell(column: u32, row: u32, group: Option<u32>) -> GraphCell {
    GraphCell { column, row, group }
}

#[test]
fn related_tables_form_layers_and_singletons_fill_the_next_shelf() {
    let layout = schema_graph_layout(&graph(
        &["tree", "orders", "notes", "items", "customers", "tags"],
        &[
            ("orders", "customers"),
            ("items", "orders"),
            ("tree", "tree"),
            ("items", "missing"),
        ],
    ));
    assert_eq!(
        layout,
        [
            cell(2, 1, None),
            cell(1, 0, Some(0)),
            cell(0, 1, None),
            cell(2, 0, Some(0)),
            cell(0, 0, Some(0)),
            cell(1, 1, None),
        ]
    );
}

#[test]
fn shared_targets_stack_rows_and_cycles_still_get_layers() {
    let layout = schema_graph_layout(&graph(
        &["x", "p", "y", "a", "b"],
        &[("x", "p"), ("y", "p"), ("a", "b"), ("b", "a")],
    ));
    assert_eq!(
        layout,
        [
            cell(1, 0, Some(0)),
            cell(0, 0, Some(0)),
            cell(1, 1, Some(0)),
            cell(1, 2, Some(1)),
            cell(0, 2, Some(1)),
        ]
    );
}
