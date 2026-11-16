use crate::table_import::new_table_name;
use crate::{BridgeEngine, ffi, submit, unpack};
use choscordb_core::structure_edit_unavailable;
use choscordb_driver_api::GridEditDriver;
use choscordb_driver_api::{
    StructureChange, StructureChangeKind, StructureColumn, StructureDialect, StructureKey,
    StructurePane, StructureTable, plan_structure, structure_change_summary,
};
use std::collections::BTreeMap;

const NO_STRUCTURE: &str = "This connection cannot edit table structures.";

fn structure_driver(name: &str) -> Result<GridEditDriver, String> {
    crate::grid_edit::grid_driver(name).map_err(|_| NO_STRUCTURE.into())
}

fn structure_dialect(name: &str) -> Result<&'static dyn StructureDialect, String> {
    crate::drivers()
        .into_iter()
        .find(|driver| driver.id() == name)
        .and_then(|driver| driver.structure_dialect())
        .ok_or_else(|| NO_STRUCTURE.into())
}

pub fn structure_unavailable_reason(kind: &str, relation_subtype: &str) -> String {
    structure_edit_unavailable(kind, relation_subtype)
        .unwrap_or_default()
        .into()
}

pub fn structure_change_kinds(driver: &str, new_table: bool) -> Vec<ffi::StructureChangeKindDto> {
    let dialect = structure_dialect(driver);
    StructureChangeKind::ALL
        .into_iter()
        .map(|kind| ffi::StructureChangeKindDto {
            code: kind.code().into(),
            label: kind.label().into(),
            pane: match kind.pane() {
                StructurePane::Columns => ffi::StructurePaneDto::Columns,
                StructurePane::Indexes => ffi::StructurePaneDto::Indexes,
                StructurePane::Keys => ffi::StructurePaneDto::Keys,
            },
            unavailable: match &dialect {
                Ok(dialect) => kind
                    .unavailable(*dialect, new_table)
                    .unwrap_or_default()
                    .into(),
                Err(error) => error.clone(),
            },
            fields: kind
                .fields()
                .iter()
                .map(|field| ffi::StructureFieldDto {
                    key: field.key.into(),
                    label: field.label.into(),
                    input: field.kind.input().into(),
                    names: field.kind.names().into(),
                    required: field.required,
                    choices: field
                        .kind
                        .choices()
                        .iter()
                        .map(|(code, label)| ffi::CodeLabelDto {
                            code: (*code).into(),
                            label: (*label).into(),
                        })
                        .collect(),
                })
                .collect(),
        })
        .collect()
}

pub fn structure_types(driver: &str) -> Vec<String> {
    match structure_dialect(driver) {
        Ok(dialect) => dialect
            .column_types()
            .iter()
            .map(|name| (*name).to_owned())
            .collect(),
        Err(_) => Vec::new(),
    }
}

fn table(dto: &ffi::StructureTableDto) -> Result<StructureTable, String> {
    let driver = structure_driver(&dto.driver)?;
    Ok(StructureTable {
        driver,
        qualified_name: if dto.existing {
            dto.qualified_name.clone()
        } else {
            new_table_name(
                driver,
                &dto.parent_kind,
                &dto.parent_qualified,
                &dto.table_name,
            )?
        },
        new_table: !dto.existing,
        columns: dto
            .columns
            .iter()
            .map(|column| StructureColumn {
                name: column.name.clone(),
                data_type: column.data_type.clone(),
                nullable: column.nullable,
            })
            .collect(),
        indexes: dto.indexes.clone(),
        keys: dto
            .keys
            .iter()
            .map(|key| StructureKey {
                name: key.name.clone(),
                kind: key.kind.clone(),
            })
            .collect(),
    })
}

fn changes(dtos: &[ffi::StructureChangeDto]) -> Result<Vec<StructureChange>, String> {
    dtos.iter()
        .map(|dto| {
            let kind = StructureChangeKind::from_code(&dto.kind)
                .ok_or_else(|| format!("The change {:?} is not supported.", dto.kind))?;
            if dto.keys.len() != dto.values.len() {
                return Err("The change fields do not match their values.".into());
            }
            let mut values: BTreeMap<String, Vec<String>> = BTreeMap::new();
            for (key, value) in dto.keys.iter().zip(&dto.values) {
                values.entry(key.clone()).or_default().push(value.clone());
            }
            Ok(StructureChange { kind, values })
        })
        .collect()
}

fn planned(
    table_dto: &ffi::StructureTableDto,
    change_dtos: &[ffi::StructureChangeDto],
) -> Result<(Vec<StructureChange>, Vec<String>), String> {
    let table = table(table_dto)?;
    let rules = structure_dialect(&table_dto.driver)?;
    let changes = changes(change_dtos)?;
    let statements = plan_structure(&table, rules, &changes).map_err(|error| error.to_string())?;
    Ok((changes, statements))
}

pub fn structure_plan(
    table: &ffi::StructureTableDto,
    changes: Vec<ffi::StructureChangeDto>,
) -> ffi::StructurePlanDto {
    let summaries = self::changes(&changes)
        .map(|changes| changes.iter().map(structure_change_summary).collect())
        .unwrap_or_default();
    match planned(table, &changes) {
        Ok((_, statements)) => ffi::StructurePlanDto {
            valid: true,
            error: String::new(),
            summaries,
            statements,
        },
        Err(error) => ffi::StructurePlanDto {
            valid: false,
            error,
            summaries,
            statements: Vec::new(),
        },
    }
}

pub fn apply_structure(
    engine: &mut BridgeEngine,
    connection: u64,
    table: &ffi::StructureTableDto,
    changes: Vec<ffi::StructureChangeDto>,
    request_token: u64,
) -> ffi::Submit {
    let statements = planned(table, &changes).map(|(_, statements)| statements);
    submit(engine, |e| {
        e.apply_ddl(unpack(connection), statements?, request_token)
            .map(|()| connection)
            .map_err(|error| error.to_string())
    })
}
