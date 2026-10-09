use choscordb_driver_api::{
    is_system_schema_node, navigator_object_visible, system_schemas_hidden,
};

pub fn navigator_object_visible_policy(
    driver: &str,
    show_system_schemas: bool,
    qualified_name: &str,
) -> bool {
    navigator_object_visible(driver, show_system_schemas, qualified_name)
}

pub fn system_schema_node_policy(kind: &str, name: &str) -> bool {
    is_system_schema_node(kind, name)
}

pub fn system_schemas_hidden_policy(driver: &str, show_system_schemas: bool) -> bool {
    system_schemas_hidden(driver, show_system_schemas)
}
