use choscordb_core::{GridRect, MAX_PASTE_CELLS, PasteCell, plan_grid_paste};

fn rect(top: u32, left: u32, rows: u32, columns: u32) -> GridRect {
    GridRect {
        top,
        left,
        rows,
        columns,
    }
}

fn cells(plan: &[PasteCell]) -> Vec<(u32, u32, &str)> {
    plan.iter()
        .map(|cell| (cell.row, cell.column, cell.text.as_str()))
        .collect()
}

#[test]
fn a_tab_block_lands_at_the_selected_cell() {
    let plan = plan_grid_paste("a\tb\r\nc\td\r\n", rect(1, 2, 1, 1), 5, 5).unwrap();
    assert_eq!(
        cells(&plan),
        [(1, 2, "a"), (1, 3, "b"), (2, 2, "c"), (2, 3, "d")]
    );
}

#[test]
fn quoted_values_keep_tabs_and_line_breaks() {
    let plan = plan_grid_paste("\"x\ty\"\t\"two\nlines\"", rect(0, 0, 1, 1), 2, 2).unwrap();
    assert_eq!(cells(&plan), [(0, 0, "x\ty"), (0, 1, "two\nlines")]);
}

#[test]
fn comma_rows_paste_as_csv_and_one_line_stays_one_value() {
    let plan = plan_grid_paste("1,ann\n2,bo\n", rect(0, 0, 1, 1), 3, 3).unwrap();
    assert_eq!(
        cells(&plan),
        [(0, 0, "1"), (0, 1, "ann"), (1, 0, "2"), (1, 1, "bo")]
    );
    let single = plan_grid_paste("Smith, Ann", rect(0, 0, 1, 1), 1, 1).unwrap();
    assert_eq!(cells(&single), [(0, 0, "Smith, Ann")]);
}

#[test]
fn one_value_fills_the_selection_and_a_block_tiles_an_exact_multiple() {
    let fill = plan_grid_paste("z", rect(1, 1, 2, 2), 4, 4).unwrap();
    assert_eq!(
        cells(&fill),
        [(1, 1, "z"), (1, 2, "z"), (2, 1, "z"), (2, 2, "z")]
    );
    let tile = plan_grid_paste("a\nb", rect(0, 0, 4, 1), 4, 1).unwrap();
    assert_eq!(
        cells(&tile),
        [(0, 0, "a"), (1, 0, "b"), (2, 0, "a"), (3, 0, "b")]
    );
    let anchored = plan_grid_paste("a\nb", rect(0, 0, 3, 1), 4, 1).unwrap();
    assert_eq!(cells(&anchored), [(0, 0, "a"), (1, 0, "b")]);
}

#[test]
fn a_block_that_does_not_fit_or_is_ragged_or_empty_fails() {
    assert_eq!(
        plan_grid_paste("a\tb\nc\td", rect(1, 1, 1, 1), 2, 3).unwrap_err(),
        "The pasted block has 2 rows and 2 columns. It does not fit from row 2, column 2."
    );
    assert_eq!(
        plan_grid_paste("a\tb\nc", rect(0, 0, 1, 1), 5, 5).unwrap_err(),
        "Each pasted row must have the same number of values."
    );
    assert_eq!(
        plan_grid_paste("", rect(0, 0, 1, 1), 5, 5).unwrap_err(),
        "The clipboard has no text to paste."
    );
}

#[test]
fn a_paste_has_a_cell_limit() {
    let rows = MAX_PASTE_CELLS as u32 + 1;
    assert_eq!(
        plan_grid_paste("v", rect(0, 0, rows, 1), rows, 1).unwrap_err(),
        format!("A paste can change at most {MAX_PASTE_CELLS} cells.")
    );
}
