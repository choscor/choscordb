//! Typed transport for Rust-owned pin identity and durable storage.
use crate::ffi::{PinLoadDto, PinRecordDto, PinSaveDto};
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

pub fn pin_load(path: &str, profile_storage: bool) -> PinLoadDto {
    let result = store(path, profile_storage).load();
    PinLoadDto {
        pins: result.pins.into_iter().map(from_record).collect(),
        error: result.error,
    }
}

pub fn pin_save(path: &str, profile_storage: bool, pins: Vec<PinRecordDto>) -> PinSaveDto {
    let records = pins.into_iter().map(to_record).collect::<Vec<_>>();
    match store(path, profile_storage).save(&records) {
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
