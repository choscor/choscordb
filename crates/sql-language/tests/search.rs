use choscordb_sql_language::{
    MAX_SEARCH_SOURCE_BYTES, SearchError, SearchOptions, find, pattern_usable, replace_all,
    replacement_fits,
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
        Err(SearchError::OutputTooLarge)
    ));
    assert!(matches!(
        find("x", &"x".repeat(16385), 0, false, SearchOptions::default()),
        Err(SearchError::PatternTooLarge)
    ));
    assert!(replacement_fits(MAX_SEARCH_SOURCE_BYTES, 1, 1));
    assert!(!replacement_fits(MAX_SEARCH_SOURCE_BYTES, 1, 2));
    assert!(!replacement_fits(10, 11, 0));
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
        Err(SearchError::DocumentTooLarge)
    );
    let result = replace_all("secret-sql", "secret", "private", SearchOptions::default()).unwrap();
    assert!(!format!("{result:?}").contains("private"));
    assert!(!format!("{}", SearchError::DocumentTooLarge).contains("secret"));
}

#[test]
fn find_all_returns_bounded_nonoverlapping_folded_matches() {
    use choscordb_sql_language::find_all;
    let o = SearchOptions::default();
    let matches = find_all("ÉCOLE école ecole École", "école", o, 10).unwrap();
    assert_eq!(matches, [0..6, 7..13, 20..26]);
    assert_eq!(find_all("aaaa", "aa", o, 10).unwrap(), [0..2, 2..4]);
    assert_eq!(find_all("x x x", "x", o, 2).unwrap(), [0..1, 2..3]);
    assert_eq!(find_all("abc", "", o, 2), Err(SearchError::EmptyPattern));
}

#[test]
fn only_nonempty_bounded_patterns_are_usable() {
    assert!(pattern_usable("select"));
    assert!(pattern_usable(&"x".repeat(16 * 1024)));
    assert!(!pattern_usable(&"x".repeat(16 * 1024 + 1)));
    assert!(!pattern_usable(""));
}

#[test]
fn one_compiled_finder_searches_many_sources() {
    use choscordb_sql_language::TextFinder;
    let finder = TextFinder::new("école", SearchOptions::default()).unwrap();
    let spans = |source: &str, limit| -> Vec<_> {
        finder
            .find_all(source, limit)
            .unwrap()
            .into_iter()
            .map(|range| (range.start, range.end))
            .collect()
    };
    assert_eq!(spans("ÉCOLE ecole", 10), [(0, 6)]);
    assert_eq!(spans("x École école", 1), [(2, 8)]);
    assert!(finder.find_all("none", 10).unwrap().is_empty());
    let oversized = "x".repeat(MAX_SEARCH_SOURCE_BYTES + 1);
    assert_eq!(
        finder.find_all(&oversized, 1),
        Err(SearchError::DocumentTooLarge)
    );
    assert_eq!(
        TextFinder::new("", SearchOptions::default()).err(),
        Some(SearchError::EmptyPattern)
    );
}
