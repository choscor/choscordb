use crate::{Event, QueryState};
use choscordb_driver_api::*;
use std::{future::Future, sync::Arc, time::Duration};
use tokio::{
    sync::{mpsc, watch},
    time::Instant,
};

pub(crate) struct Control<'a> {
    pub query: QueryId,
    pub cancellation: &'a mut watch::Receiver<bool>,
    pub shutdown: &'a mut watch::Receiver<bool>,
    pub deadline: Option<Instant>,
    pub grace: Duration,
    pub events: &'a mpsc::Sender<Event>,
}
pub(crate) struct Outcome<T> {
    pub result: Result<T>,
    pub poisoned: bool,
}
pub(crate) async fn signalled(receiver: &mut watch::Receiver<bool>) {
    loop {
        if *receiver.borrow_and_update() {
            return;
        }
        if receiver.changed().await.is_err() {
            std::future::pending::<()>().await;
        }
    }
}
pub(crate) async fn perform<T>(
    future: impl Future<Output = Result<T>>,
    cancel: Arc<dyn CancelHandle>,
    control: Control<'_>,
) -> Outcome<T> {
    // Do not poll an operation that was cancelled or expired before it began:
    // polling it merely to settle cancellation could itself start a write.
    let preflight = if *control.shutdown.borrow() {
        Some(ErrorKind::Disconnected)
    } else if *control.cancellation.borrow() {
        Some(ErrorKind::Cancelled)
    } else if control
        .deadline
        .is_some_and(|deadline| deadline <= Instant::now())
    {
        Some(ErrorKind::Timeout)
    } else {
        None
    };
    if let Some(reason) = preflight {
        return Outcome {
            result: Err(DriverError::new(
                reason,
                "Operation stopped before execution",
            )),
            poisoned: false,
        };
    }
    tokio::pin!(future);
    let deadline = async {
        match control.deadline {
            Some(at) => tokio::time::sleep_until(at).await,
            None => std::future::pending::<()>().await,
        }
    };
    let reason = tokio::select! {
        biased;
        _ = signalled(control.shutdown) => ErrorKind::Disconnected,
        _ = signalled(control.cancellation) => ErrorKind::Cancelled,
        _ = deadline => ErrorKind::Timeout,
        result = &mut future => return Outcome { result, poisoned: false },
    };
    // Poll cancellation and work together. Do not start any later command until both
    // have settled. A stuck adapter poisons this connection instead of risking a write retry.
    let settlement = tokio::time::timeout(control.grace, async {
        let (cancel_result, work_result) = tokio::join!(cancel.cancel(), &mut future);
        drop(work_result);
        cancel_result
    });
    let notice = control.events.send(Event::QueryState {
        query: control.query,
        state: QueryState::Cancelling,
    });
    let (settled, _) = tokio::join!(settlement, notice);
    let poisoned = !matches!(settled, Ok(Ok(())));
    Outcome {
        result: Err(DriverError::new(
            reason,
            match reason {
                ErrorKind::Timeout => "Query timed out",
                ErrorKind::Disconnected => "Engine shutting down",
                _ => "Query cancelled",
            },
        )),
        poisoned,
    }
}

/// Capacity waits have no database operation to settle. Never poll execute merely
/// to cancel a query which has not acquired its source reservation.
pub(crate) async fn wait_capacity<T>(
    future: impl Future<Output = T>,
    cancellation: Option<&mut watch::Receiver<bool>>,
    shutdown: &mut watch::Receiver<bool>,
    deadline: Option<Instant>,
) -> Result<T> {
    let cancelled = async {
        if let Some(receiver) = cancellation {
            signalled(receiver).await;
        } else {
            std::future::pending::<()>().await;
        }
    };
    let expired = async {
        if let Some(at) = deadline {
            tokio::time::sleep_until(at).await;
        } else {
            std::future::pending::<()>().await;
        }
    };
    let kind = tokio::select! {
        biased;
        _ = signalled(shutdown) => ErrorKind::Disconnected,
        _ = cancelled => ErrorKind::Cancelled,
        _ = expired => ErrorKind::Timeout,
        value = future => return Ok(value),
    };
    Err(DriverError::new(kind, "Page memory wait stopped"))
}
