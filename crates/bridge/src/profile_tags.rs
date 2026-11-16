use crate::ffi;
use choscordb_core::{
    ConnectionColor, ConnectionEnvironment, ConnectionProfile, ConnectionTags, ConnectionTone,
    ExecutionGate, MAX_ENVIRONMENT_LABEL_BYTES, SafetyLevel,
};

fn parsed<T: Default>(text: &str, parse: fn(&str) -> Option<T>, field: &str) -> Result<T, String> {
    if text.is_empty() {
        return Ok(T::default());
    }
    parse(text).ok_or_else(|| format!("Unknown {field}"))
}

pub(crate) fn parse(
    dto: &ffi::ProfileDto,
) -> Result<(ConnectionEnvironment, ConnectionColor, SafetyLevel), String> {
    Ok((
        parsed(
            &dto.environment,
            ConnectionEnvironment::from_code,
            "environment",
        )?,
        parsed(&dto.color, ConnectionColor::from_code, "connection color")?,
        parsed(&dto.safety, SafetyLevel::from_code, "safety level")?,
    ))
}

pub(crate) fn tone_code(tone: Option<ConnectionTone>) -> &'static str {
    match tone {
        None => "",
        Some(ConnectionTone::Neutral) => "neutral",
        Some(ConnectionTone::Brand) => "brand",
        Some(ConnectionTone::Success) => "success",
        Some(ConnectionTone::Warning) => "warning",
        Some(ConnectionTone::Danger) => "danger",
    }
}

pub(crate) fn fill(dto: &mut ffi::ProfileDto, profile: &ConnectionProfile) {
    dto.environment = profile.environment.code().into();
    dto.environment_label = profile.environment_label.clone().unwrap_or_default();
    dto.color = profile.color.code().into();
    dto.safety = profile.safety.code().into();
    let fields = tag_fields(profile.tags());
    (dto.tone, dto.tag_label, dto.chip_label) = fields;
}

pub(crate) fn refresh(dto: &mut ffi::ProfileDto) {
    let environment = parsed(
        &dto.environment,
        ConnectionEnvironment::from_code,
        "environment",
    );
    let color = parsed(&dto.color, ConnectionColor::from_code, "connection color");
    let (Ok(environment), Ok(color)) = (environment, color) else {
        dto.tone.clear();
        dto.tag_label.clear();
        dto.chip_label.clear();
        return;
    };
    let tags = ConnectionTags {
        name: &dto.name,
        environment,
        environment_label: (!dto.environment_label.is_empty())
            .then_some(dto.environment_label.as_str()),
        color,
    };
    let fields = tag_fields(tags);
    (dto.tone, dto.tag_label, dto.chip_label) = fields;
}

fn tag_fields(tags: ConnectionTags<'_>) -> (String, String, String) {
    (
        tone_code(tags.tone()).into(),
        tags.tag_label().unwrap_or_default(),
        tags.chip_label().unwrap_or_default(),
    )
}

fn choices<T: Copy>(
    all: &[T],
    code: fn(T) -> &'static str,
    label: fn(T) -> &'static str,
) -> Vec<ffi::ProfileChoiceDto> {
    all.iter()
        .map(|value| ffi::ProfileChoiceDto {
            code: code(*value).into(),
            label: label(*value).into(),
        })
        .collect()
}

pub fn profile_tag_options() -> ffi::ProfileTagOptionsDto {
    ffi::ProfileTagOptionsDto {
        environments: choices(
            &ConnectionEnvironment::ALL,
            ConnectionEnvironment::code,
            ConnectionEnvironment::label,
        ),
        colors: choices(
            &ConnectionColor::ALL,
            ConnectionColor::code,
            ConnectionColor::label,
        ),
        safety_levels: choices(&SafetyLevel::ALL, SafetyLevel::code, SafetyLevel::label),
        safety_names: choices(&SafetyLevel::ALL, SafetyLevel::code, SafetyLevel::name),
        labelled_environment: ConnectionEnvironment::Custom.code().into(),
        session_safety: SafetyLevel::ALL
            .into_iter()
            .map(|level| ffi::SessionSafetyDto {
                code: level.code().into(),
                unavailable: level.session_unavailable().unwrap_or("").into(),
            })
            .collect(),
        max_environment_label_bytes: MAX_ENVIRONMENT_LABEL_BYTES as u32,
    }
}

pub fn sql_execution_gate(safety: &str, reads_only: bool, destructive: bool) -> ffi::SqlGateDto {
    let level = match parsed(safety, SafetyLevel::from_code, "safety level") {
        Ok(level) => level,
        Err(message) => {
            return ffi::SqlGateDto {
                decision: "reject".into(),
                message,
                ..Default::default()
            };
        }
    };
    match level.gate(reads_only, destructive) {
        ExecutionGate::Run => ffi::SqlGateDto {
            decision: "run".into(),
            ..Default::default()
        },
        ExecutionGate::Confirm => ffi::SqlGateDto {
            decision: "confirm".into(),
            title: "Run statement?".into(),
            message: if destructive {
                "This statement can change or delete data."
            } else {
                "This connection asks for confirmation before each statement."
            }
            .into(),
            accept: "Run statement".into(),
        },
        ExecutionGate::Reject => ffi::SqlGateDto {
            decision: "reject".into(),
            message: "This connection is read-only. The statement can change data.".into(),
            ..Default::default()
        },
    }
}
