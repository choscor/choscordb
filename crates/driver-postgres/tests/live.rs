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
        ssh_jump_secrets: Default::default(),
        ssh_private_key: None,
        ssh_jump_private_keys: Default::default(),
        proxy: None,
        proxy_secret: None,
        host,
        port: config.get_ports().first().copied().unwrap_or(5432),
        database: config.get_dbname().unwrap_or("postgres").into(),
        user: config.get_user().expect("fixture user").into(),
        password: config
            .get_password()
            .map(|value| Secret::new(std::str::from_utf8(value).unwrap())),
        ssh_secret: None,
        tls: if std::env::var_os("CHOSCORDB_TEST_POSTGRES_ROOT_CERTIFICATE").is_some() {
            TlsMode::VerifyFull
        } else {
            TlsMode::Disable
        },
        ssh: std::env::var("CHOSCORDB_TEST_POSTGRES_SSH_HOST")
            .ok()
            .map(|host| SshTunnel {
                options: Default::default(),
                host,
                port: std::env::var("CHOSCORDB_TEST_POSTGRES_SSH_PORT")
                    .expect("SSH fixture port")
                    .parse()
                    .unwrap(),
                user: std::env::var("CHOSCORDB_TEST_POSTGRES_SSH_USER").expect("SSH fixture user"),
                authentication: SshAuthentication::PublicKey,
                identity_source: Default::default(),
                identity_file: std::env::var("CHOSCORDB_TEST_POSTGRES_SSH_IDENTITY").ok(),
            }),
        tls_identity: None,
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
#[ignore = "requires disposable live PostgreSQL fixture"]
async fn reviewed_batch_is_atomic_and_binds_values() {
    let mut c = PostgresDriver.connect(settings()).await.unwrap();
    run(&mut *c, "DROP TABLE IF EXISTS choscordb_edit_batch", true).await;
    run(
        &mut *c,
        "CREATE TABLE choscordb_edit_batch(id integer PRIMARY KEY, name text)",
        true,
    )
    .await;
    let oid = run(
        &mut *c,
        "SELECT 'choscordb_edit_batch'::regclass::oid::bigint",
        true,
    )
    .await;
    let Value::Integer(oid) = oid[0][0] else {
        panic!("expected relation oid")
    };
    let target = c
        .inspect_edit_target(&ObjectId(format!("pg:relation:{oid}")))
        .await
        .unwrap();
    assert!(target.qualified_name.ends_with(".\"choscordb_edit_batch\""));
    assert_eq!(target.key_columns, vec!["id"]);
    let query = c
        .inspect_edit_query(
            "SELECT id AS key, upper(name) AS label, name FROM choscordb_edit_batch WHERE id=1",
            vec!["key".into(), "label".into(), "name".into()],
        )
        .await
        .unwrap();
    assert!(query.reason.is_empty(), "{}", query.reason);
    assert_eq!(query.source_columns, vec!["id", "", "name"]);
    let joined = c
        .inspect_edit_query(
            "SELECT a.id FROM choscordb_edit_batch a JOIN choscordb_edit_batch b ON b.id=a.id",
            vec!["id".into()],
        )
        .await
        .unwrap();
    assert!(!joined.reason.is_empty());
    let insert = EditStatement {
        sql: "INSERT INTO choscordb_edit_batch(id,name) VALUES($1,$2)".into(),
        params: vec![Value::Integer(1), Value::Text("a'b".into())],
        expected_rows: None,
    };
    let stale = EditStatement {
        sql: "DELETE FROM choscordb_edit_batch WHERE id=$1 AND name=$2".into(),
        params: vec![Value::Integer(1), Value::Text("old".into())],
        expected_rows: Some(1),
    };
    assert!(
        c.apply_edit_batch(EditBatch {
            statements: vec![insert.clone(), stale]
        })
        .await
        .is_err()
    );
    assert!(
        run(&mut *c, "SELECT id FROM choscordb_edit_batch", true)
            .await
            .is_empty()
    );
    assert_eq!(
        c.apply_edit_batch(EditBatch {
            statements: vec![insert]
        })
        .await
        .unwrap()
        .affected_rows,
        vec![1]
    );
    assert_eq!(
        run(
            &mut *c,
            "SELECT name FROM choscordb_edit_batch WHERE id=1",
            true
        )
        .await,
        vec![vec![Value::Text("a'b".into())]]
    );
    run(&mut *c, "DROP TABLE choscordb_edit_batch", true).await;
}
#[tokio::test]
#[ignore = "requires disposable live PostgreSQL fixture"]
async fn query_inspection_preserves_active_paging_cursor() {
    let mut c = PostgresDriver.connect(settings()).await.unwrap();
    run(&mut *c, "DROP TABLE IF EXISTS choscordb_edit_paging", true).await;
    run(
        &mut *c,
        "CREATE TABLE choscordb_edit_paging(id integer PRIMARY KEY)",
        true,
    )
    .await;
    run(
        &mut *c,
        "INSERT INTO choscordb_edit_paging SELECT generate_series(1,250)",
        true,
    )
    .await;
    let sql = "SELECT id FROM choscordb_edit_paging ORDER BY id";
    let mut cursor = c.execute(sql, QueryOptions::default()).await.unwrap();
    let first = cursor
        .fetch_page(PageSize::new(100).unwrap())
        .await
        .unwrap();
    assert_eq!(first.rows[0][0], Value::Integer(1));
    assert!(first.has_more);
    let target = c.inspect_edit_query(sql, vec!["id".into()]).await.unwrap();
    assert!(target.reason.is_empty(), "{}", target.reason);
    let second = cursor
        .fetch_page(PageSize::new(100).unwrap())
        .await
        .unwrap();
    assert_eq!(second.rows[0][0], Value::Integer(101));
    cursor.close().await.unwrap();
    run(&mut *c, "DROP TABLE choscordb_edit_paging", true).await;
}
#[tokio::test]
#[ignore = "requires disposable live PostgreSQL fixture"]
async fn auto_commit_runs_commands_forbidden_in_transaction_blocks() {
    let mut connection = PostgresDriver.connect(settings()).await.unwrap();

    run(&mut *connection, "VACUUM", true).await;
    run(
        &mut *connection,
        "DROP TABLE IF EXISTS choscordb_concurrent_index",
        true,
    )
    .await;
    run(
        &mut *connection,
        "CREATE TABLE choscordb_concurrent_index(id integer)",
        true,
    )
    .await;
    run(
        &mut *connection,
        "CREATE INDEX CONCURRENTLY choscordb_concurrent_index_id ON choscordb_concurrent_index(id)",
        true,
    )
    .await;
    run(
        &mut *connection,
        "REINDEX INDEX CONCURRENTLY choscordb_concurrent_index_id",
        true,
    )
    .await;
    run(
        &mut *connection,
        "DROP INDEX CONCURRENTLY choscordb_concurrent_index_id",
        true,
    )
    .await;
    run(
        &mut *connection,
        "DROP TABLE choscordb_concurrent_index",
        true,
    )
    .await;
    run(
        &mut *connection,
        "DROP DATABASE IF EXISTS choscordb_direct_command WITH (FORCE)",
        true,
    )
    .await;
    run(
        &mut *connection,
        "CREATE DATABASE choscordb_direct_command",
        true,
    )
    .await;
    run(
        &mut *connection,
        "DROP DATABASE choscordb_direct_command WITH (FORCE)",
        true,
    )
    .await;

    let error = match connection
        .execute(
            "VACUUM",
            QueryOptions {
                auto_commit: false,
                ..Default::default()
            },
        )
        .await
    {
        Ok(_) => panic!("manual VACUUM unexpectedly succeeded"),
        Err(error) => error,
    };
    assert_eq!(error.vendor_code.as_deref(), Some("25001"));
    connection.rollback().await.unwrap();
    assert_eq!(
        run(&mut *connection, "SELECT 42", true).await,
        vec![vec![Value::Integer(42)]]
    );
    connection.close().await.unwrap();
}
#[tokio::test]
#[ignore = "requires disposable live PostgreSQL fixture"]
async fn server_side_connection_loss_is_terminal_and_a_fresh_connection_recovers() {
    let mut connection = PostgresDriver.connect(settings()).await.unwrap();
    let pid = match &run(&mut *connection, "SELECT pg_backend_pid()", true).await[0][0] {
        Value::Integer(pid) => *pid,
        _ => panic!("backend pid"),
    };
    let mut admin = PostgresDriver.connect(settings()).await.unwrap();
    assert_eq!(
        run(
            &mut *admin,
            &format!("SELECT pg_terminate_backend({pid})"),
            true,
        )
        .await,
        vec![vec![Value::Bool(true)]]
    );

    let error = match connection
        .execute("SELECT 1", QueryOptions::default())
        .await
    {
        Ok(_) => panic!("terminated connection unexpectedly recovered"),
        Err(error) => error,
    };
    assert_eq!(error.kind, ErrorKind::Disconnected);
    let second = connection
        .execute("SELECT 2", QueryOptions::default())
        .await
        .err()
        .expect("terminated connection must remain terminal");
    assert_eq!(second.kind, ErrorKind::Disconnected);

    let mut fresh = PostgresDriver.connect(settings()).await.unwrap();
    assert_eq!(
        run(&mut *fresh, "SELECT 3", true).await,
        vec![vec![Value::Integer(3)]]
    );
    fresh.close().await.unwrap();
    admin.close().await.unwrap();
}
#[tokio::test]
#[ignore = "requires disposable live PostgreSQL fixture"]
async fn pages_types_transactions_and_snapshot_values() {
    let mut connection = PostgresDriver.connect(settings()).await.unwrap();
    let mut cursor=connection.execute("SELECT i::bigint, i::numeric(30,8), ('héllo' || i)::text FROM generate_series(1,1001) AS i",QueryOptions::default()).await.unwrap();
    let mut count = 0;
    let mut index = 0;
    loop {
        let page = cursor
            .fetch_page_bounded(PageSize::new(100).unwrap(), 4096)
            .await
            .unwrap();
        assert_eq!(page.index, index);
        assert!(page.estimated_bytes() <= 4096);
        count += page.rows.len();
        index += 1;
        if !page.has_more {
            break;
        }
    }
    assert_eq!(count, 1001);
    cursor.close().await.unwrap();
    run(
        &mut *connection,
        "CREATE TEMP TABLE choscordb_tx (id integer)",
        true,
    )
    .await;
    run(
        &mut *connection,
        "INSERT INTO choscordb_tx VALUES (7)",
        false,
    )
    .await;
    connection.rollback().await.unwrap();
    assert_eq!(
        run(&mut *connection, "SELECT count(*) FROM choscordb_tx", true).await,
        vec![vec![Value::Integer(0)]]
    );
    run(
        &mut *connection,
        "INSERT INTO choscordb_tx VALUES (9)",
        false,
    )
    .await;
    connection.commit().await.unwrap();
    assert_eq!(
        run(&mut *connection, "SELECT id FROM choscordb_tx", true).await,
        vec![vec![Value::Integer(9)]]
    );
    let mut cursor = connection
        .execute(
            "SELECT repeat('é',100000), decode(repeat('ab',100000),'hex')",
            QueryOptions::default(),
        )
        .await
        .unwrap();
    let reader = cursor.deferred_reader().unwrap();
    let page = cursor
        .fetch_page_bounded(PageSize::new(100).unwrap(), 4096)
        .await
        .unwrap();
    assert!(!page.has_more);
    let Value::Deferred {
        handle,
        byte_length,
        ..
    } = page.rows[0][0]
    else {
        panic!("deferred text")
    };
    assert_eq!(byte_length, 200000);
    cursor.close().await.unwrap();
    drop(cursor);
    run(&mut *connection, "SELECT 42", true).await;
    connection.close().await.unwrap();
    let chunk = tokio::task::spawn_blocking(move || reader.read_chunk(handle, 199990, 20).unwrap())
        .await
        .unwrap();
    assert_eq!(chunk.bytes, "ééééé".as_bytes());
}
#[tokio::test]
#[ignore = "requires disposable live PostgreSQL fixture"]
async fn cancellation_and_single_statement_before_write() {
    let mut connection = PostgresDriver.connect(settings()).await.unwrap();
    run(
        &mut *connection,
        "CREATE TEMP TABLE choscordb_cancel (id integer)",
        true,
    )
    .await;
    assert!(
        connection
            .execute(
                "INSERT INTO choscordb_cancel VALUES(1); SELECT 2",
                QueryOptions::default()
            )
            .await
            .is_err()
    );
    assert_eq!(
        run(
            &mut *connection,
            "SELECT count(*) FROM choscordb_cancel",
            true
        )
        .await,
        vec![vec![Value::Integer(0)]]
    );
    let token = connection.cancellation_handle();
    let cancellation = tokio::spawn(async move {
        tokio::time::sleep(std::time::Duration::from_millis(100)).await;
        token.cancel().await.unwrap();
        token
    });
    let error = connection
        .execute("SELECT pg_sleep(30)", QueryOptions::default())
        .await
        .err()
        .expect("cancelled execute");
    assert_eq!(error.kind, ErrorKind::Cancelled);
    let old = cancellation.await.unwrap();
    let mut newer = connection
        .execute("SELECT 42::bigint", QueryOptions::default())
        .await
        .unwrap();
    old.cancel().await.unwrap();
    assert_eq!(
        newer.fetch_page(PageSize::default()).await.unwrap().rows,
        vec![vec![Value::Integer(42)]]
    );
    connection.close().await.unwrap();
}
#[tokio::test]
#[ignore = "requires disposable live PostgreSQL fixture and root certificate"]
async fn tls_identity_authentication_and_schema_preflight() {
    let mut good = settings();
    if let ConnectionOptions::Postgres {
        tls,
        root_certificate,
        ..
    } = &mut good
    {
        *tls = TlsMode::VerifyFull;
        *root_certificate = Some(
            std::env::var_os("CHOSCORDB_TEST_POSTGRES_ROOT_CERTIFICATE")
                .expect("root certificate")
                .into(),
        );
    }
    let mut connection = PostgresDriver.connect(good).await.unwrap();
    assert_eq!(
        run(&mut *connection, "SELECT 1::bigint", true).await,
        vec![vec![Value::Integer(1)]]
    );
    run(
        &mut *connection,
        "CREATE TEMP TABLE choscordb_schema (id integer)",
        true,
    )
    .await;
    let rejected = connection
        .execute_bounded(
            "INSERT INTO choscordb_schema VALUES (7) RETURNING id",
            QueryOptions::default(),
            1,
        )
        .await;
    assert!(matches!(
        rejected,
        Err(DriverError {
            kind: ErrorKind::ResourceLimit,
            ..
        })
    ));
    assert_eq!(
        run(
            &mut *connection,
            "SELECT count(*) FROM choscordb_schema",
            true
        )
        .await,
        vec![vec![Value::Integer(0)]]
    );
    assert!(matches!(
        connection
            .execute(
                "SELECT pg_sleep(30)",
                QueryOptions {
                    timeout: Some(std::time::Duration::from_millis(50)),
                    ..Default::default()
                }
            )
            .await,
        Err(DriverError {
            kind: ErrorKind::Timeout,
            ..
        })
    ));
    connection.close().await.unwrap();
    let mut wrong_host = settings();
    if let ConnectionOptions::Postgres { host, tls, .. } = &mut wrong_host {
        *host = "127.0.0.1".into();
        *tls = TlsMode::VerifyFull;
    }
    assert!(matches!(
        PostgresDriver.connect(wrong_host).await,
        Err(DriverError {
            kind: ErrorKind::Tls,
            ..
        })
    ));
    let mut untrusted = settings();
    if let ConnectionOptions::Postgres {
        tls,
        root_certificate,
        ..
    } = &mut untrusted
    {
        *tls = TlsMode::VerifyFull;
        *root_certificate = None;
    }
    assert!(matches!(
        PostgresDriver.connect(untrusted).await,
        Err(DriverError {
            kind: ErrorKind::Tls,
            ..
        })
    ));
    let mut wrong_password = settings();
    if let ConnectionOptions::Postgres { password, .. } = &mut wrong_password {
        *password = Some(Secret::new("wrong-password"));
    }
    assert!(matches!(
        PostgresDriver.connect(wrong_password).await,
        Err(DriverError {
            kind: ErrorKind::Authentication,
            ..
        })
    ));
}
#[tokio::test]
#[ignore = "requires disposable live PostgreSQL fixture"]
async fn timeout_is_distinct_from_user_cancellation() {
    let mut connection = PostgresDriver.connect(settings()).await.unwrap();
    let error = connection
        .execute(
            "SELECT pg_sleep(30)",
            QueryOptions {
                timeout: Some(std::time::Duration::from_millis(50)),
                ..Default::default()
            },
        )
        .await
        .err()
        .expect("timed out execute");
    assert_eq!(error.kind, ErrorKind::Timeout);
    connection.close().await.unwrap();
}
#[tokio::test]
#[ignore = "requires disposable live PostgreSQL fixture"]
async fn disconnect_interrupts_busy_portal() {
    let mut connection = PostgresDriver.connect(settings()).await.unwrap();
    let mut cursor = connection
        .execute("SELECT CASE WHEN i=1 THEN 0 ELSE length(pg_sleep(30)::text) END FROM generate_series(1,2) i", QueryOptions::default())
        .await
        .unwrap();
    let fetching = tokio::spawn(async move { cursor.fetch_page(PageSize::default()).await });
    tokio::time::sleep(std::time::Duration::from_millis(100)).await;
    tokio::time::timeout(std::time::Duration::from_secs(2), connection.close())
        .await
        .expect("disconnect must interrupt busy query")
        .unwrap();
    assert!(fetching.await.unwrap().is_err());
}
#[tokio::test]
#[ignore = "requires disposable live PostgreSQL fixture"]
async fn numeric_infinities_preserve_exact_values() {
    let mut connection = PostgresDriver.connect(settings()).await.unwrap();
    let rows = run(
        &mut *connection,
        "SELECT 'Infinity'::numeric, '-Infinity'::numeric, 'NaN'::numeric",
        true,
    )
    .await;
    assert_eq!(
        rows,
        vec![vec![
            Value::Decimal("Infinity".into()),
            Value::Decimal("-Infinity".into()),
            Value::Decimal("NaN".into())
        ]]
    );
    connection.close().await.unwrap();
}
#[tokio::test]
#[ignore = "requires disposable live PostgreSQL fixture"]
async fn result_schema_preserves_modifiers_without_assuming_nullability() {
    let mut connection = PostgresDriver.connect(settings()).await.unwrap();
    run(&mut *connection, "CREATE TEMP TABLE choscordb_modifiers (amount numeric(30,8) NOT NULL, rounded numeric(12,-2), stamp timestamptz(3), label varchar(24))", true).await;
    let mut cursor = connection
        .execute(
            "SELECT amount, rounded, stamp, label, 1::integer AS expression FROM choscordb_modifiers",
            QueryOptions::default(),
        )
        .await
        .unwrap();
    let columns = cursor.columns();
    assert_eq!(
        (columns[0].precision, columns[0].scale, columns[0].nullable),
        (Some(30), Some(8), None)
    );
    assert_eq!(
        (columns[1].precision, columns[1].scale, columns[1].nullable),
        (Some(12), Some(-2), None)
    );
    assert_eq!(
        (columns[2].precision, columns[2].timezone.as_deref()),
        (Some(3), Some("UTC"))
    );
    assert_eq!(columns[3].precision, Some(24));
    assert_eq!(columns[4].nullable, None);
    cursor.close().await.unwrap();
    connection.close().await.unwrap();
}
#[tokio::test]
async fn oversized_tls_root_is_rejected_before_connection() {
    let file = tempfile::NamedTempFile::new().unwrap();
    file.as_file().set_len(1024 * 1024 + 1).unwrap();
    let options = ConnectionOptions::Postgres {
        ssh_jump_secrets: Default::default(),
        ssh_private_key: None,
        ssh_jump_private_keys: Default::default(),
        proxy: None,
        proxy_secret: None,
        host: "localhost".into(),
        port: 1,
        database: "postgres".into(),
        user: "fixture".into(),
        password: None,
        ssh_secret: None,
        tls: TlsMode::VerifyFull,
        ssh: None,
        tls_identity: None,
        root_certificate: Some(file.path().into()),
    };
    let result = PostgresDriver.connect(options).await;
    assert!(matches!(
        result,
        Err(DriverError {
            kind: ErrorKind::ResourceLimit,
            ..
        })
    ));
}
#[tokio::test]
#[ignore = "requires disposable live PostgreSQL fixture"]
async fn execute_writes_before_fetch_or_commit() {
    let mut connection = PostgresDriver.connect(settings()).await.unwrap();
    run(
        &mut *connection,
        "CREATE TEMP TABLE choscordb_eager (id integer)",
        true,
    )
    .await;
    let cursor = connection
        .execute(
            "INSERT INTO choscordb_eager VALUES (1)",
            QueryOptions {
                auto_commit: false,
                ..Default::default()
            },
        )
        .await
        .unwrap();
    assert_eq!(cursor.summary().affected_rows, Some(1));
    connection.commit().await.unwrap();
    drop(cursor);
    assert_eq!(
        run(
            &mut *connection,
            "SELECT count(*) FROM choscordb_eager",
            true
        )
        .await,
        vec![vec![Value::Integer(1)]]
    );
    let cursor = connection
        .execute(
            "INSERT INTO choscordb_eager VALUES (2)",
            QueryOptions::default(),
        )
        .await
        .unwrap();
    connection.rollback().await.unwrap();
    drop(cursor);
    assert_eq!(
        run(
            &mut *connection,
            "SELECT count(*) FROM choscordb_eager",
            true
        )
        .await,
        vec![vec![Value::Integer(2)]]
    );
    connection.close().await.unwrap();
}
#[tokio::test]
#[ignore = "requires disposable live PostgreSQL fixture"]
async fn stale_fetch_does_not_rollback_newer_write() {
    let mut connection = PostgresDriver.connect(settings()).await.unwrap();
    run(
        &mut *connection,
        "CREATE TEMP TABLE choscordb_stale (id integer)",
        true,
    )
    .await;
    let mut old = connection
        .execute("SELECT 1", QueryOptions::default())
        .await
        .unwrap();
    let mut newer = connection
        .execute(
            "INSERT INTO choscordb_stale VALUES(1) RETURNING id",
            QueryOptions::default(),
        )
        .await
        .unwrap();
    assert_eq!(
        old.fetch_page(PageSize::default()).await.unwrap_err().kind,
        ErrorKind::StaleHandle
    );
    assert_eq!(
        newer.fetch_page(PageSize::default()).await.unwrap().rows,
        vec![vec![Value::Integer(1)]]
    );
    assert_eq!(
        run(
            &mut *connection,
            "SELECT count(*) FROM choscordb_stale",
            true
        )
        .await,
        vec![vec![Value::Integer(1)]]
    );
    connection.close().await.unwrap();
}
#[tokio::test]
#[ignore = "requires disposable live PostgreSQL fixture"]
async fn outer_join_does_not_claim_source_not_null() {
    let mut connection = PostgresDriver.connect(settings()).await.unwrap();
    run(
        &mut *connection,
        "CREATE TEMP TABLE choscordb_nullable (id integer NOT NULL)",
        true,
    )
    .await;
    let mut cursor = connection
        .execute(
            "SELECT b.id FROM (VALUES(1)) a(id) LEFT JOIN choscordb_nullable b USING(id)",
            QueryOptions::default(),
        )
        .await
        .unwrap();
    assert_eq!(cursor.columns()[0].nullable, None);
    assert_eq!(
        cursor.fetch_page(PageSize::default()).await.unwrap().rows,
        vec![vec![Value::Null]]
    );
    connection.close().await.unwrap();
}
#[tokio::test]
#[ignore = "requires disposable live PostgreSQL fixture"]
async fn prepare_lock_wait_obeys_timeout() {
    let mut blocker = PostgresDriver.connect(settings()).await.unwrap();
    let name = format!(
        "choscordb_prepare_{}",
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos()
    );
    run(
        &mut *blocker,
        &format!("CREATE TABLE {name}(id integer)"),
        true,
    )
    .await;
    run(
        &mut *blocker,
        &format!("LOCK TABLE {name} IN ACCESS EXCLUSIVE MODE"),
        false,
    )
    .await;
    let mut connection = PostgresDriver.connect(settings()).await.unwrap();
    let result = connection
        .execute(
            &format!("SELECT * FROM {name}"),
            QueryOptions {
                timeout: Some(std::time::Duration::from_millis(50)),
                ..Default::default()
            },
        )
        .await;
    blocker.rollback().await.unwrap();
    run(&mut *blocker, &format!("DROP TABLE {name}"), true).await;
    assert!(matches!(
        result,
        Err(DriverError {
            kind: ErrorKind::Timeout,
            ..
        })
    ));
    connection.close().await.unwrap();
    blocker.close().await.unwrap();
}
#[tokio::test]
#[ignore = "requires disposable live PostgreSQL fixture"]
async fn suspended_cancel_rolls_back_returning_write_on_close_and_replace() {
    let mut connection = PostgresDriver.connect(settings()).await.unwrap();
    let mut observer = PostgresDriver.connect(settings()).await.unwrap();
    let table = format!(
        "choscordb_idle_cancel_{}",
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos()
    );
    run(
        &mut *connection,
        &format!("CREATE TABLE {table}(id integer)"),
        true,
    )
    .await;
    for replace in [false, true] {
        let token = connection.cancellation_handle();
        let mut cursor = connection
            .execute(
                &format!("INSERT INTO {table} SELECT generate_series(1,3) RETURNING id"),
                QueryOptions::default(),
            )
            .await
            .unwrap();
        token.cancel().await.unwrap();
        if replace {
            run(&mut *connection, "SELECT 1", true).await;
        } else {
            cursor.close().await.unwrap();
        }
        assert_eq!(
            run(
                &mut *observer,
                &format!("SELECT count(*) FROM {table}"),
                true
            )
            .await,
            vec![vec![Value::Integer(0)]]
        );
    }
    let mut normal = connection
        .execute(
            &format!("INSERT INTO {table} SELECT generate_series(1,3) RETURNING id"),
            QueryOptions::default(),
        )
        .await
        .unwrap();
    normal.close().await.unwrap();
    assert_eq!(
        run(
            &mut *observer,
            &format!("SELECT count(*) FROM {table}"),
            true
        )
        .await,
        vec![vec![Value::Integer(3)]]
    );
    run(&mut *connection, &format!("DROP TABLE {table}"), true).await;
    connection.close().await.unwrap();
    observer.close().await.unwrap();
}
#[tokio::test]
#[ignore = "requires disposable live PostgreSQL fixture"]
async fn completed_command_notices_belong_to_that_query() {
    let mut connection = PostgresDriver.connect(settings()).await.unwrap();
    let cursor = connection
        .execute(
            "DO $$ BEGIN RAISE NOTICE 'fixture notice'; END $$",
            QueryOptions::default(),
        )
        .await
        .unwrap();
    assert_eq!(cursor.summary().warnings.len(), 1);
    assert!(cursor.summary().warnings[0].contains("fixture notice"));
    let mut next = connection
        .execute("SELECT 1", QueryOptions::default())
        .await
        .unwrap();
    assert!(next.summary().warnings.is_empty());
    next.fetch_page(PageSize::default()).await.unwrap();
    assert!(next.summary().warnings.is_empty());
    connection.close().await.unwrap();
}
#[tokio::test]
#[ignore = "requires disposable live PostgreSQL fixture"]
async fn xml_char_and_timetz_use_typed_codec() {
    let mut connection = PostgresDriver.connect(settings()).await.unwrap();
    let rows = run(
        &mut *connection,
        "SELECT '<x>text</x>'::xml, 'a'::\"char\", '12:00:00.123456+05:30:45'::timetz",
        true,
    )
    .await;
    assert_eq!(
        rows,
        vec![vec![
            Value::Text("<x>text</x>".into()),
            Value::Text("a".into()),
            Value::Time("12:00:00.123456+05:30:45".into())
        ]]
    );
    let mut cursor = connection
        .execute(
            "SELECT ('<x>' || repeat('a',100000) || '</x>')::xml",
            QueryOptions::default(),
        )
        .await
        .unwrap();
    let page = cursor
        .fetch_page_bounded(PageSize::default(), 4096)
        .await
        .unwrap();
    let Value::Deferred {
        handle,
        byte_length,
        ..
    } = page.rows[0][0]
    else {
        panic!("deferred XML")
    };
    assert_eq!(byte_length, 100007);
    let reader = cursor.deferred_reader().unwrap();
    let chunk = tokio::task::spawn_blocking(move || reader.read_chunk(handle, 100003, 4).unwrap())
        .await
        .unwrap();
    assert_eq!(chunk.bytes, b"</x>");
    connection.close().await.unwrap();
}
#[tokio::test]
#[ignore = "requires disposable live PostgreSQL fixture"]
async fn utf8_notices_are_bounded_and_completed_source_has_hint() {
    let mut connection = PostgresDriver.connect(settings()).await.unwrap();
    let cursor = connection
        .execute(
            "DO $$ BEGIN RAISE NOTICE '%', repeat('é',10000); END $$",
            QueryOptions::default(),
        )
        .await
        .unwrap();
    let warnings = cursor.summary().warnings;
    assert_eq!(warnings.len(), 1);
    assert!(warnings[0].contains('é'));
    assert!(warnings[0].len() <= 512);
    assert!(warnings[0].ends_with('…'));
    assert!(
        cursor
            .retained_bytes_after_completion()
            .is_some_and(|bytes| bytes < 1024 * 1024)
    );
    let mut next = connection
        .execute("SELECT 'text'", QueryOptions::default())
        .await
        .unwrap();
    next.fetch_page(PageSize::default()).await.unwrap();
    assert!(next.summary().warnings.is_empty());
    assert!(
        next.retained_bytes_after_completion()
            .is_some_and(|bytes| (65536..1024 * 1024).contains(&bytes))
    );
    connection.close().await.unwrap();
}
#[tokio::test]
#[ignore = "requires disposable live PostgreSQL fixture"]
async fn disabled_tls_ignores_obsolete_root_certificate_path() {
    let mut options = settings();
    if let ConnectionOptions::Postgres {
        tls,
        root_certificate,
        ..
    } = &mut options
    {
        *tls = TlsMode::Disable;
        *root_certificate = Some("/missing/obsolete/root-certificate.pem".into());
    }
    let mut connection = PostgresDriver.connect(options).await.unwrap();
    assert_eq!(
        run(&mut *connection, "SELECT 1", true).await,
        vec![vec![Value::Integer(1)]]
    );
    connection.close().await.unwrap();
}

#[tokio::test]
#[ignore = "requires disposable live PostgreSQL fixture"]
async fn object_pages_preserve_sql_portal_and_uncommitted_work() {
    let mut c = PostgresDriver.connect(settings()).await.unwrap();
    run(&mut *c, "CREATE SCHEMA object_read_fixture", false).await;
    run(
        &mut *c,
        "CREATE TABLE object_read_fixture.\"dữ\"\" liệu\"(x integer)",
        false,
    )
    .await;
    run(
        &mut *c,
        "INSERT INTO object_read_fixture.\"dữ\"\" liệu\" SELECT n*10 FROM generate_series(1,250) n",
        false,
    )
    .await;
    let database = c.load_metadata(None).await.unwrap().remove(0);
    let schema = c
        .load_metadata(Some(database.id))
        .await
        .unwrap()
        .into_iter()
        .find(|o| o.name == "object_read_fixture")
        .unwrap();
    let tables = c.load_metadata(Some(schema.id)).await.unwrap();
    let table = c
        .load_metadata(Some(
            tables.into_iter().find(|o| o.name == "Tables").unwrap().id,
        ))
        .await
        .unwrap()
        .remove(0);
    let mut sql = c
        .execute(
            "SELECT generate_series(101,350)",
            QueryOptions {
                auto_commit: false,
                ..Default::default()
            },
        )
        .await
        .unwrap();
    assert_eq!(
        sql.fetch_page(PageSize::new(100).unwrap())
            .await
            .unwrap()
            .rows[0],
        vec![Value::Integer(101)]
    );
    let mut object = c.open_object(&table.id, 4096).await.unwrap();
    let page = object
        .fetch_page_bounded(PageSize::new(100).unwrap(), 65536)
        .await
        .unwrap();
    assert_eq!(page.rows[0], vec![Value::Integer(10)]);
    assert_eq!(page.rows[99], vec![Value::Integer(1000)]);
    assert!(page.has_more);
    assert_eq!(object.summary().transaction_active, Some(true));
    assert_eq!(
        sql.fetch_page(PageSize::new(100).unwrap())
            .await
            .unwrap()
            .rows[0],
        vec![Value::Integer(201)]
    );
    object.close().await.unwrap();
    sql.close().await.unwrap();
    c.rollback().await.unwrap();
    let rows = run(
        &mut *c,
        "SELECT EXISTS(SELECT 1 FROM pg_namespace WHERE nspname='object_read_fixture')",
        true,
    )
    .await;
    assert_eq!(rows, vec![vec![Value::Bool(false)]]);
}

#[tokio::test]
#[ignore = "requires disposable live PostgreSQL fixture"]
async fn cancelling_object_read_keeps_user_savepoint_transaction_and_sql_portal() {
    let mut c = PostgresDriver.connect(settings()).await.unwrap();
    run(&mut *c, "CREATE SCHEMA object_cancel_fixture", false).await;
    run(
        &mut *c,
        "CREATE TABLE object_cancel_fixture.edits(x integer)",
        false,
    )
    .await;
    run(
        &mut *c,
        "INSERT INTO object_cancel_fixture.edits VALUES(71)",
        false,
    )
    .await;
    run(&mut *c, "SAVEPOINT user_work", false).await;
    run(&mut *c,"CREATE VIEW object_cancel_fixture.slow AS SELECT n FROM generate_series(1,250) n CROSS JOIN LATERAL pg_sleep(n*0+0.1)",false).await;
    let database = c.load_metadata(None).await.unwrap().remove(0);
    let schema = c
        .load_metadata(Some(database.id))
        .await
        .unwrap()
        .into_iter()
        .find(|o| o.name == "object_cancel_fixture")
        .unwrap();
    let views = c.load_metadata(Some(schema.id)).await.unwrap();
    let view = c
        .load_metadata(Some(
            views.into_iter().find(|o| o.name == "Views").unwrap().id,
        ))
        .await
        .unwrap()
        .into_iter()
        .find(|o| o.name == "slow")
        .unwrap();
    let mut sql = c
        .execute(
            "SELECT generate_series(101,350)",
            QueryOptions {
                auto_commit: false,
                ..Default::default()
            },
        )
        .await
        .unwrap();
    assert_eq!(
        sql.fetch_page(PageSize::new(100).unwrap())
            .await
            .unwrap()
            .rows[0],
        vec![Value::Integer(101)]
    );
    let mut object = c.open_object(&view.id, 4096).await.unwrap();
    let cancel = object.independent_cancellation_handle().unwrap();
    let read = tokio::spawn(async move {
        let result = object
            .fetch_page_bounded(PageSize::new(100).unwrap(), 65536)
            .await;
        object.close().await.unwrap();
        result
    });
    tokio::time::sleep(std::time::Duration::from_millis(100)).await;
    cancel.cancel().await.unwrap();
    let result = tokio::time::timeout(std::time::Duration::from_secs(3), read)
        .await
        .unwrap()
        .unwrap();
    assert!(matches!(result,Err(error) if error.kind==ErrorKind::Cancelled));
    assert_eq!(
        sql.fetch_page(PageSize::new(100).unwrap())
            .await
            .unwrap()
            .rows[0],
        vec![Value::Integer(201)]
    );
    sql.close().await.unwrap();
    run(&mut *c, "ROLLBACK TO SAVEPOINT user_work", false).await;
    assert_eq!(
        run(&mut *c, "SELECT x FROM object_cancel_fixture.edits", false).await,
        vec![vec![Value::Integer(71)]]
    );
    c.rollback().await.unwrap();
}
