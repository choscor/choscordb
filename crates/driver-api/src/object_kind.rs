#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct ObjectKindTraits {
    pub opens_object_tab: bool,
    pub ddl: bool,
    pub relation: bool,
    pub object_actions: bool,
    pub container: bool,
    pub pinnable: bool,
    pub navigation_anchor: bool,
    pub search_descends: bool,
    pub completion_candidate: bool,
    pub detail_pane: Option<u8>,
    pub connection: bool,
    pub column: bool,
    pub diagram: bool,
    pub initial_pane: Option<u8>,
    pub repeats_across_parents: bool,
}

fn key(kind: &str) -> bool {
    kind.ends_with("key")
}

pub fn object_kind_traits(kind: &str) -> ObjectKindTraits {
    let relation = matches!(kind, "table" | "view");
    let inspectable = relation || matches!(kind, "index" | "sequence" | "function");
    ObjectKindTraits {
        opens_object_tab: inspectable,
        ddl: inspectable,
        relation,
        object_actions: relation || kind == "sequence",
        container: relation || kind == "schema",
        pinnable: inspectable
            || matches!(
                kind,
                "schema" | "column" | "primarykey" | "foreignkey" | "uniquekey"
            ),
        navigation_anchor: inspectable || matches!(kind, "schema" | "connection"),
        completion_candidate: relation || matches!(kind, "database" | "schema" | "column"),
        search_descends: relation || matches!(kind, "connection" | "database" | "schema" | "group"),
        detail_pane: match kind {
            "column" => Some(0),
            "index" => Some(1),
            "ddl" => Some(3),
            "data" | "table" => Some(5),
            _ if key(kind) => Some(2),
            _ => None,
        },
        connection: kind == "connection",
        column: kind == "column",
        diagram: kind == "table",
        initial_pane: (kind == "table").then_some(5),
        repeats_across_parents: kind == "index",
    }
}

pub fn sidebar_child_visible(parent_kind: &str, kind: &str) -> bool {
    !object_kind_traits(parent_kind).relation
        || matches!(kind, "column" | "loading" | "error" | "load_more")
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct NavigatorSearchBudget {
    pub quick_visits: u32,
    pub quick_requests: u32,
    pub quick_results: u32,
    pub filter_visits: u32,
    pub filter_requests: u32,
}

pub const NAVIGATOR_SEARCH_BUDGET: NavigatorSearchBudget = NavigatorSearchBudget {
    quick_visits: 10_000,
    quick_requests: 96,
    quick_results: 100,
    filter_visits: 20_000,
    filter_requests: 256,
};
