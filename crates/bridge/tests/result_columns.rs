use choscordb_bridge::{result_column_toggle, result_columns_context};

fn names(values: &[&str]) -> Vec<String> {
    values.iter().map(|value| value.to_string()).collect()
}

#[test]
fn column_toggle_returns_the_next_hidden_list_or_an_error() {
    let columns = names(&["id", "name"]);
    let hidden = result_column_toggle(vec![], "name", true, columns.clone());
    assert!(hidden.error.is_empty());
    assert_eq!(hidden.hidden, names(&["name"]));
    let rejected = result_column_toggle(names(&["name"]), "id", true, columns);
    assert_eq!(rejected.error, "The result must show at least one column");
    assert_eq!(rejected.hidden, names(&["name"]));
}

#[test]
fn contexts_differ_per_profile_and_object() {
    let first = result_columns_context("a", "orders");
    assert_ne!(first, result_columns_context("b", "orders"));
    assert_ne!(first, result_columns_context("a", "users"));
    assert_eq!(first, result_columns_context("a", "orders"));
    assert!(result_columns_context("", "orders").is_empty());
}
