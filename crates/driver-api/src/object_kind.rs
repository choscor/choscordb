//! Navigator object kinds and what the desktop may offer for each.

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct ObjectKindTraits {
    /// Opens an object tab (and its Details/Data panes).
    pub opens_object_tab: bool,
    /// Offers "Show DDL".
    pub ddl: bool,
    /// A table or view: drop/rename actions and SQL templates apply.
    pub relation: bool,
    /// Can hold navigator children (pinned rows may expand).
    pub container: bool,
    pub pinnable: bool,
    /// Selection resolves to this object or its owning connection/schema.
    pub navigation_anchor: bool,
    /// Sidebar searches look inside this node's children.
    pub search_descends: bool,
    /// SQL completion offers this object's name.
    pub completion_candidate: bool,
    /// The owning object's pane that shows this kind.
    pub detail_pane: Option<u8>,
    /// The navigator root that represents an open connection.
    pub connection: bool,
    /// A relation column; SQL templates list these children.
    pub column: bool,
    /// The object tab offers the ER diagram pane.
    pub diagram: bool,
    /// The pane an object tab opens on when its object is selected.
    pub initial_pane: Option<u8>,
    /// The same object may be listed under several parents, such as an index
    /// shown under both its table and its schema.
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

/// Whether a child of `parent_kind` appears in the sidebar tree. A relation's
/// indexes and keys appear only in its object tab.
pub fn sidebar_child_visible(parent_kind: &str, kind: &str) -> bool {
    !object_kind_traits(parent_kind).relation
        || matches!(kind, "column" | "loading" | "error" | "load_more")
}

/// Work bounds for navigator searches, so a keystroke never walks or loads an
/// unbounded catalog.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct NavigatorSearchBudget {
    /// Nodes one quick-open pass may visit, metadata pages it may request, and
    /// results it returns.
    pub quick_visits: u32,
    pub quick_requests: u32,
    pub quick_results: u32,
    /// Nodes and metadata pages one sidebar filter pass may use.
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
