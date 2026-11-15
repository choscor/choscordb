use crate::{ffi, sql_plan};
use choscordb_core::{TriggerScript, TriggerScriptRequest};

pub fn trigger_script_kinds() -> Vec<ffi::TriggerScriptKindDto> {
    TriggerScript::ALL
        .into_iter()
        .map(|kind| ffi::TriggerScriptKindDto {
            code: kind.code().into(),
            label: kind.label().into(),
            needs_trigger: kind.needs_trigger(),
        })
        .collect()
}

pub fn trigger_script(dto: &ffi::TriggerScriptDto) -> ffi::SqlPlanDto {
    let script = TriggerScript::from_code(&dto.kind)
        .ok_or_else(|| "Choose a trigger action.".to_string())
        .and_then(|kind| {
            choscordb_core::trigger_script(
                kind,
                &TriggerScriptRequest {
                    driver: dto.driver.clone(),
                    table_id: dto.table_id.clone(),
                    table_name: dto.table_name.clone(),
                    trigger: dto.trigger.clone(),
                    definition: dto.definition.clone(),
                },
            )
        });
    sql_plan(script)
}
