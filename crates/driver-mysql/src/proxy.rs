//! Loopback transport shim retaining the database hostname for native TLS.
use choscordb_driver_api::{DriverError, ErrorKind, Result, Secret, SocksProxy, connect_socks};
use std::{sync::Arc, time::Duration};
use tokio::{
    net::TcpListener,
    task::{JoinHandle, JoinSet},
};
pub(crate) struct Tunnel {
    port: u16,
    worker: JoinHandle<()>,
}
impl Tunnel {
    pub(crate) fn port(&self) -> u16 {
        self.port
    }
    pub(crate) async fn open(
        settings: SocksProxy,
        secret: Option<Secret>,
        host: String,
        port: u16,
        timeout: Duration,
    ) -> Result<Arc<Self>> {
        // Establish one transport before creating the local relay so proxy errors
        // (notably authentication) retain their public classification.
        let first = connect_socks(&settings, secret.as_ref(), &host, port, timeout).await?;
        let listener = TcpListener::bind((std::net::Ipv4Addr::LOCALHOST, 0))
            .await
            .map_err(|_| {
                DriverError::new(ErrorKind::Connection, "Cannot bind MySQL proxy relay")
            })?;
        let local_port = listener
            .local_addr()
            .map_err(|_| {
                DriverError::new(ErrorKind::Connection, "Cannot inspect MySQL proxy relay")
            })?
            .port();
        let secret = secret.map(Arc::new);
        let worker = tokio::spawn(async move {
            let mut first = Some(first);
            let mut clients = JoinSet::new();
            loop {
                tokio::select! {
                    accepted=listener.accept()=>{
                        let Ok((mut socket,_))=accepted else {break};
                        if clients.len()>=32 {drop(socket);continue;}
                        let ready=first.take();let settings=settings.clone();let secret=secret.clone();let host=host.clone();
                        clients.spawn(async move {
                            let mut remote=if let Some(ready)=ready {ready} else {
                                let connecting=connect_socks(&settings,secret.as_deref(),&host,port,timeout);
                                tokio::pin!(connecting);
                                let mut byte=[0];
                                let result=tokio::select! {
                                    result=&mut connecting=>result,
                                    ready=socket.peek(&mut byte)=>{
                                        if !matches!(ready,Ok(n) if n>0) {return;}
                                        connecting.await
                                    },
                                };
                                let Ok(remote)=result else {return};remote
                            };
                            let _=tokio::io::copy_bidirectional(&mut socket,&mut remote).await;
                        });
                    },
                    _=clients.join_next(),if !clients.is_empty()=>{},
                }
            }
        });
        Ok(Arc::new(Self {
            port: local_port,
            worker,
        }))
    }
}
impl Drop for Tunnel {
    fn drop(&mut self) {
        self.worker.abort();
    }
}
