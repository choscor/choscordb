use choscordb_result_store::{
    CompletedDeferredValue, DeferredAssembler, DeferredAssemblerError, DeferredLoadPolicy,
};

#[test]
fn assembles_text_and_binary_chunks_without_changing_their_values() {
    let mut text = DeferredAssembler::new("text", false, 11, 0, DeferredLoadPolicy::Copy).unwrap();
    assert!(text.push("text", 0, 11, b"hello ", true).unwrap().is_none());
    assert_eq!(
        text.push("text", 6, 11, b"world", true).unwrap(),
        Some(CompletedDeferredValue::Text("hello world".into()))
    );
    assert_eq!(text.received_bytes(), 11);

    let mut binary = DeferredAssembler::new("blob", false, 3, 0, DeferredLoadPolicy::Json).unwrap();
    assert_eq!(
        binary.push("binary", 0, 3, &[0, 127, 255], true).unwrap(),
        Some(CompletedDeferredValue::Binary(vec![0, 127, 255]))
    );
}

#[test]
fn rejects_out_of_order_chunks_and_changed_total_or_kind() {
    let mut value = DeferredAssembler::new("jsonb", false, 4, 0, DeferredLoadPolicy::Json).unwrap();
    assert_eq!(
        value.push("text", 2, 4, b"ab", true).unwrap_err(),
        DeferredAssemblerError::InvalidChunk
    );
    assert!(value.push("text", 0, 4, b"ab", true).unwrap().is_none());
    assert_eq!(
        value.push("text", 2, 5, b"cd", true).unwrap_err(),
        DeferredAssemblerError::InvalidChunk
    );
    assert_eq!(
        value.push("binary", 2, 4, b"cd", true).unwrap_err(),
        DeferredAssemblerError::InvalidChunk
    );
    assert_eq!(
        value.push("text", 2, 4, b"cd", true).unwrap(),
        Some(CompletedDeferredValue::Text("abcd".into()))
    );
}

#[test]
fn enforces_eight_mib_for_each_value_and_the_sum() {
    const LIMIT: u64 = 8 * 1024 * 1024;
    assert_eq!(
        DeferredAssembler::new("blob", false, LIMIT + 1, 0, DeferredLoadPolicy::Copy).unwrap_err(),
        DeferredAssemblerError::TooLarge
    );
    assert_eq!(
        DeferredAssembler::new("blob", false, 2, LIMIT - 1, DeferredLoadPolicy::Json).unwrap_err(),
        DeferredAssemblerError::TooLarge
    );
    let mut boundary =
        DeferredAssembler::new("blob", false, 1, LIMIT - 1, DeferredLoadPolicy::Json).unwrap();
    assert_eq!(
        boundary.push("binary", 0, 1, &[42], true).unwrap(),
        Some(CompletedDeferredValue::Binary(vec![42]))
    );
}

#[test]
fn rejects_wrong_kind_missing_lease_and_bad_utf8() {
    let mut fallback =
        DeferredAssembler::new("custom", true, 2, 0, DeferredLoadPolicy::Copy).unwrap();
    assert_eq!(
        fallback.push("binary", 0, 2, b"ok", true).unwrap_err(),
        DeferredAssemblerError::InvalidChunk
    );
    assert_eq!(
        fallback.push("text", 0, 2, b"ok", false).unwrap_err(),
        DeferredAssemblerError::InvalidChunk
    );
    assert_eq!(
        fallback.push("text", 0, 2, b"ok", true).unwrap(),
        Some(CompletedDeferredValue::FallbackText {
            text: "ok".into(),
            database_type: "custom".into(),
        })
    );
    let mut malformed =
        DeferredAssembler::new("text", false, 2, 0, DeferredLoadPolicy::Json).unwrap();
    assert_eq!(
        malformed
            .push("text", 0, 2, &[0xc3, 0x28], true)
            .unwrap_err(),
        DeferredAssemblerError::InvalidUtf8
    );
}

#[test]
fn rejects_oversized_chunks_and_cannot_continue_after_completion() {
    let mut value =
        DeferredAssembler::new("blob", false, 65537, 0, DeferredLoadPolicy::Copy).unwrap();
    assert_eq!(
        value
            .push("binary", 0, 65537, &vec![0; 65537], true)
            .unwrap_err(),
        DeferredAssemblerError::InvalidChunk
    );
    assert!(
        value
            .push("binary", 0, 65537, &vec![0; 65536], true)
            .unwrap()
            .is_none()
    );
    assert_eq!(
        value.push("binary", 65536, 65537, &[1], true).unwrap(),
        Some(CompletedDeferredValue::Binary({
            let mut bytes = vec![0; 65537];
            bytes[65536] = 1;
            bytes
        }))
    );
    assert_eq!(
        value.push("binary", 65537, 65537, &[], true).unwrap_err(),
        DeferredAssemblerError::InvalidChunk
    );
}
