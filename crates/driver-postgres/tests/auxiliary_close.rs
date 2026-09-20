use choscordb_driver_api::*;
use choscordb_driver_postgres::PostgresDriver;
fn settings() -> ConnectionOptions {
    let config: tokio_postgres::Config = std::env::var("CHOSCORDB_TEST_POSTGRES")
        .expect("CHOSCORDB_TEST_POSTGRES must name a disposable fixture")
        .parse()
        .unwrap();
    let fixture_host = config.get_hosts().first().expect("fixture TCP host");
    #[cfg(unix)]
    let host = match fixture_host {
        tokio_postgres::config::Host::Tcp(host) => host.clone(),
        tokio_postgres::config::Host::Unix(_) => panic!("fixture requires TCP"),
    };
    #[cfg(not(unix))]
    let host = {
        let tokio_postgres::config::Host::Tcp(host) = fixture_host;
        host.clone()
    };
    ConnectionOptions::Postgres {
        host,
        port: config.get_ports().first().copied().unwrap_or(5432),
        database: config.get_dbname().unwrap_or("postgres").into(),
        user: config.get_user().expect("fixture user").into(),
        password: config
            .get_password()
            .map(|value| Secret::new(std::str::from_utf8(value).unwrap())),
        tls: if std::env::var_os("CHOSCORDB_TEST_POSTGRES_ROOT_CERTIFICATE").is_some() {
            TlsMode::VerifyFull
        } else {
            TlsMode::Disable
        },
        ssh: None,
        root_certificate: std::env::var_os("CHOSCORDB_TEST_POSTGRES_ROOT_CERTIFICATE")
            .map(Into::into),
    }
}

async fn run(connection: &mut dyn Connection, sql: &str, auto: bool) -> Vec<Row> {
    let mut cursor = connection
        .execute(
            sql,
            QueryOptions {
                auto_commit: auto,
                ..Default::default()
            },
        )
        .await
        .unwrap();
    let mut rows = Vec::new();
    loop {
        let page = cursor
            .fetch_page(PageSize::new(100).unwrap())
            .await
            .unwrap();
        rows.extend(page.rows);
        if !page.has_more {
            break;
        }
    }
    cursor.close().await.unwrap();
    rows
}
#[tokio::test]
#[ignore = "requires exclusive disposable CHOSCORDB_TEST_POSTGRES fixture"]
async fn close_interrupts_pending_auxiliary_requests_and_rolls_back() {
    let mut config: tokio_postgres::Config = std::env::var("CHOSCORDB_TEST_POSTGRES")
        .unwrap()
        .parse()
        .unwrap();
    config.ssl_mode(tokio_postgres::config::SslMode::Disable);
    let (admin, transport) = config.connect(tokio_postgres::NoTls).await.unwrap();
    let transport = tokio::spawn(async move {
        let _ = transport.await;
    });
    let table = format!("choscordb_aux_close_{}", std::process::id());
    admin
        .batch_execute(&format!(
            "DROP TABLE IF EXISTS {table}; CREATE TABLE {table}(value int)"
        ))
        .await
        .unwrap();
    let database: u32 = admin
        .query_one(
            "SELECT oid FROM pg_database WHERE datname=current_database()",
            &[],
        )
        .await
        .unwrap()
        .get(0);
    let schema: u32 = admin
        .query_one("SELECT oid FROM pg_namespace WHERE nspname='public'", &[])
        .await
        .unwrap()
        .get(0);
    let blocked = admin
        .prepare(
            "SELECT EXISTS(SELECT 1 FROM pg_stat_activity WHERE pid=$1 AND wait_event_type='Lock')",
        )
        .await
        .unwrap();
    let mut outcomes = Vec::new();
    for (ddl, automatic) in [(false, false), (true, false), (false, true), (true, true)] {
        let mut connection = PostgresDriver.connect(settings()).await.unwrap();
        let pid = match &run(&mut *connection, "SELECT pg_backend_pid()", true).await[0][0] {
            Value::Integer(pid) => *pid as i32,
            _ => panic!("pid"),
        };
        let retained = if automatic {
            Some(
                connection
                    .execute(
                        &format!("INSERT INTO {table} VALUES(1),(2) RETURNING value"),
                        QueryOptions::default(),
                    )
                    .await
                    .unwrap(),
            )
        } else {
            run(
                &mut *connection,
                &format!("INSERT INTO {table} VALUES(1)"),
                false,
            )
            .await;
            None
        };
        admin.batch_execute("BEGIN; SET LOCAL statement_timeout='3s'; LOCK pg_catalog.pg_namespace IN ACCESS EXCLUSIVE MODE").await.unwrap();
        let observed = {
            let request = async {
                if ddl {
                    connection
                        .object_ddl(&ObjectId(format!("pg:schema:{schema}")))
                        .await
                        .map(|_| ())
                } else {
                    connection
                        .load_metadata(Some(ObjectId(format!("pg:database:{database}"))))
                        .await
                        .map(|_| ())
                }
            };
            tokio::pin!(request);
            tokio::select! {
                result = &mut request => panic!("auxiliary request completed before lock observation: {result:?}"),
                result = tokio::time::timeout(std::time::Duration::from_secs(2), async {
                    loop {
                        if admin.query_one(&blocked,&[&pid]).await.unwrap().get::<_,bool>(0) { break; }
                        tokio::task::yield_now().await;
                    }
                }) => result.is_ok()
            }
        }; // Drop only the caller future; the adapter worker is still blocked.
        let closed =
            tokio::time::timeout(std::time::Duration::from_millis(750), connection.close()).await;
        admin.batch_execute("ROLLBACK").await.unwrap(); // Always release catalog lock before asserting.
        drop(connection);
        drop(retained);
        let backend_gone = tokio::time::timeout(std::time::Duration::from_secs(2), async {
            loop {
                let alive: bool = admin
                    .query_one(
                        "SELECT EXISTS(SELECT 1 FROM pg_stat_activity WHERE pid=$1)",
                        &[&pid],
                    )
                    .await
                    .unwrap()
                    .get(0);
                if !alive {
                    break;
                }
                tokio::task::yield_now().await;
            }
        })
        .await
        .is_ok();
        let count: i64 = admin
            .query_one(&format!("SELECT count(*) FROM {table}"), &[])
            .await
            .unwrap()
            .get(0);
        outcomes.push((
            observed,
            closed.is_ok_and(|r| r.is_ok()),
            backend_gone,
            count,
        ));
    }
    admin
        .batch_execute(&format!("DROP TABLE {table}"))
        .await
        .unwrap();
    transport.abort();
    for outcome in outcomes {
        assert_eq!(
            outcome,
            (true, true, true, 0),
            "pending metadata/DDL close must terminate transport and roll back"
        );
    }
}
