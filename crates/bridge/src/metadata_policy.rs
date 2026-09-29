use choscordb_driver_api::{is_postgres_system_schema, navigator_object_visible};

pub fn navigator_object_visible_policy(
    driver: &str,
    show_system_schemas: bool,
    qualified_name: &str,
) -> bool {
    navigator_object_visible(driver, show_system_schemas, qualified_name)
}

pub fn postgres_system_schema_policy(schema: &str) -> bool {
    is_postgres_system_schema(schema)
}
