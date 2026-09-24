//! Actor-owned execution history. Retained SQL and queued writes share a 64 MiB
//! budget. The single metadata worker additionally permits one bounded storage
//! JSON encoding buffer (at most MAX_COLLECTION_BYTES, currently 64 MiB). These
//! are separate bounds, not a claim that the combined pipeline fits 64 MiB.
use crate::{Event, HistoryEntry, HistoryStatus};
use choscordb_driver_api::{DriverError, ErrorKind, QueryId};
use std::sync::{
    Arc,
    atomic::{AtomicUsize, Ordering},
};
use tokio::sync::mpsc;
pub(crate) const LIMIT: usize = 64 * 1024 * 1024;
pub(crate) struct Reservation {
    used: Arc<AtomicUsize>,
    bytes: usize,
}
impl Drop for Reservation {
    fn drop(&mut self) {
        self.used.fetch_sub(self.bytes, Ordering::AcqRel);
    }
}
pub(crate) struct Ticket {
    query: QueryId,
    sql: Arc<String>,
    profile: Option<String>,
    timestamp: i64,
    started: std::time::Instant,
    sender: mpsc::Sender<crate::profiles::Command>,
    events: mpsc::Sender<Event>,
    actor_events: Option<crate::actor::EventSink>,
    reservation: Option<Arc<Reservation>>,
    finished: bool,
}
pub(crate) struct Write {
    pub query: QueryId,
    sql: Arc<String>,
    entry: HistoryEntry,
    _reservation: Arc<Reservation>,
}
impl Write {
    pub fn persist(self, storage: &mut choscordb_storage::Storage) -> Result<(), DriverError> {
        let mut entry = self.entry;
        entry.sql = Arc::try_unwrap(self.sql).unwrap_or_else(|sql| (*sql).clone());
        storage
            .record_history(&entry, now())
            .map(|_| ())
            .map_err(|_| DriverError::new(ErrorKind::Io, "Could not save query history"))
    }
}
fn now() -> i64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs()
        .min(i64::MAX as u64) as i64
}
impl Ticket {
    pub fn new(
        query: QueryId,
        sql: Arc<String>,
        profile: Option<String>,
        used: Arc<AtomicUsize>,
        sender: mpsc::Sender<crate::profiles::Command>,
        events: mpsc::Sender<Event>,
    ) -> Option<Self> {
        let bytes = sql.capacity().saturating_mul(2).saturating_add(1024);
        let admitted = sql.len() <= choscordb_storage::MAX_SQL_BYTES
            && used
                .fetch_update(Ordering::AcqRel, Ordering::Acquire, |n| {
                    n.checked_add(bytes).filter(|n| *n <= LIMIT)
                })
                .is_ok();
        let reservation = admitted.then(|| Arc::new(Reservation { used, bytes }));
        let sql = if admitted {
            sql
        } else {
            Arc::new(String::new())
        };
        Some(Self {
            query,
            sql,
            profile,
            timestamp: now(),
            started: std::time::Instant::now(),
            sender,
            events,
            actor_events: None,
            reservation,
            finished: false,
        })
    }
    pub(crate) fn bind_events(&mut self, events: crate::actor::EventSink) {
        self.actor_events = Some(events);
    }
    pub async fn finish(&mut self, status: HistoryStatus, rows: Option<u64>) {
        if let Some(error) = self.prepare_write(status, rows) {
            let event = Event::HistoryWriteFailed {
                query: self.query,
                error,
            };
            if let Some(events) = &self.actor_events {
                events.send(event).await;
            } else {
                let _ = self.events.send(event).await;
            }
        }
    }
    fn prepare_write(&mut self, status: HistoryStatus, rows: Option<u64>) -> Option<DriverError> {
        if self.finished {
            return None;
        }
        self.finished = true;
        let Some(reservation) = &self.reservation else {
            return Some(DriverError::new(
                ErrorKind::ResourceLimit,
                "Query history memory budget exceeded",
            ));
        };
        let job = Write {
            query: self.query,
            sql: self.sql.clone(),
            entry: HistoryEntry {
                id: uuid::Uuid::new_v4().to_string(),
                profile_id: self.profile.take(),
                sql: String::new(),
                timestamp: self.timestamp,
                duration_ms: self.started.elapsed().as_millis().min(u64::MAX as u128) as u64,
                status,
                row_count: rows,
            },
            _reservation: reservation.clone(),
        };
        if self
            .sender
            .try_send(crate::profiles::Command::HistoryWrite(job))
            .is_err()
        {
            return Some(DriverError::new(
                ErrorKind::ResourceLimit,
                "Query history worker is busy or unavailable",
            ));
        }
        None
    }
    pub async fn fail(&mut self, kind: ErrorKind) {
        self.finish(
            match kind {
                ErrorKind::Cancelled => HistoryStatus::Cancelled,
                ErrorKind::Disconnected => HistoryStatus::Disconnected,
                _ => HistoryStatus::Failed,
            },
            None,
        )
        .await;
    }
}
impl Drop for Ticket {
    fn drop(&mut self) {
        // Only abrupt task cancellation/panic reaches this fallback. Normal actor
        // termination awaits finish/fail before its terminal connection event.
        // Never spawn detached tasks or wait in Drop.
        if let Some(error) = self.prepare_write(HistoryStatus::Disconnected, None) {
            let _ = self.events.try_send(Event::HistoryWriteFailed {
                query: self.query,
                error,
            });
        }
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn full_event_queue_holds_failure_before_actor_can_emit_terminal_event() {
        let runtime = tokio::runtime::Runtime::new().unwrap();
        runtime.block_on(async {
            let (sender, _commands) = mpsc::channel(1);
            let (events, mut received) = mpsc::channel(1);
            events
                .send(Event::HistoryFlushed { request_token: 99 })
                .await
                .unwrap();
            let query = choscordb_driver_api::Arena::<()>::default().insert(());
            let mut ticket = Ticket::new(
                query,
                Arc::new("SELECT 1".into()),
                None,
                Arc::new(AtomicUsize::new(LIMIT)),
                sender,
                events.clone(),
            )
            .unwrap();
            let settled = ticket.fail(ErrorKind::Disconnected);
            tokio::pin!(settled);
            assert!(
                tokio::time::timeout(std::time::Duration::from_millis(20), &mut settled)
                    .await
                    .is_err()
            );
            assert!(matches!(
                received.recv().await,
                Some(Event::HistoryFlushed { .. })
            ));
            settled.await;
            // Settlement must enqueue the error before the caller proceeds to
            // Disconnected and the metadata flush barrier.
            assert!(matches!(
                received.try_recv(),
                Ok(Event::HistoryWriteFailed { .. })
            ));
            events
                .send(Event::HistoryFlushed { request_token: 100 })
                .await
                .unwrap();
            assert!(matches!(
                received.recv().await,
                Some(Event::HistoryFlushed { request_token: 100 })
            ));
        });
    }
    #[test]
    fn actor_history_failure_backpressure_obeys_shutdown() {
        tokio::runtime::Runtime::new().unwrap().block_on(async {
            let (sender, _commands) = mpsc::channel(1);
            let (events, _received) = mpsc::channel(1);
            events
                .send(Event::HistoryFlushed { request_token: 99 })
                .await
                .unwrap();
            let (shutdown, stopped) = tokio::sync::watch::channel(false);
            let query = choscordb_driver_api::Arena::<()>::default().insert(());
            let mut ticket = Ticket::new(
                query,
                Arc::new("SELECT 1".into()),
                None,
                Arc::new(AtomicUsize::new(LIMIT)),
                sender,
                events.clone(),
            )
            .unwrap();
            ticket.bind_events(crate::actor::EventSink::new(events, stopped));
            let failure = ticket.fail(ErrorKind::Disconnected);
            tokio::pin!(failure);
            assert!(
                tokio::time::timeout(std::time::Duration::from_millis(20), &mut failure)
                    .await
                    .is_err()
            );
            shutdown.send(true).unwrap();
            tokio::time::timeout(std::time::Duration::from_millis(200), failure)
                .await
                .expect("history error retained actor behind a full event queue");
        });
    }
    #[test]
    fn admission_and_metadata_queue_failures_are_explicit_and_release_budget() {
        let runtime = tokio::runtime::Runtime::new().unwrap();
        runtime.block_on(async {
            let used = Arc::new(AtomicUsize::new(LIMIT));
            let (sender, mut commands) = mpsc::channel(1);
            let (events, mut received) = mpsc::channel(4);
            let query = choscordb_driver_api::Arena::<()>::default().insert(());
            let sql = Arc::new("SELECT 1".to_string());
            let mut rejected = Ticket::new(
                query,
                sql.clone(),
                None,
                used.clone(),
                sender.clone(),
                events.clone(),
            )
            .unwrap();
            rejected.fail(ErrorKind::Disconnected).await;
            assert!(matches!(
                received.recv().await,
                Some(Event::HistoryWriteFailed { .. })
            ));
            used.store(0, Ordering::Release);
            let mut first = Ticket::new(
                query,
                sql.clone(),
                None,
                used.clone(),
                sender.clone(),
                events.clone(),
            )
            .unwrap();
            first.finish(HistoryStatus::Completed, Some(1)).await;
            drop(first);
            let reserved = used.load(Ordering::Acquire);
            assert!(reserved > 0);
            let mut second = Ticket::new(query, sql, None, used.clone(), sender, events).unwrap();
            second.finish(HistoryStatus::Completed, Some(1)).await;
            drop(second);
            assert!(matches!(
                received.recv().await,
                Some(Event::HistoryWriteFailed { .. })
            ));
            assert_eq!(used.load(Ordering::Acquire), reserved);
            drop(commands.recv().await);
            assert_eq!(used.load(Ordering::Acquire), 0);
        });
    }
}
