use choscordb_driver_api::Result;
use mysql_async::{Conn, Opts, OptsBuilder};
/// A partially consumed result cannot use mysql_async's default Drop, which
/// spawns an unbounded drain. Consuming disconnect sets its disconnected flag
/// before any await; poll it once, then drop its stream even if draining rows
/// or writing QUIT is pending. Never await that drain or spawn cleanup work.
pub(super) struct PendingConnection(Option<Conn>, pub(super) super::idle::Tracker);
impl PendingConnection {
    pub(super) fn new(connection: Conn) -> Self {
        Self(Some(connection), Default::default())
    }
    pub(super) fn connection(&mut self) -> &mut Conn {
        self.0.as_mut().expect("owned connection")
    }
    pub(super) fn ready(mut self) -> Conn {
        self.0.take().expect("owned connection")
    }
}
impl Drop for PendingConnection {
    fn drop(&mut self) {
        use std::{
            future::Future,
            task::{Context, Waker},
        };
        if let Some(connection) = self.0.take() {
            let mut disconnect = Box::pin(connection.disconnect());
            let _ = disconnect
                .as_mut()
                .poll(&mut Context::from_waker(Waker::noop()));
        }
    }
}

/// Disable native startup SQL so cancellation never hands a pending result to
/// mysql_async's unbounded Drop cleanup before we own its connection.
pub(super) async fn connect(options: Opts) -> Result<PendingConnection> {
    let raw = OptsBuilder::from_opts(options.clone())
        .init(Vec::<String>::new())
        .setup(Vec::<String>::new())
        // This is only an idle-expiry hint for pools. We use direct connections;
        // querying the server for this otherwise-unused hint can itself hang.
        .wait_timeout(Some(28_800));
    Ok(PendingConnection::new(
        Conn::new(raw).await.map_err(super::normalize)?,
    ))
}
