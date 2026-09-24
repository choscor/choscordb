use choscordb_driver_api::*;
use choscordb_driver_sqlite::SqliteDriver;

async fn connect(path: &str, read_only: bool) -> Result<Box<dyn Connection>> {
    SqliteDriver
        .connect(ConnectionOptions::Sqlite {
            path: path.into(),
            read_only,
        })
        .await
}
async fn execute(connection: &mut dyn Connection, sql: &str) -> Result<()> {
    let mut cursor = connection.execute(sql, QueryOptions::default()).await?;
    cursor.fetch_page(PageSize::default()).await?;
    cursor.close().await
}

#[tokio::test]
async fn explicit_uri_read_only_mode_uses_the_existing_file_and_rejects_writes() {
    let directory = tempfile::tempdir().unwrap();
    let file = directory.path().join("database with space.sqlite");
    let literal = file.to_str().unwrap();
    let mut db = connect(literal, false).await.unwrap();
    execute(&mut *db, "CREATE TABLE marker(value INTEGER)")
        .await
        .unwrap();
    execute(&mut *db, "INSERT INTO marker VALUES(41)")
        .await
        .unwrap();
    db.close().await.unwrap();
    let uri = format!("file:{}?mode=ro", literal.replace(' ', "%20"));
    let mut db = connect(&uri, false)
        .await
        .expect("URI must open the existing database");
    let mut cursor = db
        .execute("SELECT value FROM marker", QueryOptions::default())
        .await
        .unwrap();
    assert_eq!(
        cursor.fetch_page(PageSize::default()).await.unwrap().rows,
        vec![vec![Value::Integer(41)]]
    );
    cursor.close().await.unwrap();
    assert!(
        execute(&mut *db, "INSERT INTO marker VALUES(42)")
            .await
            .is_err()
    );
    db.close().await.unwrap();
}

#[tokio::test]
async fn malformed_or_unknown_uri_settings_are_rejected_before_open() {
    for suffix in [
        "mode=memory&unknown=ignored",
        "mode=memory&mode=ro",
        "mode=memory&cache=typo",
        "mode=memory&immutable=perhaps",
        "mode=memory&%6Dode=ro",
        "mode=memory&vfs=%00",
    ] {
        let error = connect(&format!("file:validation?{suffix}"), false)
            .await
            .err()
            .expect("invalid URI must fail");
        assert_eq!(error.kind, ErrorKind::InvalidInput);
    }
}

#[tokio::test]
async fn uri_rw_does_not_create_and_read_only_cannot_be_upgraded() {
    let directory = tempfile::tempdir().unwrap();
    let file = directory.path().join("missing.sqlite");
    let uri = format!("file:{}?mode=rw", file.to_str().unwrap());
    assert!(connect(&uri, false).await.is_err());
    assert!(!file.exists());
    let mut db = connect(file.to_str().unwrap(), false).await.unwrap();
    execute(&mut *db, "CREATE TABLE marker(value INTEGER)")
        .await
        .unwrap();
    db.close().await.unwrap();
    for mode in ["rw", "rwc", "memory"] {
        let error = connect(
            &format!("file:{}?mode={mode}", file.to_str().unwrap()),
            true,
        )
        .await
        .err()
        .expect("read-only must not be upgraded");
        assert_eq!(error.kind, ErrorKind::InvalidInput);
    }
    let mut db = connect(
        &format!("file:{}?immutable=1", file.to_str().unwrap()),
        false,
    )
    .await
    .unwrap();
    assert!(
        execute(&mut *db, "INSERT INTO marker VALUES(1)")
            .await
            .is_err()
    );
    db.close().await.unwrap();
}

#[tokio::test]
async fn named_memory_shared_cache_is_shared_but_private_cache_isolated() {
    let directory = tempfile::tempdir().unwrap();
    for cache in ["shared", "private"] {
        let path = directory.path().join(cache);
        let uri = format!("file:{}?mode=memory&cache={cache}", path.to_str().unwrap());
        let mut first = connect(&uri, false).await.unwrap();
        execute(&mut *first, "CREATE TABLE marker(value INTEGER)")
            .await
            .unwrap();
        execute(&mut *first, "INSERT INTO marker VALUES(19)")
            .await
            .unwrap();
        let mut second = connect(&uri, false).await.unwrap();
        let result = second
            .execute("SELECT value FROM marker", QueryOptions::default())
            .await;
        match cache {
            "shared" => {
                let mut cursor = result.unwrap();
                assert_eq!(
                    cursor.fetch_page(PageSize::default()).await.unwrap().rows,
                    vec![vec![Value::Integer(19)]]
                );
                cursor.close().await.unwrap();
            }
            _ => assert!(result.is_err()),
        }
        second.close().await.unwrap();
        first.close().await.unwrap();
        assert!(!path.exists());
    }
}

#[cfg(unix)]
#[tokio::test]
async fn ordinary_file_names_keep_question_marks_hashes_and_percent_sequences_literal() {
    let directory = tempfile::tempdir().unwrap();
    let file = directory.path().join("literal?mode=ro#%20.sqlite");
    let mut db = connect(file.to_str().unwrap(), false).await.unwrap();
    execute(&mut *db, "CREATE TABLE literal(value INTEGER)")
        .await
        .unwrap();
    execute(&mut *db, "INSERT INTO literal VALUES(1)")
        .await
        .unwrap();
    db.close().await.unwrap();
    assert!(file.exists());
    assert!(!directory.path().join("literal").exists());
}
