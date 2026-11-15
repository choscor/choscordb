use choscordb_bridge::grid_paste_plan;

#[test]
fn paste_plan_lists_each_target_cell_or_the_error() {
    let plan = grid_paste_plan("a\tb", 2, 1, 1, 1, 5, 5);
    assert_eq!(plan.error, "");
    assert_eq!(plan.rows, [2, 2]);
    assert_eq!(plan.columns, [1, 2]);
    assert_eq!(plan.texts, ["a", "b"]);
    let failed = grid_paste_plan("a\tb", 0, 4, 1, 1, 5, 5);
    assert_eq!(
        failed.error,
        "The pasted block has 1 rows and 2 columns. It does not fit from row 1, column 5."
    );
    assert!(failed.texts.is_empty());
}
