use std::{
    io,
    pin::Pin,
    task::{Context, Poll},
};
use tokio::io::{AsyncRead, AsyncWrite, ReadBuf};

/// Reject malformed SSLRequest replies before the upstream Prefer policy can
/// interpret them as a server refusal and send a plaintext startup message.
pub(super) struct TlsResponseGuard<S> {
    inner: S,
    check_first_read: bool,
}

impl<S> TlsResponseGuard<S> {
    pub(super) fn new(inner: S, check_first_read: bool) -> Self {
        Self {
            inner,
            check_first_read,
        }
    }
}

impl<S: AsyncRead + Unpin> AsyncRead for TlsResponseGuard<S> {
    fn poll_read(
        mut self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        buf: &mut ReadBuf<'_>,
    ) -> Poll<io::Result<()>> {
        let before = buf.filled().len();
        match Pin::new(&mut self.inner).poll_read(cx, buf) {
            Poll::Ready(Ok(())) if self.check_first_read && buf.filled().len() > before => {
                self.check_first_read = false;
                if matches!(buf.filled()[before], b'S' | b'N') {
                    Poll::Ready(Ok(()))
                } else {
                    Poll::Ready(Err(io::Error::new(
                        io::ErrorKind::InvalidData,
                        "Invalid PostgreSQL TLS negotiation reply",
                    )))
                }
            }
            other => other,
        }
    }
}

impl<S: AsyncWrite + Unpin> AsyncWrite for TlsResponseGuard<S> {
    fn poll_write(
        mut self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        buf: &[u8],
    ) -> Poll<io::Result<usize>> {
        Pin::new(&mut self.inner).poll_write(cx, buf)
    }

    fn poll_flush(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<io::Result<()>> {
        Pin::new(&mut self.inner).poll_flush(cx)
    }

    fn poll_shutdown(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<io::Result<()>> {
        Pin::new(&mut self.inner).poll_shutdown(cx)
    }
}
