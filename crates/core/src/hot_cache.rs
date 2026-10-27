//! Original pages are expendable hot copies. Transfers have separate reservations
//! so eviction never invalidates a page already held by a consumer.
use crate::{PageLease, PageMemoryConfig, memory::Memory};
use choscordb_driver_api::{QueryId, ResultPage};
use choscordb_result_cache::{PageCache, ResidentSize};
use std::sync::{Arc, Mutex};

struct CachedPage {
    first_row: u64,
    page: Arc<ResultPage>,
    _lease: Option<PageLease>,
}
impl ResidentSize for CachedPage {
    fn resident_bytes(&self) -> usize {
        std::mem::size_of::<Self>() + self.page.estimated_bytes()
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
    /// The first row and the shared page. The actor acquires the transfer reservation first.
    pub fn read(&self, query: QueryId, index: u64) -> Option<(u64, Arc<ResultPage>)> {
        let mut state = self.0.lock().unwrap();
        let page = state
            .pages
            .get(query, index)
            .map(|entry| (entry.first_row, entry.page.clone()));
        if page.is_some() {
            state.hits = state.hits.saturating_add(1);
        } else {
            state.misses = state.misses.saturating_add(1);
        }
        page
    }
    /// Shares `page` with the cache. The charge is the whole page, because the cache keeps
    /// the page alive after the other holders drop it.
    pub fn insert(
        &self,
        memory: &Arc<Memory>,
        query: QueryId,
        first_row: u64,
        page: &Arc<ResultPage>,
    ) {
        let mut entry = CachedPage {
            first_row,
            page: page.clone(),
            _lease: None,
        };
        let bytes = PageCache::<QueryId, CachedPage>::entry_bytes(&entry);
        let mut state = self.0.lock().unwrap();
        state.pages.remove_page(query, page.index);
        let lease = loop {
            if let Some(lease) = memory.try_acquire(query, bytes, false) {
                break lease;
            }
            if !state.pages.evict_one(memory.eviction_scope(query, bytes)) {
                return;
            }
        };
        entry._lease = Some(lease);
        let _ = state.pages.insert(query, page.index, entry, false);
        drop(state);
        // A waiter may have found no evictable page just before this insertion.
        // Wake it even though total reserved memory increased: reclaimable
        // capacity has become available.
        memory.cache_changed();
    }
}
