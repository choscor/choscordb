//! Converts core events to bridge events on the engine runtime, so that the UI thread only
//! takes finished events. One task converts in arrival order.
use crate::{convert, ffi};
use choscordb_core::{Event, PageLease};
use choscordb_driver_api::Arena;
use std::{
    collections::VecDeque,
    panic::{AssertUnwindSafe, catch_unwind},
    sync::{Arc, Condvar, Mutex},
    time::Duration,
};
use tokio::sync::mpsc;

/// The most events that are in conversion or wait for the UI thread at one time.
pub const EVENT_DRAIN_BUDGET: usize = 64;
/// The longest time that one drain waits for events in conversion. Small events finish in this
/// time and reach the UI in the same tick; a large page goes to a later tick.
const CONVERSION_WAIT: Duration = Duration::from_millis(2);

#[derive(Default)]
struct Ready {
    events: Mutex<VecDeque<ffi::BridgeEvent>>,
    converted: Condvar,
}

pub(crate) struct EventForwarder {
    raw: mpsc::UnboundedSender<Event>,
    ready: Arc<Ready>,
    /// Events sent for conversion and not yet taken.
    outstanding: usize,
}

impl EventForwarder {
    pub(crate) fn new(
        runtime: &tokio::runtime::Handle,
        leases: Arc<Mutex<Arena<PageLease>>>,
    ) -> Self {
        let (raw, mut received) = mpsc::unbounded_channel::<Event>();
        let ready = Arc::new(Ready::default());
        let output = ready.clone();
        runtime.spawn(async move {
            while let Some(event) = received.recv().await {
                let converted = catch_unwind(AssertUnwindSafe(|| convert::event(event, &leases)))
                    .unwrap_or_else(|_| failed());
                match output.events.lock() {
                    Ok(mut queue) => queue.push_back(converted),
                    Err(_) => return,
                }
                output.converted.notify_one();
            }
        });
        Self {
            raw,
            ready,
            outstanding: 0,
        }
    }
    /// True while the budget allows one more event in conversion.
    pub(crate) fn has_capacity(&self) -> bool {
        self.outstanding < EVENT_DRAIN_BUDGET
    }
    pub(crate) fn send(&mut self, event: Event) -> Result<(), ()> {
        self.raw.send(event).map_err(|_| ())?;
        self.outstanding += 1;
        Ok(())
    }
    /// The converted events, in arrival order, after a short wait for the events in conversion.
    pub(crate) fn take(&mut self) -> Result<Vec<ffi::BridgeEvent>, ()> {
        let queue = self.ready.events.lock().map_err(|_| ())?;
        let outstanding = self.outstanding;
        let (mut queue, _) = self
            .ready
            .converted
            .wait_timeout_while(queue, CONVERSION_WAIT, |queue| queue.len() < outstanding)
            .map_err(|_| ())?;
        let taken: Vec<_> = queue.drain(..).collect();
        self.outstanding -= taken.len();
        Ok(taken)
    }
}

pub(crate) fn failed() -> ffi::BridgeEvent {
    ffi::BridgeEvent {
        kind: "bridge_failed".into(),
        error: "Internal event conversion failure".into(),
        ..Default::default()
    }
}
