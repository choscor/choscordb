//! Bounded local listeners owned by the database connections that requested them.
use crate::{DriverError, ErrorKind, Result, Secret, SshForward, SshTunnel};
use std::{
    collections::{BTreeMap, HashMap},
    net::{IpAddr, Ipv4Addr, Ipv6Addr, SocketAddr},
    sync::{Arc, Mutex, OnceLock, Weak},
};
type Slot = tokio::sync::Mutex<Weak<SshLocalForward>>;
static POOL: OnceLock<Mutex<HashMap<[u8; 32], Weak<Slot>>>> = OnceLock::new();
static LISTENERS: tokio::sync::Semaphore = tokio::sync::Semaphore::const_new(128);
pub(crate) fn invalidate(key: [u8; 32]) -> Result<()> {
    if let Some(pool) = POOL.get() {
        pool.lock()
            .map_err(|_| DriverError::new(ErrorKind::Internal, "SSH listener registry failed"))?
            .remove(&key);
    }
    Ok(())
}
pub struct SshLocalForward {
    address: SocketAddr,
    _admission: Option<tokio::sync::SemaphorePermit<'static>>,
    worker: tokio::task::JoinHandle<()>,
    _slot: Option<Arc<Slot>>,
}
impl SshLocalForward {
    pub fn address(&self) -> SocketAddr {
        self.address
    }
    pub fn port(&self) -> u16 {
        self.address.port()
    }
    /// Address the owning application can connect to when a wildcard bind was
    /// explicitly selected. The listener itself retains the requested address.
    pub fn connect_address(&self) -> SocketAddr {
        SocketAddr::new(
            match self.address.ip() {
                IpAddr::V4(ip) if ip.is_unspecified() => IpAddr::V4(Ipv4Addr::LOCALHOST),
                IpAddr::V6(ip) if ip.is_unspecified() => IpAddr::V6(Ipv6Addr::LOCALHOST),
                ip => ip,
            },
            self.address.port(),
        )
    }
    pub async fn open(
        settings: &SshTunnel,
        secret: Option<&Secret>,
        hops: &BTreeMap<String, Secret>,
        private_key: Option<&Secret>,
        hop_keys: &BTreeMap<String, Secret>,
        host: &str,
        port: u16,
    ) -> Result<Arc<Self>> {
        crate::validate_ssh_chain_authentication_with_keys(
            settings,
            secret,
            hops,
            private_key,
            hop_keys,
        )?;
        settings.options.forwarding_destination(host, port)?;
        let slot = if settings.options.share_tunnels {
            let key = crate::ssh_context::key(
                settings,
                secret,
                hops,
                private_key,
                hop_keys,
                "listener",
                &[host.as_bytes().to_vec(), port.to_le_bytes().to_vec()],
            )?;
            let mut pool = POOL.get_or_init(Default::default).lock().map_err(|_| {
                DriverError::new(ErrorKind::Internal, "SSH listener registry failed")
            })?;
            pool.retain(|_, slot| slot.strong_count() > 0);
            Some(if let Some(slot) = pool.get(&key).and_then(Weak::upgrade) {
                slot
            } else {
                if pool.len() >= 128 {
                    return Err(DriverError::new(
                        ErrorKind::ResourceLimit,
                        "SSH local listener limit reached",
                    ));
                }
                let slot = Arc::new(tokio::sync::Mutex::new(Weak::new()));
                pool.insert(key, Arc::downgrade(&slot));
                slot
            })
        } else {
            None
        };
        let mut owner = match &slot {
            Some(slot) => Some(slot.lock().await),
            None => None,
        };
        if let Some(current) = owner.as_ref().and_then(|owner| owner.upgrade()) {
            return Ok(current);
        }
        let admission = if slot.is_some() {
            Some(LISTENERS.try_acquire().map_err(|_| {
                DriverError::new(ErrorKind::ResourceLimit, "SSH local listener limit reached")
            })?)
        } else {
            None
        };
        let listener = tokio::net::TcpListener::bind(settings.options.local_address()?)
            .await
            .map_err(|_| {
                DriverError::new(
                    ErrorKind::Connection,
                    "Cannot bind selected SSH local address",
                )
            })?;
        let address = listener.local_addr().map_err(|_| {
            DriverError::new(ErrorKind::Connection, "Cannot inspect SSH local listener")
        })?;
        let settings = settings.clone();
        let host = host.to_owned();
        let secret = secret.map(|value| Arc::new(Secret::new(value.expose())));
        let private_key = private_key.map(|value| Arc::new(Secret::new(value.expose())));
        let copy = |values: &BTreeMap<String, Secret>| {
            Arc::new(
                values
                    .iter()
                    .map(|(id, value)| (id.clone(), Secret::new(value.expose())))
                    .collect::<BTreeMap<_, _>>(),
            )
        };
        let hops = copy(hops);
        let hop_keys = copy(hop_keys);
        let worker = tokio::spawn(async move {
            let mut clients = tokio::task::JoinSet::new();
            loop {
                tokio::select! {
                    accepted=listener.accept()=>{
                        let Ok((socket,_))=accepted else{break;};
                        if clients.len()>=32{drop(socket);continue;}
                        let settings=settings.clone();let host=host.clone();let secret=secret.clone();let private_key=private_key.clone();let hops=hops.clone();let hop_keys=hop_keys.clone();
                        clients.spawn(async move{
                            let prepare=SshForward::open_channel_with_keys(&settings,secret.as_deref(),&hops,private_key.as_deref(),&hop_keys,&host,port);
                            tokio::pin!(prepare);let mut peek=[0];
                            let result=tokio::select!{
                                result=&mut prepare=>result,
                                ready=socket.peek(&mut peek)=>{if !matches!(ready,Ok(n)if n>0){return;}prepare.await}
                            };
                            let Ok(stream)=result else{return;};
                            let(mut a,mut b)=socket.into_split();let(mut c,mut d)=tokio::io::split(stream);
                            tokio::select!{_=tokio::io::copy(&mut a,&mut d)=>{},_=tokio::io::copy(&mut c,&mut b)=>{}}
                        });
                    },
                    _=clients.join_next(),if !clients.is_empty()=>{},
                }
            }
        });
        let forward = Arc::new(Self {
            address,
            _admission: admission,
            worker,
            _slot: slot.clone(),
        });
        if let Some(owner) = &mut owner {
            **owner = Arc::downgrade(&forward);
        }
        Ok(forward)
    }
}
impl Drop for SshLocalForward {
    fn drop(&mut self) {
        self.worker.abort();
    }
}
