use crate::{BridgeEngine, ffi, sql_plan, submit, unpack};
use choscordb_core::{DatabaseActionKind, DatabaseActionRequest, plan_database_action};

pub fn database_action_kinds() -> Vec<ffi::DatabaseActionKindDto> {
    DatabaseActionKind::ALL
        .into_iter()
        .map(|kind| {
            let fields = kind.fields();
            ffi::DatabaseActionKindDto {
                code: kind.code().into(),
                label: kind.label().into(),
                existing: fields.existing,
                new_name: fields.new_name,
                settings: fields.settings,
                confirmation: fields.confirmation,
            }
        })
        .collect()
}

fn planned(dto: &ffi::DatabaseActionDto) -> Result<String, String> {
    let kind = DatabaseActionKind::from_code(&dto.kind)
        .ok_or_else(|| "Choose a database action.".to_string())?;
    plan_database_action(&DatabaseActionRequest {
        driver: dto.driver.clone(),
        kind,
        current: dto.current.clone(),
        name: dto.name.clone(),
        new_name: dto.new_name.clone(),
        encoding: dto.encoding.clone(),
        collation: dto.collation.clone(),
        confirmation: dto.confirmation.clone(),
    })
}

pub fn database_action_plan(dto: &ffi::DatabaseActionDto) -> ffi::SqlPlanDto {
    sql_plan(planned(dto))
}

pub fn apply_database_action(
    engine: &mut BridgeEngine,
    connection: u64,
    dto: &ffi::DatabaseActionDto,
    request_token: u64,
) -> ffi::Submit {
    let sql = planned(dto);
    submit(engine, |e| {
        e.apply_ddl(unpack(connection), vec![sql?], request_token)
            .map(|()| connection)
            .map_err(|error| error.to_string())
    })
}
