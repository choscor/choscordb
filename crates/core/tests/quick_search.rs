use choscordb_core::quick_search::QuickSearchNeedle;

fn score(query: &str, name: &str) -> Option<u32> {
    QuickSearchNeedle::new(query).score(name)
}

#[test]
fn closer_name_matches_rank_first() {
    let exact = score("sales orders", "Sales Orders").unwrap();
    let prefix = score("sales", "Sales Orders").unwrap();
    let substring = score("orders", "Sales Orders").unwrap();
    let words = score("sales ord", "Sales_Orders").unwrap();
    let subsequence = score("srdr", "Sales_Orders").unwrap();
    assert_eq!(exact, 0);
    assert!(exact < prefix && prefix < substring && substring < words);
    assert!(words < subsequence && subsequence < 100);
}

#[test]
fn later_and_longer_matches_rank_below_earlier_ones() {
    assert!(
        score("ord", "orders").unwrap() < score("ord", "orders_with_a_much_longer_name").unwrap()
    );
    assert!(score("ord", "x_orders").unwrap() < score("ord", "xxxxxxxxxxxxxxxx_orders").unwrap());
}

#[test]
fn unrelated_empty_and_unbounded_input_does_not_match() {
    assert_eq!(score("orders sales", "Sales Orders"), None);
    assert_eq!(score("sales xyz", "Sales Orders"), None);
    assert_eq!(score("", "Sales Orders"), None);
    assert_eq!(score("   ", "Sales Orders"), None);
    assert_eq!(score(&"x".repeat(129), "Sales Orders"), None);
    assert_eq!(score("sales", &"x".repeat(1025)), None);
    assert_eq!(score("sales", ""), None);
}

#[test]
fn separated_words_match_in_order_and_case_is_folded() {
    assert!(score("  sales   orders  ", "Sales.Orders").is_some());
    assert_eq!(score("ÉTÉ", "été"), Some(0));
}

mod palette {
    use choscordb_core::quick_search::{
        Destination, DestinationKind, MAX_QUICK_SEARCH_RESULTS, QuickSearchNeedle,
    };

    #[test]
    fn an_oversized_query_lists_no_destinations() {
        let rows = [
            row(DestinationKind::Object, "Object", "orders"),
            row(DestinationKind::OpenTab, "Tab", "Query 1"),
        ];
        let plan = QuickSearchNeedle::new(&"x".repeat(129)).plan(&rows, 2, 3);
        assert!(plan.destinations.is_empty());
        assert_eq!((plan.editor_rows, plan.history_rows), (2, 3));
    }

    #[test]
    fn objects_found_by_the_navigator_stay_listed_without_a_name_match() {
        let rows = [Destination {
            context: "public",
            ..row(DestinationKind::Object, "Object", "orders")
        }];
        assert_eq!(
            QuickSearchNeedle::new("zzz").plan(&rows, 0, 0).destinations,
            vec![0]
        );
    }

    fn row(
        kind: DestinationKind,
        group: &'static str,
        title: &'static str,
    ) -> Destination<'static> {
        Destination {
            kind,
            group,
            title,
            context: "",
            id: title,
        }
    }

    #[test]
    fn a_blank_query_lists_open_tabs_after_commands_and_objects_last() {
        let rows = [
            row(DestinationKind::Object, "Object", "orders"),
            row(DestinationKind::OpenTab, "Tab", "Query 1"),
            row(DestinationKind::Command, "Command", "Settings"),
        ];
        let plan = QuickSearchNeedle::new("  ").plan(&rows, 0, 0);
        assert_eq!(plan.destinations, vec![2, 1, 0]);
        assert!(!plan.more);
    }

    #[test]
    fn matches_rank_by_score_then_group_title_and_id() {
        let rows = [
            row(DestinationKind::Command, "Command", "Reorder columns"),
            row(DestinationKind::Object, "Object", "orders"),
            Destination {
                context: "orders",
                ..row(DestinationKind::Object, "Object", "line_items")
            },
            row(DestinationKind::OpenTab, "Tab", "unrelated"),
            row(DestinationKind::Command, "Command", "Order by"),
        ];
        let plan = QuickSearchNeedle::new("orders").plan(&rows, 0, 0);
        // Title and context matches tie and order by title, then a scattered
        // match. Tabs and commands that do not match are left out.
        assert_eq!(plan.destinations, vec![2, 1, 0]);
    }

    #[test]
    fn the_palette_shows_a_bounded_number_of_rows_and_reports_more() {
        let rows: Vec<_> = (0..70)
            .map(|_| row(DestinationKind::Command, "Command", "Run"))
            .collect();
        let plan = QuickSearchNeedle::new("").plan(&rows, 6, 30);
        assert_eq!(plan.destinations.len(), 70);
        assert_eq!(plan.editor_rows, 6);
        assert_eq!(plan.history_rows, MAX_QUICK_SEARCH_RESULTS - 76);
        assert!(plan.more);
        let fits = QuickSearchNeedle::new("").plan(&rows[..4], 6, 30);
        assert_eq!(
            (fits.editor_rows, fits.history_rows, fits.more),
            (6, 30, false)
        );
    }
}
