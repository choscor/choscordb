use crate::{BridgeEngine, ffi, pack, submit, unpack};
use choscordb_core::{BackupDirection, BackupRequest};

fn direction(restore: bool) -> BackupDirection {
    if restore {
        BackupDirection::Restore
    } else {
        BackupDirection::Backup
    }
}

pub fn backup_form(driver: &str, restore: bool) -> ffi::BackupFormDto {
    let direction = direction(restore);
    ffi::BackupFormDto {
        options: choscordb_core::backup_options(driver, direction)
            .iter()
            .map(|option| ffi::BackupOptionDto {
                code: option.code.into(),
                label: option.label.into(),
                default_on: option.default,
            })
            .collect(),
        unavailable: choscordb_core::backup_unavailable(driver, direction)
            .unwrap_or("")
            .to_owned(),
        uses_tool: choscordb_core::backup_uses_tool(driver),
    }
}

pub fn start_backup(
    engine: &mut BridgeEngine,
    connection: u64,
    request: &ffi::BackupRequestDto,
) -> ffi::Submit {
    let planned = BackupRequest::new(
        &request.driver,
        direction(request.restore),
        &request.file,
        &request.tool_folder,
        &request.options,
    );
    submit(engine, |e| {
        e.start_backup(unpack(connection), planned?)
            .map(pack)
            .map_err(|error| error.to_string())
    })
}

pub fn backup_presentation(
    state: ffi::ExportStateDto,
    detail: &str,
    restore: bool,
) -> ffi::PresentationDto {
    crate::screen_state::transfer_presentation(state, |state| {
        choscordb_core::export_machine::present_backup(state, detail, direction(restore))
    })
}
