use choscordb_bridge::{ffi, result_sort_toggle};

fn key(column: u32, direction: &str) -> ffi::ResultSortDto {
    ffi::ResultSortDto {
        column,
        direction: direction.into(),
    }
}
fn keys(value: &ffi::ResultSortToggleDto) -> Vec<(u32, String)> {
    value
        .sorts
        .iter()
        .map(|sort| (sort.column, sort.direction.clone()))
        .collect()
}

#[test]
fn toggles_return_the_next_sort_list_or_a_typed_error() {
    let next = result_sort_toggle(vec![key(1, "ascending")], 4, true);
    assert!(next.error.is_empty());
    assert_eq!(
        keys(&next),
        [(1, "ascending".to_string()), (4, "ascending".to_string())]
    );
    let plain = result_sort_toggle(vec![key(1, "ascending"), key(4, "ascending")], 1, false);
    assert_eq!(keys(&plain), [(1, "descending".to_string())]);
    let invalid = result_sort_toggle(vec![key(1, "sideways")], 1, false);
    assert_eq!(invalid.error, "Invalid sort direction");
    assert_eq!(keys(&invalid), [(1, "sideways".to_string())]);
    let full = (0..4).map(|column| key(column, "ascending")).collect();
    let rejected = result_sort_toggle(full, 9, true);
    assert_eq!(rejected.error, "Too many sort columns");
    assert_eq!(rejected.sorts.len(), 4);
}
