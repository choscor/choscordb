//! Test relay that counts protocol round trips to one loopback database.
//!
//! A turnaround is a change of direction to client->server. The connection starts in the
//! server direction, so each request that waits for a response counts once. Requests that a
//! client pipelines without a response between them count once together. The relay adds no
//! delay.
use std::sync::{Arc, Mutex};
use tokio::{
    io::{AsyncReadExt, AsyncWriteExt},
    net::{TcpListener, TcpStream, tcp::OwnedReadHalf, tcp::OwnedWriteHalf},
    task::JoinHandle,
};

#[derive(Default)]
struct Counts {
    accepted: usize,
    // One entry for each accepted connection, in accept order.
    turnarounds: Vec<usize>,
    client_sent_last: Vec<bool>,
}

pub struct RoundTripProxy {
    pub port: u16,
    counts: Arc<Mutex<Counts>>,
    worker: JoinHandle<()>,
}

impl RoundTripProxy {
    pub async fn start(target_host: &str, target_port: u16) -> Self {
        let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
        let port = listener.local_addr().unwrap().port();
        let counts = Arc::new(Mutex::new(Counts::default()));
        let shared = counts.clone();
        let target = format!("{target_host}:{target_port}");
        let worker = tokio::spawn(async move {
            loop {
                let (client, _) = listener.accept().await.unwrap();
                let server = TcpStream::connect(&target).await.unwrap();
                client.set_nodelay(true).unwrap();
                server.set_nodelay(true).unwrap();
                let index = {
                    let mut counts = shared.lock().unwrap();
                    counts.accepted += 1;
                    counts.turnarounds.push(0);
                    counts.client_sent_last.push(false);
                    counts.turnarounds.len() - 1
                };
                let (client_read, client_write) = client.into_split();
                let (server_read, server_write) = server.into_split();
                tokio::spawn(relay(
                    client_read,
                    server_write,
                    shared.clone(),
                    index,
                    true,
                ));
                tokio::spawn(relay(
                    server_read,
                    client_write,
                    shared.clone(),
                    index,
                    false,
                ));
            }
        });
        Self {
            port,
            counts,
            worker,
        }
    }

    /// Turnarounds of all connections.
    pub fn turnarounds(&self) -> usize {
        self.counts.lock().unwrap().turnarounds.iter().sum()
    }

    /// Turnarounds of each accepted connection, in accept order.
    pub fn connection_turnarounds(&self) -> Vec<usize> {
        self.counts.lock().unwrap().turnarounds.clone()
    }

    /// Connections accepted since the start or the last reset.
    pub fn accepted(&self) -> usize {
        self.counts.lock().unwrap().accepted
    }

    /// Sets all counts to zero. Open connections continue to count.
    pub fn reset(&self) {
        let mut counts = self.counts.lock().unwrap();
        counts.accepted = 0;
        counts.turnarounds.iter_mut().for_each(|count| *count = 0);
    }
}

impl Drop for RoundTripProxy {
    fn drop(&mut self) {
        self.worker.abort();
    }
}

async fn relay(
    mut from: OwnedReadHalf,
    mut to: OwnedWriteHalf,
    counts: Arc<Mutex<Counts>>,
    index: usize,
    from_client: bool,
) {
    let mut buffer = vec![0; 64 * 1024];
    loop {
        let read = match from.read(&mut buffer).await {
            Ok(0) | Err(_) => break,
            Ok(read) => read,
        };
        {
            // The count changes before the bytes go on, so a response is always counted
            // before the client can read it and send the next request.
            let mut counts = counts.lock().unwrap();
            if from_client && !counts.client_sent_last[index] {
                counts.turnarounds[index] += 1;
            }
            counts.client_sent_last[index] = from_client;
        }
        if to.write_all(&buffer[..read]).await.is_err() {
            break;
        }
    }
    let _ = to.shutdown().await;
}
