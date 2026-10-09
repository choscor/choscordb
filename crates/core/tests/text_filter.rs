use choscordb_core::text_filter::TextFilter;

#[test]
fn filters_ignore_case_and_surrounding_whitespace() {
    let filter = TextFilter::new("  Order ");
    assert!(!filter.is_empty());
    assert!(filter.matches("customer_orders"));
    assert!(filter.matches("ORDERS"));
    assert!(!filter.matches("customers"));
    let accented = TextFilter::new("ÉTÉ");
    assert!(accented.matches("les_été_2026"));
}

#[test]
fn blank_filters_accept_every_label() {
    for query in ["", "   "] {
        let filter = TextFilter::new(query);
        assert!(filter.is_empty());
        assert!(filter.matches("anything"));
        assert!(filter.matches(""));
    }
}
