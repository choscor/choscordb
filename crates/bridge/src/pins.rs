//! Typed transport for Rust-owned pin identity and durable storage.
use crate::ffi::{PinChangeDto, PinLoadDto, PinRecordDto, PinSaveDto};
use choscordb_pins::{PinRecord, PinStore};
use std::path::Path;

fn to_record(pin: PinRecordDto) -> PinRecord {
    PinRecord {
        profile_id: pin.profile_id,
        profile_name: pin.profile_name,
        object_id: pin.object_id,
        name: pin.name,
        qualified_name: pin.qualified_name,
        kind: pin.kind,
        parent_object_id: pin.parent_object_id,
        relation_subtype: pin.relation_subtype,
        ancestry_ids: pin.ancestry_ids,
        ancestry_names: pin.ancestry_names,
        unavailable: pin.unavailable,
    }
}

fn from_record(pin: PinRecord) -> PinRecordDto {
    PinRecordDto {
        profile_id: pin.profile_id,
        profile_name: pin.profile_name,
        object_id: pin.object_id,
        name: pin.name,
        qualified_name: pin.qualified_name,
        kind: pin.kind,
        parent_object_id: pin.parent_object_id,
        relation_subtype: pin.relation_subtype,
        ancestry_ids: pin.ancestry_ids,
        ancestry_names: pin.ancestry_names,
        unavailable: pin.unavailable,
    }
}

fn store(path: &str, profile_storage: bool) -> PinStore {
    if profile_storage {
        PinStore::for_profile_storage(Path::new(path))
    } else {
        PinStore::for_application_data(Path::new(path))
    }
}

pub fn pin_valid(pin: PinRecordDto) -> bool {
    choscordb_pins::valid(&to_record(pin))
}

pub fn pin_identity_key(pin: PinRecordDto) -> String {
    choscordb_pins::identity_key(&to_record(pin))
}

fn change(updated: Option<Vec<PinRecord>>) -> PinChangeDto {
    PinChangeDto {
        changed: updated.is_some(),
        pins: updated
            .unwrap_or_default()
            .into_iter()
            .map(from_record)
            .collect(),
    }
}

fn records(pins: Vec<PinRecordDto>) -> Vec<PinRecord> {
    pins.into_iter().map(to_record).collect()
}

pub fn pin_toggle(pins: Vec<PinRecordDto>, candidate: PinRecordDto, unpin: bool) -> PinChangeDto {
    change(choscordb_pins::toggle_pin(
        &records(pins),
        to_record(candidate),
        unpin,
    ))
}

pub fn pin_remove(pins: Vec<PinRecordDto>, key: &str) -> PinChangeDto {
    change(choscordb_pins::remove_pin(&records(pins), key))
}

pub fn pin_remove_profile(pins: Vec<PinRecordDto>, profile_id: &str) -> PinChangeDto {
    change(choscordb_pins::remove_profile_pins(
        &records(pins),
        profile_id,
    ))
}

pub fn pin_load(path: &str, profile_storage: bool) -> PinLoadDto {
    let result = store(path, profile_storage).load();
    PinLoadDto {
        pins: result.pins.into_iter().map(from_record).collect(),
        error: result.error,
    }
}

pub fn pin_save(path: &str, profile_storage: bool, pins: Vec<PinRecordDto>) -> PinSaveDto {
    match store(path, profile_storage).save(&records(pins)) {
        Ok(()) => PinSaveDto {
            success: true,
            error: String::new(),
        },
        Err(error) => PinSaveDto {
            success: false,
            error: error.to_string(),
        },
    }
}
