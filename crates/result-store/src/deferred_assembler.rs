//! Bounded, ordered assembly of one deferred database value.

pub const MAX_DEFERRED_LOAD_BYTES: u64 = 8 * 1024 * 1024;
pub const MAX_DEFERRED_CHUNK_BYTES: usize = 65536;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DeferredLoadPolicy {
    Json,
    Copy,
}

#[derive(Debug, PartialEq, Eq)]
pub enum CompletedDeferredValue {
    Text(String),
    FallbackText { text: String, database_type: String },
    Binary(Vec<u8>),
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, thiserror::Error)]
pub enum DeferredAssemblerError {
    #[error("complete values exceed the 8 MiB loading limit")]
    TooLarge,
    #[error("invalid or stale complete value chunk")]
    InvalidChunk,
    #[error("complete text value is not valid UTF-8")]
    InvalidUtf8,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum ChunkKind {
    Text,
    Binary,
}

#[derive(Debug)]
pub struct DeferredAssembler {
    database_type: String,
    fallback: bool,
    declared_bytes: u64,
    remaining_budget: u64,
    policy: DeferredLoadPolicy,
    expected_kind: Option<ChunkKind>,
    actual_kind: Option<ChunkKind>,
    total_bytes: Option<u64>,
    received_bytes: u64,
    bytes: Vec<u8>,
    complete: bool,
}

impl DeferredAssembler {
    pub fn new(
        database_type: &str,
        fallback: bool,
        declared_bytes: u64,
        resolved_bytes: u64,
        policy: DeferredLoadPolicy,
    ) -> Result<Self, DeferredAssemblerError> {
        let Some(remaining_budget) = MAX_DEFERRED_LOAD_BYTES.checked_sub(resolved_bytes) else {
            return Err(DeferredAssemblerError::TooLarge);
        };
        if declared_bytes > remaining_budget {
            return Err(DeferredAssemblerError::TooLarge);
        }
        let expected_kind = if fallback {
            Some(ChunkKind::Text)
        } else if policy == DeferredLoadPolicy::Json {
            match database_type.to_ascii_lowercase().as_str() {
                "text" | "string" | "json" | "jsonb" => Some(ChunkKind::Text),
                "binary" | "blob" | "bytea" | "varbinary" => Some(ChunkKind::Binary),
                _ => None,
            }
        } else {
            None
        };
        Ok(Self {
            database_type: database_type.to_owned(),
            fallback,
            declared_bytes,
            remaining_budget,
            policy,
            expected_kind,
            actual_kind: None,
            total_bytes: None,
            received_bytes: 0,
            bytes: Vec::new(),
            complete: false,
        })
    }

    pub fn received_bytes(&self) -> u64 {
        self.received_bytes
    }

    pub fn push(
        &mut self,
        kind: &str,
        offset: u64,
        total_bytes: u64,
        chunk: &[u8],
        has_lease: bool,
    ) -> Result<Option<CompletedDeferredValue>, DeferredAssemblerError> {
        let kind = match kind {
            "text" => ChunkKind::Text,
            "binary" => ChunkKind::Binary,
            _ => return Err(DeferredAssemblerError::InvalidChunk),
        };
        let chunk_bytes = chunk.len() as u64;
        if self.complete
            || !has_lease
            || self.expected_kind.is_some_and(|expected| expected != kind)
            || self.actual_kind.is_some_and(|previous| previous != kind)
            || self
                .total_bytes
                .is_some_and(|previous| previous != total_bytes)
            || total_bytes > self.remaining_budget
            || (self.policy == DeferredLoadPolicy::Copy && total_bytes != self.declared_bytes)
            || chunk.len() > MAX_DEFERRED_CHUNK_BYTES
            || offset != self.received_bytes()
            || offset > total_bytes
            || chunk_bytes > total_bytes - offset
            || (chunk.is_empty() && offset < total_bytes)
        {
            return Err(DeferredAssemblerError::InvalidChunk);
        }
        self.actual_kind = Some(kind);
        self.total_bytes = Some(total_bytes);
        self.bytes.extend_from_slice(chunk);
        self.received_bytes += chunk_bytes;
        if self.received_bytes() < total_bytes {
            return Ok(None);
        }
        self.complete = true;
        let bytes = std::mem::take(&mut self.bytes);
        let value = match kind {
            ChunkKind::Text => {
                let text =
                    String::from_utf8(bytes).map_err(|_| DeferredAssemblerError::InvalidUtf8)?;
                if self.fallback {
                    CompletedDeferredValue::FallbackText {
                        text,
                        database_type: self.database_type.clone(),
                    }
                } else {
                    CompletedDeferredValue::Text(text)
                }
            }
            ChunkKind::Binary => CompletedDeferredValue::Binary(bytes),
        };
        Ok(Some(value))
    }
}
