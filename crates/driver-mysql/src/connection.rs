use choscordb_driver_api::Result;
use mysql_async::{Conn, DriverError as MysqlDriverError, Error as MysqlError, Opts, OptsBuilder};
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

pub(super) async fn connect(options: Opts, prefer_tls: bool) -> Result<PendingConnection> {
    if !prefer_tls {
        return connect_raw(options).await.map_err(super::normalize);
    }
    match connect_raw(options.clone()).await {
        Ok(connection) => Ok(connection),
        Err(MysqlError::Driver(MysqlDriverError::NoClientSslFlagFromServer)) => {
            let plain: Opts = OptsBuilder::from_opts(options).ssl_opts(None).into();
            connect_raw(plain).await.map_err(super::normalize)
        }
        Err(error) => Err(super::normalize(error)),
    }
}

async fn connect_raw(options: Opts) -> mysql_async::Result<PendingConnection> {
    let raw = OptsBuilder::from_opts(options.clone())
        .init(Vec::<String>::new())
        .setup(Vec::<String>::new())
        .wait_timeout(Some(28_800));
    Ok(PendingConnection::new(Conn::new(raw).await?))
}
