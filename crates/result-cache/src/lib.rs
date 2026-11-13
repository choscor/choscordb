pub const DEFAULT_RESULT_BYTES: usize = 64 * 1024 * 1024;
pub const DEFAULT_GLOBAL_BYTES: usize = 256 * 1024 * 1024;

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

pub struct PageCache<K, V> {
    head: Option<Box<Node<K, V>>>,
    per_result: usize,
    global: usize,
    bytes: usize,
}
impl<K, V> Drop for PageCache<K, V> {
    fn drop(&mut self) {
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
    pub fn evict_one(&mut self, result: Option<K>) -> bool {
        let key = self
            .nodes()
            .filter(|n| !n.pinned && result.is_none_or(|r| n.key.0 == r))
            .last()
            .map(|n| n.key);
        key.is_some_and(|key| self.unlink(key).is_some())
    }
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
