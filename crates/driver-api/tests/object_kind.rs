use choscordb_driver_api::{NAVIGATOR_SEARCH_BUDGET, object_kind_traits, sidebar_child_visible};

#[test]
fn inspectable_objects_open_tabs_and_show_ddl() {
    for kind in ["table", "view", "index", "sequence", "function"] {
        let traits = object_kind_traits(kind);
        assert!(traits.opens_object_tab && traits.ddl, "{kind}");
        assert!(traits.pinnable, "{kind}");
    }
    for kind in ["schema", "column", "primarykey", "connection", "loading"] {
        let traits = object_kind_traits(kind);
        assert!(!traits.opens_object_tab && !traits.ddl, "{kind}");
    }
}

#[test]
fn only_relations_take_actions_and_templates() {
    for kind in ["table", "view"] {
        let traits = object_kind_traits(kind);
        assert!(traits.relation && traits.container, "{kind}");
    }
    assert!(object_kind_traits("schema").container);
    assert!(!object_kind_traits("schema").relation);
    assert!(!object_kind_traits("index").relation);
    assert!(!object_kind_traits("column").container);
}

#[test]
fn detail_kinds_select_their_object_pane() {
    let pane = |kind| object_kind_traits(kind).detail_pane;
    assert_eq!(pane("column"), Some(0));
    assert_eq!(pane("index"), Some(1));
    assert_eq!(pane("primarykey"), Some(2));
    assert_eq!(pane("foreignkey"), Some(2));
    assert_eq!(pane("ddl"), Some(3));
    assert_eq!(pane("data"), Some(5));
    assert_eq!(pane("table"), Some(5));
    assert_eq!(pane("view"), None);
}

#[test]
fn pinnable_and_navigation_anchor_kinds() {
    for kind in ["schema", "column", "primarykey", "foreignkey", "uniquekey"] {
        assert!(object_kind_traits(kind).pinnable, "{kind}");
    }
    assert!(!object_kind_traits("connection").pinnable);
    assert!(object_kind_traits("schema").navigation_anchor);
    assert!(object_kind_traits("connection").navigation_anchor);
    assert!(object_kind_traits("table").navigation_anchor);
    assert!(!object_kind_traits("column").navigation_anchor);
}

#[test]
fn relation_children_show_only_columns_and_placeholders_in_the_sidebar() {
    for kind in ["column", "loading", "error", "load_more"] {
        assert!(sidebar_child_visible("table", kind), "{kind}");
        assert!(sidebar_child_visible("view", kind), "{kind}");
    }
    for kind in ["index", "primarykey", "foreignkey"] {
        assert!(!sidebar_child_visible("table", kind), "{kind}");
    }
    assert!(sidebar_child_visible("schema", "index"));
    assert!(sidebar_child_visible("", "connection"));
}

#[test]
fn sidebar_search_descends_only_into_containers() {
    for kind in ["connection", "database", "schema", "group", "table", "view"] {
        assert!(object_kind_traits(kind).search_descends, "{kind}");
    }
    for kind in ["column", "index", "primarykey", "function", "loading"] {
        assert!(!object_kind_traits(kind).search_descends, "{kind}");
    }
}

#[test]
fn navigator_searches_are_bounded_below_a_large_catalog() {
    let budget = NAVIGATOR_SEARCH_BUDGET;
    assert_eq!(
        (
            budget.quick_visits,
            budget.quick_requests,
            budget.quick_results
        ),
        (10_000, 96, 100)
    );
    assert_eq!(
        (budget.filter_visits, budget.filter_requests),
        (20_000, 256)
    );
}

#[test]
fn completion_offers_containers_relations_and_columns() {
    for kind in ["database", "schema", "table", "view", "column"] {
        assert!(object_kind_traits(kind).completion_candidate, "{kind}");
    }
    for kind in ["index", "function", "primarykey", "connection", "load_more"] {
        assert!(!object_kind_traits(kind).completion_candidate, "{kind}");
    }
}

#[test]
fn navigator_structure_kinds_are_named_by_traits() {
    assert!(object_kind_traits("connection").connection);
    assert!(!object_kind_traits("schema").connection);
    assert!(object_kind_traits("column").column);
    assert!(!object_kind_traits("primarykey").column);
    assert!(object_kind_traits("index").repeats_across_parents);
    assert!(!object_kind_traits("table").repeats_across_parents);
}

#[test]
fn only_tables_open_on_data_and_offer_diagrams() {
    let table = object_kind_traits("table");
    assert!(table.diagram);
    assert_eq!(table.initial_pane, Some(5));
    for kind in ["view", "index", "schema", "column"] {
        let traits = object_kind_traits(kind);
        assert!(!traits.diagram, "{kind}");
        assert_eq!(traits.initial_pane, None, "{kind}");
    }
}
