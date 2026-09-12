//! Application-wide LRU enforcing result and global allocation budgets.
//!
//! Every owned allocation is charged, including list nodes and value capacities.
//! Budgets exclude allocator bookkeeping, the fixed `size_of::<PageCache<K,V>>()`
//! root, and values owned by callers before insertion or after FFI copying.
//! List operations require no temporary heap allocations. Lookup/eviction are
//! linear in page count; the list keeps most recently used pages first.
pub const DEFAULT_RESULT_BYTES: usize = 64 * 1024 * 1024;
pub const DEFAULT_GLOBAL_BYTES: usize = 256 * 1024 * 1024;

/// Value size plus all exclusively owned allocation capacities. Implementations
/// must not undercount or mutate allocations through a shared reference.
pub trait ResidentSize {
    fn resident_bytes(&self) -> usize;
}
impl ResidentSize for Vec<u8> {
    fn resident_bytes(&self) -> usize {
        std::mem::size_of::<Self>().saturating_add(self.capacity())
    }
}
impl ResidentSize for choscordb_driver_api::ResultPage {
    fn resident_bytes(&self) -> usize {
        self.estimated_bytes()
    }
}

struct Node<K, V> {
    key: (K, u64),
    value: V,
    bytes: usize,
    pinned: bool,
    next: Option<Box<Node<K, V>>>,
}
#[derive(Debug, PartialEq, Eq)]
pub enum CacheError {
    PageTooLarge,
    PinnedPagesExhaustBudget,
}

/// Keys must include the query generation, not only its reusable slot.
pub struct PageCache<K, V> {
    head: Option<Box<Node<K, V>>>,
    per_result: usize,
    global: usize,
    bytes: usize,
}
impl<K, V> Drop for PageCache<K, V> {
    fn drop(&mut self) {
        // Avoid a recursive destructor for long lists.
        while let Some(mut node) = self.head.take() {
            self.head = node.next.take();
        }
    }
}
impl<K: Copy + Eq, V: ResidentSize> PageCache<K, V> {
    pub fn new(per_result: usize, global: usize) -> Self {
        Self {
            head: None,
            per_result,
            global,
            bytes: 0,
        }
    }
    /// Complete allocation charge of storing a value, including key and links.
    pub fn entry_bytes(value: &V) -> usize {
        std::mem::size_of::<Node<K, V>>().saturating_add(
            value
                .resident_bytes()
                .saturating_sub(std::mem::size_of::<V>()),
        )
    }
    fn nodes(&self) -> impl Iterator<Item = &Node<K, V>> {
        std::iter::successors(self.head.as_deref(), |node| node.next.as_deref())
    }
    fn unlink(&mut self, key: (K, u64)) -> Option<Box<Node<K, V>>> {
        let mut link = &mut self.head;
        loop {
            if link.as_ref()?.key == key {
                let mut node = link.take()?;
                *link = node.next.take();
                self.bytes -= node.bytes;
                return Some(node);
            }
            link = &mut link.as_mut()?.next;
        }
    }
    fn push(&mut self, mut node: Box<Node<K, V>>) {
        self.bytes += node.bytes;
        node.next = self.head.take();
        self.head = Some(node);
    }
    fn evict_lru(&mut self, result: Option<K>) {
        self.evict_one(result);
    }
    /// Failed insertion preserves entries, pinning and LRU order. A pinned page
    /// may be explicitly replaced by its owner using the same key.
    pub fn insert(
        &mut self,
        result: K,
        page: u64,
        value: V,
        pinned: bool,
    ) -> Result<(), CacheError> {
        let size = Self::entry_bytes(&value);
        if size > self.per_result || size > self.global {
            return Err(CacheError::PageTooLarge);
        }
        let key = (result, page);
        let (mut pinned_local, mut pinned_global) = (0usize, 0usize);
        for node in self.nodes().filter(|n| n.pinned && n.key != key) {
            pinned_global += node.bytes;
            if node.key.0 == result {
                pinned_local += node.bytes;
            }
        }
        if pinned_local > self.per_result - size || pinned_global > self.global - size {
            return Err(CacheError::PinnedPagesExhaustBudget);
        }
        // Preflight proved an eviction solution exists; mutation is now atomic
        // with respect to all recoverable errors and needs no scratch vectors.
        self.unlink(key);
        while self.result_bytes(result) > self.per_result - size {
            self.evict_lru(Some(result));
        }
        while self.bytes > self.global - size {
            self.evict_lru(None);
        }
        self.push(Box::new(Node {
            key,
            value,
            bytes: size,
            pinned,
            next: None,
        }));
        Ok(())
    }
    pub fn get(&mut self, result: K, page: u64) -> Option<&V> {
        let node = self.unlink((result, page))?;
        self.push(node);
        self.head.as_ref().map(|node| &node.value)
    }
    pub fn set_pinned(&mut self, result: K, page: u64, pinned: bool) -> bool {
        let mut node = self.head.as_deref_mut();
        while let Some(entry) = node {
            if entry.key == (result, page) {
                entry.pinned = pinned;
                return true;
            }
            node = entry.next.as_deref_mut();
        }
        false
    }
    /// Evict the least-recently-used unpinned page, optionally within one result.
    /// Used by the shared memory broker before waiting for transfer capacity.
    pub fn evict_one(&mut self, result: Option<K>) -> bool {
        let key = self
            .nodes()
            .filter(|n| !n.pinned && result.is_none_or(|r| n.key.0 == r))
            .last()
            .map(|n| n.key);
        key.is_some_and(|key| self.unlink(key).is_some())
    }
    /// Explicit owner removal may remove a pinned entry and returns its value.
    pub fn remove_page(&mut self, result: K, page: u64) -> Option<V> {
        self.unlink((result, page)).map(|node| node.value)
    }
    pub fn remove_result(&mut self, result: K) {
        loop {
            let key = self.nodes().find(|n| n.key.0 == result).map(|n| n.key);
            match key {
                Some(key) => {
                    self.unlink(key);
                }
                None => break,
            }
        }
    }
    pub fn resident_bytes(&self) -> usize {
        self.bytes
    }
    pub fn result_bytes(&self, result: K) -> usize {
        self.nodes()
            .filter(|n| n.key.0 == result)
            .map(|n| n.bytes)
            .sum()
    }
}
