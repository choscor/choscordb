fn overhead() -> usize {
    PageCache::<i32, Vec<u8>>::entry_bytes(&Vec::new())
}
fn page_bytes() -> usize {
    overhead() + 36
}
use choscordb_result_cache::*;

#[test]
fn evicts_lru_across_results_without_evicting_visible_pages() {
    let mut cache = PageCache::new(2 * page_bytes(), 3 * page_bytes());
    cache.insert(1, 0, vec![0; 36], true).unwrap();
    cache.insert(1, 1, vec![1; 36], false).unwrap();
    cache.insert(2, 0, vec![2; 36], false).unwrap();
    assert!(cache.get(1, 1).is_some());
    cache.insert(2, 1, vec![3; 36], false).unwrap();
    assert!(cache.get(1, 0).is_some());
    assert!(cache.get(1, 1).is_some());
    assert!(cache.get(2, 0).is_none());
    assert_eq!(cache.resident_bytes(), 3 * page_bytes());
}

#[test]
fn rejects_pressure_from_pinned_pages_atomically() {
    let mut cache = PageCache::new(2 * page_bytes(), 3 * page_bytes());
    cache.insert(1, 0, vec![0; 36], true).unwrap();
    cache.insert(1, 1, vec![1; 36], true).unwrap();
    cache.insert(2, 0, vec![2; 36], false).unwrap();
    assert_eq!(
        cache.insert(1, 2, vec![3; 36], false),
        Err(CacheError::PinnedPagesExhaustBudget)
    );
    assert_eq!(cache.resident_bytes(), 3 * page_bytes());
    assert_eq!(cache.get(2, 0).unwrap()[0], 2);
    cache.set_pinned(1, 0, false);
    cache.insert(1, 2, vec![3; 36], true).unwrap();
    assert!(cache.get(1, 0).is_none());
    assert!(cache.get(2, 0).is_some());
}

#[test]
fn replacing_and_releasing_pages_reclaims_accounted_capacity() {
    let mut cache = PageCache::new(overhead() + 76, 2 * (overhead() + 76));
    let mut reserved = Vec::with_capacity(76);
    reserved.push(1);
    cache.insert(1, 0, reserved, true).unwrap();
    assert_eq!(cache.result_bytes(1), overhead() + 76);
    cache.insert(1, 0, vec![2; 1], true).unwrap();
    assert_eq!(cache.result_bytes(1), overhead() + 1);
    assert_eq!(
        cache.insert(1, 1, vec![0; 101], false),
        Err(CacheError::PageTooLarge)
    );
    cache.remove_result(1);
    assert_eq!(cache.resident_bytes(), 0);
}

proptest::proptest! {
    #[test]
    fn arbitrary_paging_never_exceeds_budgets(operations in proptest::collection::vec((0u8..5, 0u8..20, 0usize..200, proptest::bool::ANY), 1..200)) {
        let mut cache = PageCache::new(400, 1000);
        for (result, page, bytes, pin) in operations {
            let _ = cache.insert(result, page.into(), vec![0; bytes], pin);
            proptest::prop_assert!(cache.resident_bytes() <= 1000);
            for id in 0..5 { proptest::prop_assert!(cache.result_bytes(id) <= 400); }
        }
    }
}

#[test]
fn empty_values_charge_their_owning_nodes() {
    struct Empty;
    impl ResidentSize for Empty {
        fn resident_bytes(&self) -> usize {
            0
        }
    }
    let mut cache = PageCache::new(128, 128);
    for page in 0..10_000 {
        cache.insert(1u64, page, Empty, false).unwrap();
    }
    let remaining = (0..10_000)
        .filter(|page| cache.get(1, *page).is_some())
        .count();
    assert!(
        remaining <= 4,
        "owning nodes must bound zero-sized page count: {remaining}"
    );
}

#[test]
fn global_pinning_failure_preserves_lru_and_existing_pages() {
    let size = page_bytes();
    let mut cache = PageCache::new(3 * size, 3 * size);
    cache.insert(1, 0, vec![1; 36], false).unwrap();
    cache.insert(2, 0, vec![2; 36], true).unwrap();
    cache.insert(3, 0, vec![3; 36], true).unwrap();
    // New allocation requires more than the only unpinned node can free.
    assert_eq!(
        cache.insert(4, 0, vec![4; 37], false),
        Err(CacheError::PinnedPagesExhaustBudget)
    );
    assert_eq!(cache.resident_bytes(), 3 * size);
    cache.set_pinned(2, 0, false);
    cache.insert(4, 0, vec![4; 36], false).unwrap();
    assert!(cache.get(1, 0).is_none());
    assert!(cache.get(2, 0).is_some());
    assert!(cache.get(3, 0).is_some());
}

#[test]
fn real_pages_charge_reserved_row_and_value_capacities() {
    use choscordb_driver_api::{ResultPage, Value};
    let mut text = String::with_capacity(120);
    text.push('a');
    let mut row = Vec::with_capacity(8);
    row.push(Value::Text(text));
    let mut rows = Vec::with_capacity(10);
    rows.push(row);
    let page = ResultPage {
        index: 0,
        rows,
        has_more: false,
    };
    let payload = 10 * std::mem::size_of::<Vec<Value>>() + 8 * std::mem::size_of::<Value>() + 120;
    let charge = PageCache::<u64, ResultPage>::entry_bytes(&page);
    assert!(charge >= payload + std::mem::size_of::<ResultPage>() + 2 * std::mem::size_of::<u64>());
    let mut cache = PageCache::new(charge, charge);
    cache.insert(1u64, 0, page, true).unwrap();
    assert_eq!(cache.resident_bytes(), charge);
    cache.remove_result(1);
    assert_eq!(cache.resident_bytes(), 0);
}

#[test]
fn memory_pressure_evicts_only_unpinned_pages_and_removal_transfers_ownership() {
    let mut cache = PageCache::new(4 * page_bytes(), 4 * page_bytes());
    cache.insert(1, 0, vec![1; 36], true).unwrap();
    cache.insert(1, 1, vec![2; 36], false).unwrap();
    cache.insert(2, 0, vec![3; 36], false).unwrap();
    assert!(cache.evict_one(Some(1)));
    assert!(cache.get(1, 0).is_some());
    assert!(cache.get(1, 1).is_none());
    assert!(!cache.evict_one(Some(1)));
    assert!(cache.evict_one(None));
    assert_eq!(cache.resident_bytes(), page_bytes());
    assert_eq!(cache.remove_page(1, 0), Some(vec![1; 36]));
    assert_eq!(cache.resident_bytes(), 0);
    assert!(!cache.evict_one(None));
}
