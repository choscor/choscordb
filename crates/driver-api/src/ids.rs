use serde::{Deserialize, Serialize};
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub struct Handle {
    pub slot: u32,
    pub generation: u32,
}
pub type ConnectionId = Handle;
pub type QueryId = Handle;
pub type CursorId = Handle;
pub type PageId = Handle;
struct Slot<T> {
    generation: u32,
    value: Option<T>,
}
pub struct Arena<T> {
    slots: Vec<Slot<T>>,
}
impl<T> Default for Arena<T> {
    fn default() -> Self {
        Self { slots: Vec::new() }
    }
}
impl<T> Arena<T> {
    pub fn insert(&mut self, value: T) -> Handle {
        if let Some((i, slot)) = self
            .slots
            .iter_mut()
            .enumerate()
            .find(|(_, s)| s.value.is_none() && s.generation < u32::MAX)
        {
            slot.value = Some(value);
            return Handle {
                slot: i as u32,
                generation: slot.generation,
            };
        }
        let slot = u32::try_from(self.slots.len()).expect("handle arena exhausted");
        self.slots.push(Slot {
            generation: 0,
            value: Some(value),
        });
        Handle {
            slot,
            generation: 0,
        }
    }
    pub fn get(&self, id: Handle) -> Option<&T> {
        self.slots
            .get(id.slot as usize)
            .filter(|s| s.generation == id.generation)?
            .value
            .as_ref()
    }
    pub fn get_mut(&mut self, id: Handle) -> Option<&mut T> {
        self.slots
            .get_mut(id.slot as usize)
            .filter(|s| s.generation == id.generation)?
            .value
            .as_mut()
    }
    pub fn remove(&mut self, id: Handle) -> Option<T> {
        let s = self
            .slots
            .get_mut(id.slot as usize)
            .filter(|s| s.generation == id.generation)?;
        let value = s.value.take()?;
        s.generation = s.generation.saturating_add(1);
        Some(value)
    }
}
