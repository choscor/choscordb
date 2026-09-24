//! Controlled test proxy forwards only to one disposable loopback database.
use choscordb_driver_api::{SocksProtocol, SocksProxy};
use std::sync::{
    Arc,
    atomic::{AtomicUsize, Ordering},
};
use tokio::{
    io::{AsyncReadExt, AsyncWriteExt},
    net::{TcpListener, TcpStream},
    task::{JoinHandle, JoinSet},
};
pub async fn start(target_port: u16) -> (SocksProxy, JoinHandle<()>, Arc<AtomicUsize>) {
    start_for_host(target_port, "localhost").await
}
pub async fn start_for_host(
    target_port: u16,
    expected_host: &'static str,
) -> (SocksProxy, JoinHandle<()>, Arc<AtomicUsize>) {
    let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let port = listener.local_addr().unwrap().port();
    let count = Arc::new(AtomicUsize::new(0));
    let counted = count.clone();
    let worker = tokio::spawn(async move {
        let mut clients = JoinSet::new();
        loop {
            tokio::select! {
                accepted=listener.accept()=>{
                    let (mut client,_)=accepted.unwrap();let counted=counted.clone();
                    clients.spawn(async move {
                        let mut greeting=[0;3];client.read_exact(&mut greeting).await.unwrap();assert_eq!(greeting,[5,1,2]);client.write_all(&[5,2]).await.unwrap();
                        let mut credentials=[0;14];client.read_exact(&mut credentials).await.unwrap();assert_eq!(&credentials,b"\x01\x05alice\x06secret");client.write_all(&[1,0]).await.unwrap();
                        let mut header=[0;5];client.read_exact(&mut header).await.unwrap();assert_eq!(&header[..4],[5,1,0,3]);
                        let mut domain=vec![0;usize::from(header[4])];client.read_exact(&mut domain).await.unwrap();assert_eq!(&domain,expected_host.as_bytes());
                        assert_eq!(client.read_u16().await.unwrap(),target_port);
                        let mut remote=TcpStream::connect((std::net::Ipv4Addr::LOCALHOST,target_port)).await.unwrap();
                        client.write_all(&[5,0,0,1,127,0,0,1,0,0]).await.unwrap();counted.fetch_add(1,Ordering::SeqCst);
                        let _=tokio::io::copy_bidirectional(&mut client,&mut remote).await;
                    });
                },
                done=clients.join_next(),if !clients.is_empty()=>{done.unwrap().unwrap();},
            }
        }
    });
    (
        SocksProxy {
            protocol: SocksProtocol::Socks5,
            host: "127.0.0.1".into(),
            port,
            username: Some("alice".into()),
        },
        worker,
        count,
    )
}
