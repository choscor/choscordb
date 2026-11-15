pub const MAX_PASTE_CELLS: usize = 100_000;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct GridRect {
    pub top: u32,
    pub left: u32,
    pub rows: u32,
    pub columns: u32,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PasteCell {
    pub row: u32,
    pub column: u32,
    pub text: String,
}

fn delimited(text: &str, delimiter: u8) -> Result<Vec<Vec<String>>, String> {
    csv::ReaderBuilder::new()
        .has_headers(false)
        .flexible(true)
        .delimiter(delimiter)
        .from_reader(text.as_bytes())
        .records()
        .map(|record| {
            record
                .map(|record| record.iter().map(str::to_owned).collect())
                .map_err(|error| format!("The pasted text cannot be read: {error}"))
        })
        .collect()
}

fn paste_block(text: &str) -> Result<Vec<Vec<String>>, String> {
    let text = text
        .strip_suffix("\r\n")
        .or_else(|| text.strip_suffix('\n'))
        .unwrap_or(text);
    if text.is_empty() {
        return Err("The clipboard has no text to paste.".into());
    }
    let block = if text.contains('\t') {
        delimited(text, b'\t')?
    } else {
        let csv = if text.contains('\n') {
            delimited(text, b',')?
        } else {
            Vec::new()
        };
        let width = csv.first().map_or(0, Vec::len);
        if width > 1 && csv.iter().all(|row| row.len() == width) {
            csv
        } else {
            text.split('\n')
                .map(|line| vec![line.strip_suffix('\r').unwrap_or(line).to_owned()])
                .collect()
        }
    };
    let width = block.first().map_or(0, Vec::len);
    if block.iter().any(|row| row.len() != width) {
        return Err("Each pasted row must have the same number of values.".into());
    }
    Ok(block)
}

pub fn plan_grid_paste(
    text: &str,
    selection: GridRect,
    grid_rows: u32,
    grid_columns: u32,
) -> Result<Vec<PasteCell>, String> {
    let block = paste_block(text)?;
    let block_rows = u32::try_from(block.len()).map_err(|_| too_many())?;
    let block_columns = u32::try_from(block[0].len()).map_err(|_| too_many())?;
    let tiles = selection.rows > 0
        && selection.columns > 0
        && selection.rows.is_multiple_of(block_rows)
        && selection.columns.is_multiple_of(block_columns);
    let (rows, columns) = if tiles {
        (selection.rows, selection.columns)
    } else {
        (block_rows, block_columns)
    };
    if u64::from(selection.top) + u64::from(rows) > u64::from(grid_rows)
        || u64::from(selection.left) + u64::from(columns) > u64::from(grid_columns)
    {
        return Err(format!(
            "The pasted block has {block_rows} rows and {block_columns} columns. \
             It does not fit from row {}, column {}.",
            u64::from(selection.top) + 1,
            u64::from(selection.left) + 1
        ));
    }
    if u64::from(rows) * u64::from(columns) > MAX_PASTE_CELLS as u64 {
        return Err(too_many());
    }
    let mut cells = Vec::with_capacity((rows * columns) as usize);
    for row in 0..rows {
        for column in 0..columns {
            cells.push(PasteCell {
                row: selection.top + row,
                column: selection.left + column,
                text: block[(row % block_rows) as usize][(column % block_columns) as usize].clone(),
            });
        }
    }
    Ok(cells)
}

fn too_many() -> String {
    format!("A paste can change at most {MAX_PASTE_CELLS} cells.")
}
