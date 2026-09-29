//! Typed CXX transport for bounded deferred-value assembly.
use crate::ffi::DeferredAssemblyDto;
use choscordb_result_store::{
    CompletedDeferredValue, DeferredAssembler, DeferredAssemblerError, DeferredLoadPolicy,
};

pub struct RustDeferredAssembler {
    inner: Result<DeferredAssembler, DeferredAssemblerError>,
}

fn error_code(error: DeferredAssemblerError) -> String {
    match error {
        DeferredAssemblerError::TooLarge => "too_large",
        DeferredAssemblerError::InvalidChunk => "invalid_chunk",
        DeferredAssemblerError::InvalidUtf8 => "invalid_utf8",
    }
    .into()
}

pub fn deferred_assembler_new(
    database_type: &str,
    fallback: bool,
    declared_bytes: u64,
    resolved_bytes: u64,
    json: bool,
) -> Box<RustDeferredAssembler> {
    Box::new(RustDeferredAssembler {
        inner: DeferredAssembler::new(
            database_type,
            fallback,
            declared_bytes,
            resolved_bytes,
            if json {
                DeferredLoadPolicy::Json
            } else {
                DeferredLoadPolicy::Copy
            },
        ),
    })
}

pub fn deferred_assembler_initial_error(assembler: &RustDeferredAssembler) -> String {
    assembler
        .inner
        .as_ref()
        .err()
        .copied()
        .map_or_else(String::new, error_code)
}

pub fn deferred_assembler_push(
    assembler: &mut RustDeferredAssembler,
    kind: &str,
    offset: u64,
    total_bytes: u64,
    chunk: &[u8],
    has_lease: bool,
) -> DeferredAssemblyDto {
    let mut reply = DeferredAssemblyDto::default();
    let inner = match assembler.inner.as_mut() {
        Ok(inner) => inner,
        Err(error) => {
            reply.error = error_code(*error);
            return reply;
        }
    };
    match inner.push(kind, offset, total_bytes, chunk, has_lease) {
        Ok(Some(CompletedDeferredValue::Text(text))) => {
            reply.complete = true;
            reply.kind = "text".into();
            reply.text = text;
        }
        Ok(Some(CompletedDeferredValue::FallbackText {
            text,
            database_type,
        })) => {
            reply.complete = true;
            reply.kind = "fallback_text".into();
            reply.text = text;
            reply.database_type = database_type;
        }
        Ok(Some(CompletedDeferredValue::Binary(bytes))) => {
            reply.complete = true;
            reply.kind = "binary".into();
            reply.bytes = bytes;
        }
        Ok(None) => {}
        Err(error) => reply.error = error_code(error),
    }
    reply.received_bytes = inner.received_bytes();
    reply
}
