//! Route raw controls through the same native ownership paths as explicit UI actions.
use super::*;
pub(super) async fn execute(
    connection: &mut PostgresConnection,
    sql: &str,
    options: &QueryOptions,
    max: usize,
) -> Result<Option<Box<dyn ResultCursor>>> {
    let control = postgres_transaction_control(sql)?;
    if matches!(control, PostgresTransactionControl::Sql) {
        return Ok(None);
    }
    if max < std::mem::size_of::<Vec<Column>>() {
        return Err(DriverError::new(
            ErrorKind::ResourceLimit,
            "Result schema exceeds memory budget",
        ));
    }
    let timeout = options.timeout.unwrap_or(connection.cancel.connect_timeout);
    let result = tokio::time::timeout(timeout, async {
        worker::begin_generation(&connection.cancel).await?;
        worker::running(&connection.cancel, true).await?;
        let outcome = async {
            match control {
                PostgresTransactionControl::Begin(characteristics) => {
                    connection.begin_transaction(characteristics).await?
                }
                PostgresTransactionControl::Commit(true) => {
                    connection.chain_transaction(true).await?
                }
                PostgresTransactionControl::Rollback(true) => {
                    connection.chain_transaction(false).await?
                }
                PostgresTransactionControl::Commit(false) => connection.commit().await?,
                PostgresTransactionControl::Rollback(false) => connection.rollback().await?,
                PostgresTransactionControl::Sql => unreachable!(),
            }
            connection.transaction_state().await
        }
        .await;
        let _ = worker::running(&connection.cancel, false).await;
        let state = connection.cancel.state.lock().await;
        if state.cancelled == Some(state.current) {
            return Err(DriverError::new(
                ErrorKind::Cancelled,
                "Transaction control cancelled",
            ));
        }
        outcome
    })
    .await;
    let active = match result {
        Ok(result) => result?,
        Err(_) => {
            // A dropped control future may otherwise still own a native operation.
            // Closing is sticky and aborts its pump; never reuse an ambiguous session.
            connection.cancel.closing.request();
            let _ = tokio::time::timeout(
                std::time::Duration::from_millis(100),
                connection.client.request(Command::Close),
            )
            .await;
            return Err(DriverError::new(
                ErrorKind::Timeout,
                "Transaction control timed out; connection closed",
            ));
        }
    };
    Ok(Some(Box::new(Completed {
        summary: QuerySummary {
            transaction_active: active,
            affected_rows: Some(0),
            ..Default::default()
        },
    })))
}
struct Completed {
    summary: QuerySummary,
}
#[async_trait]
impl ResultCursor for Completed {
    fn columns(&self) -> &[Column] {
        &[]
    }
    async fn fetch_page(&mut self, _: PageSize) -> Result<ResultPage> {
        Ok(ResultPage {
            index: 0,
            rows: vec![],
            has_more: false,
        })
    }
    fn retained_bytes_after_completion(&self) -> Option<usize> {
        Some(std::mem::size_of::<Self>())
    }
    fn summary(&self) -> QuerySummary {
        self.summary.clone()
    }
    async fn close(&mut self) -> Result<()> {
        Ok(())
    }
}
