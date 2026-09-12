//! Conservative reservations cover raw rows/schema plus store serialization and
//! native conversion copies. Idle source buffers have a separate quarter-global
//! ceiling, preserving capacity for transfers. Consumer leases survive dequeue.
use choscordb_driver_api::{DriverError, ErrorKind, QueryId, Result};
use std::{
    collections::HashMap,
    sync::{Arc, Mutex, Weak},
};
use tokio::sync::Notify;

#[derive(Clone, Debug)]
pub struct PageMemoryConfig {
    pub per_result_bytes: usize,
    pub global_bytes: usize,
}
impl Default for PageMemoryConfig {
    fn default() -> Self {
        Self {
            per_result_bytes: 64 * 1024 * 1024,
            global_bytes: 256 * 1024 * 1024,
        }
    }
}
#[derive(Clone, Copy, Debug, Default)]
pub struct MemoryUsage {
    pub used_bytes: usize,
    pub source_bytes: usize,
    pub peak_bytes: usize,
}
#[derive(Default)]
struct State {
    usage: MemoryUsage,
    results: HashMap<QueryId, usize>,
}
pub(crate) struct Memory {
    config: PageMemoryConfig,
    state: Mutex<State>,
    changed: Notify,
    cache: Mutex<Weak<crate::hot_cache::HotCache>>,
}
impl Memory {
    pub fn new(config: PageMemoryConfig) -> std::io::Result<Arc<Self>> {
        if config.per_result_bytes.min(config.global_bytes) < 1024 * 1024 {
            return Err(std::io::Error::new(
                std::io::ErrorKind::InvalidInput,
                "page memory budgets must be at least 1 MiB",
            ));
        }
        Ok(Arc::new(Self {
            config,
            state: Mutex::new(State::default()),
            changed: Notify::new(),
            cache: Mutex::new(Weak::new()),
        }))
    }
    fn per_result(&self) -> usize {
        self.config.per_result_bytes.min(self.config.global_bytes)
    }
    pub fn raw_limit(&self) -> usize {
        self.per_result() / 32 - 64
    }
    pub fn usage(&self) -> MemoryUsage {
        self.state.lock().unwrap().usage
    }
    pub async fn acquire(self: &Arc<Self>, query: QueryId, source: bool) -> PageLease {
        let bytes = self.raw_limit() * if source { 8 } else { 16 } + 256;
        loop {
            // Register before checking bookkeeping: release cannot be lost between
            // the capacity check and awaiting the notification.
            let notified = self.changed.notified();
            tokio::pin!(notified);
            notified.as_mut().enable();
            if let Some(lease) = self.try_acquire(query, bytes, source) {
                return lease;
            }
            // Never hold memory bookkeeping while evicting: entry destruction
            // releases its reservation through this same broker.
            let source_blocked = source
                && bytes
                    > self.config.global_bytes / 4 - self.state.lock().unwrap().usage.source_bytes;
            // Hot pages do not consume the source quota. Only a source release
            // can resolve this wait, even if another budget is also binding.
            if !source_blocked {
                let cache = self.cache.lock().unwrap().upgrade();
                if cache.is_some_and(|cache| cache.evict_for(self.eviction_scope(query, bytes))) {
                    continue;
                }
            }
            notified.await;
        }
    }
    /// Prefer the result LRU only when that result's own ceiling is binding.
    /// Otherwise reclaim the globally oldest page across all connections.
    pub fn eviction_scope(&self, query: QueryId, bytes: usize) -> Option<QueryId> {
        let state = self.state.lock().unwrap();
        let used = state.results.get(&query).copied().unwrap_or(0);
        (bytes > self.per_result() - used).then_some(query)
    }
    pub fn cache_changed(&self) {
        self.changed.notify_waiters();
    }
    pub fn set_cache(&self, cache: &Arc<crate::hot_cache::HotCache>) {
        *self.cache.lock().unwrap() = Arc::downgrade(cache);
    }
    pub fn try_acquire(
        self: &Arc<Self>,
        query: QueryId,
        bytes: usize,
        source: bool,
    ) -> Option<PageLease> {
        let mut state = self.state.lock().unwrap();
        let result = state.results.get(&query).copied().unwrap_or(0);
        if bytes > self.config.global_bytes - state.usage.used_bytes
            || bytes > self.per_result() - result
            || (source && bytes > self.config.global_bytes / 4 - state.usage.source_bytes)
        {
            return None;
        }
        state.usage.used_bytes += bytes;
        state.usage.peak_bytes = state.usage.peak_bytes.max(state.usage.used_bytes);
        if source {
            state.usage.source_bytes += bytes;
        }
        *state.results.entry(query).or_default() += bytes;
        Some(PageLease {
            memory: self.clone(),
            query,
            bytes,
            source,
        })
    }
    fn release(&self, query: QueryId, bytes: usize, source: bool) {
        let mut state = self.state.lock().unwrap();
        state.usage.used_bytes -= bytes;
        if source {
            state.usage.source_bytes -= bytes;
        }
        if let Some(result) = state.results.get_mut(&query) {
            *result -= bytes;
            if *result == 0 {
                state.results.remove(&query);
            }
        }
        drop(state);
        self.changed.notify_waiters();
    }
}
/// Ownership of reserved result memory. Keep with its payload until it is freed.
/// This is intentionally neither Copy nor Clone.
pub struct PageLease {
    memory: Arc<Memory>,
    query: QueryId,
    bytes: usize,
    source: bool,
}
impl PageLease {
    pub fn reserved_bytes(&self) -> usize {
        self.bytes
    }
    /// Release unused conversion capacity; retains 256 bytes of bookkeeping.
    pub fn shrink_to(&mut self, payload_bytes: usize) -> Result<()> {
        let bytes = payload_bytes
            .checked_add(256)
            .filter(|bytes| *bytes <= self.bytes)
            .ok_or_else(|| DriverError::new(ErrorKind::ResourceLimit, "page lease cannot grow"))?;
        self.memory
            .release(self.query, self.bytes - bytes, self.source);
        self.bytes = bytes;
        Ok(())
    }
}
impl Drop for PageLease {
    fn drop(&mut self) {
        self.memory.release(self.query, self.bytes, self.source);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use choscordb_driver_api::{ResultPage, Value};
    use choscordb_result_store::StoredPage;

    #[tokio::test]
    async fn source_quota_wait_preserves_hot_pages_until_source_is_released() {
        let config = PageMemoryConfig {
            per_result_bytes: 1024 * 1024,
            global_bytes: 1024 * 1024,
        };
        let memory = Memory::new(config.clone()).unwrap();
        let cache = crate::hot_cache::HotCache::new(&config);
        memory.set_cache(&cache);
        let first = QueryId {
            slot: 1,
            generation: 0,
        };
        let second = QueryId {
            slot: 2,
            generation: 0,
        };
        let source = memory.acquire(first, true).await;
        cache.insert(
            &memory,
            first,
            &StoredPage {
                first_row: 0,
                page: ResultPage {
                    index: 0,
                    rows: vec![vec![Value::Integer(73)]],
                    has_more: true,
                },
            },
        );
        let cached = cache.usage().resident_bytes;
        assert!(cached > 0);
        let waiting = memory.acquire(second, true);
        tokio::pin!(waiting);
        // A zero-duration timeout polls the acquisition once before reporting
        // Pending, without relying on scheduling another worker or a sleep.
        assert!(
            tokio::time::timeout(std::time::Duration::ZERO, waiting.as_mut())
                .await
                .is_err()
        );
        assert_eq!(cache.usage().resident_bytes, cached);
        drop(source);
        let _next_source = tokio::time::timeout(std::time::Duration::from_secs(1), waiting)
            .await
            .unwrap();
        assert_eq!(cache.usage().resident_bytes, cached);
    }
}
