use choscordb_sql_language::{
    MAX_SEARCH_SOURCE_BYTES, completion_context, completion_source_supported,
};

#[test]
fn completion_is_offered_only_for_documents_within_the_source_limit() {
    assert!(completion_source_supported(0));
    assert!(completion_source_supported(MAX_SEARCH_SOURCE_BYTES));
    assert!(!completion_source_supported(MAX_SEARCH_SOURCE_BYTES + 1));
    let oversized = "a".repeat(MAX_SEARCH_SOURCE_BYTES + 1);
    assert_eq!(completion_context(&oversized, 1), None);
}
