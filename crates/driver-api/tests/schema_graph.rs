use choscordb_driver_api::{
    MAX_SCHEMA_GRAPH_COLUMNS, MAX_SCHEMA_GRAPH_TABLES, MetadataAvailability, ObjectId,
    SchemaGraphBuilder,
};

#[test]
fn builder_marks_keys_and_joins_multi_column_foreign_keys() {
    let mut graph = SchemaGraphBuilder::default();
    assert!(graph.table(ObjectId("o".into()), "main.orders".into()));
    assert!(graph.table(ObjectId("c".into()), "main.customers".into()));
    graph.column("o", "id", "INTEGER", true);
    graph.column("o", "customer_id", "INTEGER", false);
    graph.column("o", "region", "TEXT", false);
    graph.column("c", "id", "INTEGER", true);
    graph.column("c", "region", "TEXT", true);
    graph.foreign_key("fk1", "o", "customer_id", "c", "id");
    graph.foreign_key("fk1", "o", "region", "c", "region");
    let graph = graph.finish();
    assert_eq!(graph.availability, MetadataAvailability::Available);
    assert_eq!(graph.edges.len(), 1);
    assert_eq!(graph.edges[0].source_columns, ["customer_id", "region"]);
    assert_eq!(graph.edges[0].target_columns, ["id", "region"]);
    let orders = &graph.tables[0];
    assert_eq!(
        orders
            .columns
            .iter()
            .map(|column| (column.name.as_str(), column.primary_key, column.foreign_key))
            .collect::<Vec<_>>(),
        [
            ("id", true, false),
            ("customer_id", false, true),
            ("region", false, true)
        ]
    );
}

#[test]
fn builder_reports_tables_beyond_the_limit_and_outside_targets() {
    let mut graph = SchemaGraphBuilder::default();
    for index in 0..MAX_SCHEMA_GRAPH_TABLES {
        assert!(graph.table(ObjectId(format!("t{index}")), format!("t{index}")));
    }
    assert!(!graph.table(ObjectId("extra".into()), "extra".into()));
    graph.foreign_key("fk", "t0", "a", "other_schema_table", "b");
    let graph = graph.finish();
    assert_eq!(graph.tables.len(), MAX_SCHEMA_GRAPH_TABLES);
    assert_eq!(graph.availability, MetadataAvailability::Unavailable);
    assert_eq!(
        graph.reason,
        format!("The diagram shows the first {MAX_SCHEMA_GRAPH_TABLES} tables.")
    );
    assert_eq!(
        graph.warnings,
        ["1 foreign key points to a table outside the diagram."]
    );
    assert!(graph.edges.is_empty());
}

#[test]
fn builder_stops_adding_columns_at_the_total_limit() {
    let mut graph = SchemaGraphBuilder::default();
    graph.table(ObjectId("t".into()), "t".into());
    for index in 0..=MAX_SCHEMA_GRAPH_COLUMNS {
        graph.column("t", &format!("c{index}"), "INT", false);
    }
    let graph = graph.finish();
    assert_eq!(graph.tables[0].columns.len(), MAX_SCHEMA_GRAPH_COLUMNS);
    assert_eq!(
        graph.warnings,
        [format!(
            "The diagram shows the first {MAX_SCHEMA_GRAPH_COLUMNS} columns."
        )]
    );
}
