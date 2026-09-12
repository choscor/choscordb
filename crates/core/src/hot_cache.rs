//! Original pages are expendable hot copies. Transfers have separate reservations
//! so eviction never invalidates a page already held by a consumer.
use crate::{PageLease, PageMemoryConfig, memory::Memory};
use choscordb_driver_api::{QueryId, ResultPage};
use choscordb_result_cache::{PageCache, ResidentSize};
use choscordb_result_store::StoredPage;
use std::sync::{Arc, Mutex};

struct CachedPage {
    stored: StoredPage,
    _lease: Option<PageLease>,
}
impl ResidentSize for CachedPage {
    fn resident_bytes(&self) -> usize {
        std::mem::size_of::<Self>() + self.stored.page.estimated_bytes()
            - std::mem::size_of::<ResultPage>()
    }
}
#[derive(Clone, Copy, Debug, Default)]
pub struct CacheUsage {
    pub hits: u64,
    pub misses: u64,
    pub resident_bytes: usize,
}
struct State {
    pages: PageCache<QueryId, CachedPage>,
    hits: u64,
    misses: u64,
}
pub(crate) struct HotCache(Mutex<State>);
impl HotCache {
    pub fn new(config: &PageMemoryConfig) -> Arc<Self> {
        Arc::new(Self(Mutex::new(State {
            pages: PageCache::new(config.per_result_bytes, config.global_bytes),
            hits: 0,
            misses: 0,
        })))
    }
    pub fn usage(&self) -> CacheUsage {
        let state = self.0.lock().unwrap();
        CacheUsage {
            hits: state.hits,
            misses: state.misses,
            resident_bytes: state.pages.resident_bytes(),
        }
    }
    pub fn evict_for(&self, result: Option<QueryId>) -> bool {
        let mut state = self.0.lock().unwrap();
        state.pages.evict_one(result)
    }
    pub fn remove(&self, query: QueryId) {
        self.0.lock().unwrap().pages.remove_result(query);
    }
    /// The actor must acquire the transfer reservation before making this copy.
    pub fn read(&self, query: QueryId, index: u64) -> Option<StoredPage> {
        let mut state = self.0.lock().unwrap();
        let page = state.pages.get(query, index).map(|entry| StoredPage {
            first_row: entry.stored.first_row,
            page: entry.stored.page.clone(),
        });
        if page.is_some() {
            state.hits = state.hits.saturating_add(1);
        } else {
            state.misses = state.misses.saturating_add(1);
        }
        page
    }
    pub fn insert(&self, memory: &Arc<Memory>, query: QueryId, stored: &StoredPage) {
        // Compute a conservative clone charge before allocating. Cloning Vec and
        // String retains contents, not unused capacity, so the original is an upper bound.
        let empty = CachedPage {
            stored: StoredPage {
                first_row: 0,
                page: ResultPage {
                    index: 0,
                    rows: vec![],
                    has_more: false,
                },
            },
            _lease: None,
        };
        let bytes = PageCache::<QueryId, CachedPage>::entry_bytes(&empty)
            .saturating_add(stored.page.estimated_bytes() - std::mem::size_of::<ResultPage>());
        let mut state = self.0.lock().unwrap();
        state.pages.remove_page(query, stored.page.index);
        let lease = loop {
            if let Some(lease) = memory.try_acquire(query, bytes, false) {
                break lease;
            }
            if !state.pages.evict_one(memory.eviction_scope(query, bytes)) {
                return;
            }
        };
        let entry = CachedPage {
            stored: StoredPage {
                first_row: stored.first_row,
                page: stored.page.clone(),
            },
            _lease: Some(lease),
        };
        let _ = state.pages.insert(query, stored.page.index, entry, false);
        drop(state);
        // A waiter may have found no evictable page just before this insertion.
        // Wake it even though total reserved memory increased: reclaimable
        // capacity has become available.
        memory.cache_changed();
    }
}
