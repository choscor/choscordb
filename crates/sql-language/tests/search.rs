use choscordb_sql_language::{
    MAX_SEARCH_SOURCE_BYTES, SearchError, SearchOptions, find, replace_all,
};
#[test]
fn unicode_literal_case_and_words() {
    let o = SearchOptions::default();
    let m = find("α École école", "éCOLE", 0, false, o)
        .unwrap()
        .unwrap();
    assert_eq!((m.start, m.end, m.wrapped), (3, 9, false));
    let m = find("a.b", ".", 0, false, o).unwrap().unwrap();
    assert_eq!((m.start, m.end), (1, 2));
    let o = SearchOptions {
        case_sensitive: true,
        whole_word: true,
    };
    let m = find("猫咪 猫 猫_", "猫", 0, false, o).unwrap().unwrap();
    assert_eq!((m.start, m.end), (7, 10));
    assert!(find("École", "école", 0, false, o).unwrap().is_none());
}
#[test]
fn direction_wrap_and_invalid_input() {
    let o = SearchOptions::default();
    for (start, backwards, expected) in [
        (3, true, (0, 2, false)),
        (0, true, (5, 7, true)),
        (7, false, (0, 2, true)),
    ] {
        let m = find("é x é", "é", start, backwards, o).unwrap().unwrap();
        assert_eq!((m.start, m.end, m.wrapped), expected);
    }
    assert_eq!(find("é", "é", 1, false, o), Err(SearchError::InvalidOffset));
    assert_eq!(find("x", "", 0, false, o), Err(SearchError::EmptyPattern));
}
#[test]
fn literal_replacement_and_no_rescan() {
    let r = replace_all("a a", "a", "aa$1", SearchOptions::default()).unwrap();
    assert_eq!(r.text, "aa$1 aa$1");
    assert_eq!(r.count, 2);
    let r = replace_all("é É", "é", "猫", SearchOptions::default()).unwrap();
    assert_eq!(r.text, "猫 猫");
    assert_eq!(r.count, 2);
}
#[test]
fn input_and_growing_output_limits() {
    let replacement = "x".repeat(MAX_SEARCH_SOURCE_BYTES);
    assert!(matches!(
        replace_all("aa", "a", &replacement, SearchOptions::default()),
        Err(SearchError::ResourceLimit)
    ));
    assert!(matches!(
        find("x", &"x".repeat(16385), 0, false, SearchOptions::default()),
        Err(SearchError::ResourceLimit)
    ));
}
#[test]
fn backwards_overlap_and_replacement_nonoverlap_are_explicit() {
    let o = SearchOptions::default();
    let found = find("banana", "ana", 6, true, o).unwrap().unwrap();
    assert_eq!((found.start, found.end, found.wrapped), (3, 6, false));
    let result = replace_all("banana", "ana", "x", o).unwrap();
    assert_eq!(result.text, "bxna");
    assert_eq!(result.count, 1);
    assert!(find("abc", "z", 0, false, o).unwrap().is_none());
    assert_eq!(replace_all("abc", "z", "$0", o).unwrap().count, 0);
}
#[test]
fn source_limit_and_no_sql_in_debug_or_error() {
    let oversized = "x".repeat(MAX_SEARCH_SOURCE_BYTES + 1);
    assert_eq!(
        find(&oversized, "x", 0, false, SearchOptions::default()),
        Err(SearchError::ResourceLimit)
    );
    let result = replace_all("secret-sql", "secret", "private", SearchOptions::default()).unwrap();
    assert!(!format!("{result:?}").contains("private"));
    assert!(!format!("{}", SearchError::ResourceLimit).contains("secret"));
}
