#[path = "support/round_trip_proxy.rs"]
mod round_trip_proxy;

use round_trip_proxy::RoundTripProxy;
use tokio::{
    io::{AsyncReadExt, AsyncWriteExt},
    net::{TcpListener, TcpStream},
};

// Answers each line with the same line.
async fn echo_server() -> u16 {
    let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let port = listener.local_addr().unwrap().port();
    tokio::spawn(async move {
        loop {
            let (mut socket, _) = listener.accept().await.unwrap();
            tokio::spawn(async move {
                let mut buffer = [0; 1024];
                loop {
                    match socket.read(&mut buffer).await {
                        Ok(0) | Err(_) => return,
                        Ok(read) => socket.write_all(&buffer[..read]).await.unwrap(),
                    }
                }
            });
        }
    });
    port
}

async fn exchange(client: &mut TcpStream, request: &[u8]) {
    client.write_all(request).await.unwrap();
    let mut response = vec![0; request.len()];
    client.read_exact(&mut response).await.unwrap();
    assert_eq!(response, request);
}

#[tokio::test]
async fn counts_one_turnaround_for_each_request_and_response_exchange() {
    let proxy = RoundTripProxy::start("127.0.0.1", echo_server().await).await;
    let mut first = TcpStream::connect(("127.0.0.1", proxy.port)).await.unwrap();
    for _ in 0..5 {
        exchange(&mut first, b"ping\n").await;
    }
    assert_eq!(proxy.turnarounds(), 5);
    assert_eq!(proxy.accepted(), 1);

    // Two requests in one write, before the response, count as one turnaround.
    exchange(&mut first, b"one\ntwo\n").await;
    assert_eq!(proxy.turnarounds(), 6);

    let mut second = TcpStream::connect(("127.0.0.1", proxy.port)).await.unwrap();
    for _ in 0..3 {
        exchange(&mut second, b"pong\n").await;
    }
    assert_eq!(proxy.connection_turnarounds(), vec![6, 3]);
    assert_eq!(proxy.turnarounds(), 9);
    assert_eq!(proxy.accepted(), 2);

    proxy.reset();
    assert_eq!(proxy.turnarounds(), 0);
    assert_eq!(proxy.accepted(), 0);
    exchange(&mut second, b"again\n").await;
    assert_eq!(proxy.connection_turnarounds(), vec![0, 1]);
}
