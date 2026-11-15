use crate::ffi;
use choscordb_sql_language as language;

pub(crate) fn mysql_mode(driver: &str, sql_mode: &str) -> Option<language::MysqlSqlMode> {
    let mariadb = driver.eq_ignore_ascii_case("mariadb");
    (mariadb || driver.eq_ignore_ascii_case("mysql")).then(|| language::MysqlSqlMode {
        mariadb,
        ..language::MysqlSqlMode::from_sql_mode(sql_mode)
    })
}

fn offset(value: u64) -> Result<usize, language::EditError> {
    usize::try_from(value).map_err(|_| language::EditError::InvalidRange)
}

fn dto(edit: Result<language::TextEdit, language::EditError>) -> ffi::SqlTextEditDto {
    match edit {
        Ok(edit) => ffi::SqlTextEditDto {
            valid: true,
            start: edit.range.start as u64,
            end: edit.range.end as u64,
            text: edit.text,
            error: String::new(),
        },
        Err(error) => ffi::SqlTextEditDto {
            error: error.to_string(),
            ..Default::default()
        },
    }
}

pub fn sql_format_edit(request: ffi::SqlFormatRequestDto) -> ffi::SqlTextEditDto {
    dto((|| {
        if !(choscordb_core::MIN_FORMAT_INDENT..=choscordb_core::MAX_FORMAT_INDENT)
            .contains(&request.indent)
        {
            return Err(language::EditError::InvalidOptions);
        }
        language::format_edit(
            &request.sql,
            offset(request.cursor)?,
            offset(request.selection_start)?..offset(request.selection_end)?,
            mysql_mode(&request.driver, &request.sql_mode),
            language::FormatOptions {
                indent: request.indent,
                uppercase_keywords: request.uppercase_keywords,
            },
            if request.minify {
                language::FormatStyle::Minify
            } else {
                language::FormatStyle::Pretty
            },
        )
    })())
}

pub fn sql_toggle_comment(
    sql: &str,
    selection_start: u64,
    selection_end: u64,
    block: bool,
) -> ffi::SqlTextEditDto {
    dto((|| {
        let selection = offset(selection_start)?..offset(selection_end)?;
        if block {
            language::toggle_block_comment(sql, selection)
        } else {
            language::toggle_line_comment(sql, selection)
        }
    })())
}

pub fn sql_apply_row_limit(
    sql: &str,
    limit: u32,
    driver: &str,
    sql_mode: &str,
) -> ffi::RowLimitDto {
    let result = language::apply_row_limit(sql, limit, mysql_mode(driver, sql_mode));
    ffi::RowLimitDto {
        sql: result.sql,
        applied: result.applied.is_some(),
        limit: result.applied.unwrap_or(0),
    }
}

pub fn row_limit_choices() -> Vec<u32> {
    language::ROW_LIMIT_CHOICES.to_vec()
}
