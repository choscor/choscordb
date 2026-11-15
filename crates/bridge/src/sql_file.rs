use crate::{BridgeEngine, code_label, ffi, pack, sql_edit::mysql_mode, submit, unpack};
use choscordb_core::{SqlFileMode, SqlFileRun};

pub fn sql_file_modes() -> Vec<ffi::CodeLabelDto> {
    SqlFileMode::ALL
        .into_iter()
        .map(|mode| code_label(mode.code(), mode.label()))
        .collect()
}

pub fn run_sql_file(
    engine: &mut BridgeEngine,
    connection: u64,
    path: &str,
    driver: &str,
    sql_mode: &str,
    mode: &str,
) -> ffi::Submit {
    let request = match (path.trim(), SqlFileMode::from_code(mode)) {
        ("", _) => Err("Choose a SQL file to run.".to_owned()),
        (_, None) => Err(format!("Unknown SQL file run mode: {mode}")),
        (path, Some(mode)) => Ok(SqlFileRun {
            file: path.into(),
            mysql: mysql_mode(driver, sql_mode),
            mode,
        }),
    };
    submit(engine, |e| {
        e.run_sql_file(unpack(connection), request?)
            .map(pack)
            .map_err(|error| error.to_string())
    })
}
