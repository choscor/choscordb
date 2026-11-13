use crate::{
    Event,
    actor::{CatalogRead, Command, EventSink, Request, catalog_read},
    connection_machine::{CatalogSession as Catalog, ReadSession, read_session},
};
use choscordb_driver_api::*;
use std::{
    sync::{
        Arc, OnceLock,
        atomic::{AtomicBool, Ordering},
    },
    time::{Duration, Instant},
};

pub(crate) const CATALOG_RETRY: Duration = Duration::from_secs(30);
use tokio::sync::{mpsc, watch};

#[derive(Default)]
pub(crate) struct CatalogRoute {
    reads: OnceLock<mpsc::Sender<CatalogRead>>,
    in_transaction: AtomicBool,
}
impl CatalogRoute {
    pub(crate) fn reads(&self) -> Option<&mpsc::Sender<CatalogRead>> {
        let reads = self.reads.get();
        let catalog = if reads.is_some() {
            Catalog::Closed
        } else {
            Catalog::Unsupported
        };
        match read_session(self.in_transaction(), catalog) {
            ReadSession::Catalog => reads,
            ReadSession::Query => None,
        }
    }
    pub(crate) fn in_transaction(&self) -> bool {
        self.in_transaction.load(Ordering::Acquire)
    }
    pub(crate) fn follow(&self, state: Result<Option<bool>>) {
        let active = !matches!(state, Ok(Some(false)));
        self.in_transaction.store(active, Ordering::Release);
    }
    pub(crate) fn start(
        &self,
        connection: &dyn Connection,
        session: CatalogSession,
        capacity: usize,
    ) {
        let Some(opener) = connection.sibling_sessions() else {
            return;
        };
        let (sender, reads) = mpsc::channel(capacity);
        if self.reads.set(sender).is_ok() {
            tokio::spawn(run(opener, reads, session));
        }
    }
}

pub(crate) struct CatalogSession {
    pub id: ConnectionId,
    pub fallback: mpsc::WeakSender<Request>,
    pub events: EventSink,
    pub shutdown: watch::Receiver<bool>,
    pub grace: Duration,
}

async fn run(
    opener: Arc<dyn SessionOpener>,
    mut reads: mpsc::Receiver<CatalogRead>,
    mut task: CatalogSession,
) {
    let mut session: Option<Box<dyn Connection>> = None;
    let mut retry_at: Option<Instant> = None;
    loop {
        let read = tokio::select! {
            biased;
            _ = crate::operation::signalled(&mut task.shutdown) => break,
            read = reads.recv() => match read {
                Some(read) => read,
                None => break,
            },
        };
        let catalog = match (&session, retry_at.is_some_and(|at| Instant::now() < at)) {
            (Some(_), _) => Catalog::Open,
            (None, true) => Catalog::RetryPending,
            (None, false) => Catalog::Closed,
        };
        let current = match (read_session(false, catalog), &mut session) {
            (ReadSession::Query, _) => None,
            (ReadSession::Catalog, Some(current)) => Some(current),
            (ReadSession::Catalog, None) => {
                let opened = tokio::select! {
                    biased;
                    _ = crate::operation::signalled(&mut task.shutdown) => break,
                    opened = opener.open() => opened,
                };
                match opened {
                    Ok(opened) => {
                        retry_at = None;
                        Some(session.insert(opened))
                    }
                    Err(error) => {
                        retry_at = Some(Instant::now() + CATALOG_RETRY);
                        task.events
                            .send(Event::OperationFailed {
                                connection: task.id,
                                error,
                            })
                            .await;
                        None
                    }
                }
            }
        };
        let current = match current {
            Some(current) => current,
            None => {
                if let Some(actor) = task.fallback.upgrade() {
                    let request = Request {
                        command: Command::Catalog(read),
                    };
                    if actor.send(request).await.is_err() {
                        break;
                    }
                }
                continue;
            }
        };
        let error = catalog_read(
            &mut **current,
            task.id,
            read,
            &mut task.shutdown,
            &task.events,
        )
        .await;
        if error.is_some_and(|error| {
            matches!(error.kind, ErrorKind::Disconnected | ErrorKind::Connection)
        }) && let Some(mut lost) = session.take()
        {
            let _ = tokio::time::timeout(task.grace, lost.close()).await;
        }
    }
    if let Some(mut current) = session {
        let _ = tokio::time::timeout(task.grace, current.close()).await;
    }
}
