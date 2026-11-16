use choscordb_core::{
    BackupDirection, BackupFamily, BackupPlan, BackupRequest, BackupTarget, Engine, Event,
    plan_backup,
};
use choscordb_driver_api::{ConnectionOptions, ErrorKind, Secret, SshTunnel, TlsMode};
use std::path::{Path, PathBuf};

fn server(driver: &str, tls: TlsMode, ssh: Option<SshTunnel>) -> ConnectionOptions {
    server_at(driver, "db.example", "shop", tls, ssh)
}

fn server_at(
    driver: &str,
    host: &str,
    database: &str,
    tls: TlsMode,
    ssh: Option<SshTunnel>,
) -> ConnectionOptions {
    macro_rules! options {
        ($variant:ident) => {
            ConnectionOptions::$variant {
                proxy: None,
                proxy_secret: None,
                host: host.into(),
                port: 6000,
                database: database.into(),
                user: "owner".into(),
                password: Some(Secret::new("p4ss")),
                ssh_secret: None,
                ssh_jump_secrets: Default::default(),
                ssh_private_key: None,
                ssh_jump_private_keys: Default::default(),
                tls,
                root_certificate: Some("/certs/root.pem".into()),
                tls_identity: None,
                ssh,
            }
        };
    }
    match driver {
        "postgres" => options!(Postgres),
        "mysql" => options!(Mysql),
        _ => options!(Mariadb),
    }
}

fn request(
    _driver: &str,
    direction: BackupDirection,
    file: &Path,
    folder: &Path,
    codes: &[&str],
) -> BackupRequest {
    let codes: Vec<String> = codes.iter().map(|code| (*code).to_owned()).collect();
    BackupRequest::new(
        direction,
        file.to_str().unwrap(),
        folder.to_str().unwrap(),
        &codes,
    )
    .unwrap()
}

fn tool_folder(names: &[&str]) -> tempfile::TempDir {
    let folder = tempfile::tempdir().unwrap();
    for name in names {
        std::fs::write(
            folder
                .path()
                .join(format!("{name}{}", std::env::consts::EXE_SUFFIX)),
            "",
        )
        .unwrap();
    }
    folder
}

fn tool(plan: BackupPlan) -> choscordb_core::ToolCommand {
    match plan {
        BackupPlan::Tool(command) => command,
        other => panic!("{other:?}"),
    }
}

fn exe(folder: &Path, name: &str) -> PathBuf {
    folder.join(format!("{name}{}", std::env::consts::EXE_SUFFIX))
}

#[test]
fn each_driver_lists_its_options_with_defaults() {
    let codes = |driver, direction| -> Vec<(&str, bool)> {
        BackupFamily::from_driver(driver)
            .unwrap()
            .options(direction)
            .iter()
            .map(|option| (option.code, option.default))
            .collect()
    };
    assert_eq!(
        codes("postgres", BackupDirection::Backup),
        [
            ("custom_format", true),
            ("schema_only", false),
            ("data_only", false),
            ("clean", false),
            ("create", false),
            ("no_owner", false),
        ]
    );
    assert_eq!(
        codes("postgres", BackupDirection::Restore)[..2],
        [("single_transaction", true), ("exit_on_error", true)]
    );
    assert_eq!(codes("mariadb", BackupDirection::Backup).len(), 7);
    assert_eq!(codes("mysql", BackupDirection::Restore), [("force", false)]);
    assert!(codes("sqlite", BackupDirection::Backup).is_empty());
}

#[test]
fn requests_reject_bad_paths() {
    let new =
        |direction, file, folder| BackupRequest::new(direction, file, folder, &[]).unwrap_err();
    let backup = BackupDirection::Backup;
    assert_eq!(new(backup, " ", ""), "Choose a backup file.");
    assert_eq!(
        new(backup, "shop.dump", ""),
        "Enter the full path of the file."
    );
    assert_eq!(
        new(backup, "/tmp/a.dump", "bin"),
        "Enter the full path of the tool folder."
    );
    assert_eq!(BackupFamily::from_driver("duckdb"), None);
    assert_eq!(
        BackupFamily::Sqlite.unavailable(BackupDirection::Restore),
        Some(
            "ChoscorDB cannot restore over an open SQLite database. Open the backup file as a new connection."
        )
    );
}

#[test]
fn options_are_checked_against_the_connected_database() {
    let tools = tool_folder(&["mysqldump", "pg_dump"]);
    let out = tempfile::tempdir().unwrap();
    let file = out.path().join("shop.sql");
    let plan = |driver: &str, codes: &[&str]| {
        plan_backup(
            &BackupTarget::from_options(&server(driver, TlsMode::Prefer, None)),
            &request(driver, BackupDirection::Backup, &file, tools.path(), codes),
            None,
        )
        .unwrap_err()
    };
    let error = plan("mysql", &["custom_format"]);
    assert_eq!(error.kind, ErrorKind::InvalidInput);
    assert_eq!(error.message, "MySQL has no backup option custom_format.");
    assert_eq!(
        plan("postgres", &["schema_only", "data_only"]).message,
        "Choose schema only or data only, not both."
    );
}

#[test]
fn postgres_backup_runs_pg_dump_with_the_connection_settings() {
    let tools = tool_folder(&["pg_dump"]);
    let out = tempfile::tempdir().unwrap();
    let file = out.path().join("shop.dump");
    let target = BackupTarget::from_options(&server("postgres", TlsMode::VerifyFull, None));
    let command = tool(
        plan_backup(
            &target,
            &request(
                "postgres",
                BackupDirection::Backup,
                &file,
                Path::new(""),
                &["no_owner", "custom_format"],
            ),
            Some(tools.path().as_os_str()),
        )
        .unwrap(),
    );
    assert_eq!(command.program, exe(tools.path(), "pg_dump"));
    assert_eq!(
        command.args,
        [
            "--host=db.example".to_owned(),
            "--port=6000".into(),
            "--username=owner".into(),
            "--dbname=dbname='shop'".into(),
            "--no-password".into(),
            "--verbose".into(),
            "--format=custom".into(),
            "--no-owner".into(),
            format!("--file={}", file.display()),
        ]
    );
    assert_eq!(
        command.env,
        [
            ("PGSSLMODE", "verify-full".to_owned()),
            ("PGSSLROOTCERT", "/certs/root.pem".into()),
        ]
    );
    assert_eq!(command.password_env, "PGPASSWORD");
    assert_eq!(command.password.as_ref().unwrap().expose(), "p4ss");
    assert!(command.preview().starts_with("$ PGPASSWORD=<hidden> "));
    assert!(!command.preview().contains("p4ss"));

    let plain = tool(
        plan_backup(
            &target,
            &request(
                "postgres",
                BackupDirection::Backup,
                &file,
                Path::new(""),
                &[],
            ),
            Some(tools.path().as_os_str()),
        )
        .unwrap(),
    );
    assert!(plain.args.contains(&"--format=plain".to_owned()));
}

#[test]
fn a_missing_tool_names_the_tool_and_the_fix() {
    let empty = tempfile::tempdir().unwrap();
    let out = tempfile::tempdir().unwrap();
    let file = out.path().join("shop.dump");
    let target = BackupTarget::from_options(&server("postgres", TlsMode::Prefer, None));
    let error = plan_backup(
        &target,
        &request(
            "postgres",
            BackupDirection::Backup,
            &file,
            Path::new(""),
            &[],
        ),
        Some(empty.path().as_os_str()),
    )
    .unwrap_err();
    assert_eq!(
        error.message,
        "pg_dump is not on PATH. Install the PostgreSQL client tools, or choose the folder that contains them."
    );
    let error = plan_backup(
        &target,
        &request(
            "postgres",
            BackupDirection::Backup,
            &file,
            empty.path(),
            &[],
        ),
        None,
    )
    .unwrap_err();
    assert_eq!(
        error.message,
        format!(
            "pg_dump is not in {}. Choose the folder that contains the PostgreSQL client tools.",
            empty.path().display()
        )
    );
}

#[test]
fn pg_restore_accepts_only_archives() {
    let tools = tool_folder(&["pg_restore"]);
    let files = tempfile::tempdir().unwrap();
    let plain = files.path().join("plain.sql");
    std::fs::write(&plain, "CREATE TABLE t (a int);").unwrap();
    let archive = files.path().join("shop.dump");
    std::fs::write(&archive, b"PGDMP\x01\x0e").unwrap();
    let target = BackupTarget::from_options(&server("postgres", TlsMode::Disable, None));
    let plan = |file: &Path| {
        plan_backup(
            &target,
            &request(
                "postgres",
                BackupDirection::Restore,
                file,
                tools.path(),
                &["single_transaction", "clean"],
            ),
            None,
        )
    };
    assert_eq!(
        plan(&plain).unwrap_err().message,
        "pg_restore needs a custom or tar archive. Run a plain SQL backup with Run SQL file."
    );
    let command = tool(plan(&archive).unwrap());
    assert_eq!(
        command.args[6..],
        [
            "--single-transaction".to_owned(),
            "--clean".into(),
            archive.to_str().unwrap().into(),
        ]
    );
    assert_eq!(command.env[0], ("PGSSLMODE", "disable".to_owned()));
    assert_eq!(
        plan(&files.path().join("gone.dump")).unwrap_err().message,
        format!(
            "{} is not a file.",
            files.path().join("gone.dump").display()
        )
    );
}

#[test]
fn mysql_and_mariadb_use_their_own_tools_and_tls_flags() {
    let tools = tool_folder(&["mysqldump", "mysql"]);
    let out = tempfile::tempdir().unwrap();
    let file = out.path().join("shop.sql");
    std::fs::write(&file, "SELECT 1;").unwrap();
    let mysql = BackupTarget::from_options(&server("mysql", TlsMode::VerifyCa, None));
    let backup = tool(
        plan_backup(
            &mysql,
            &request(
                "mysql",
                BackupDirection::Backup,
                &file,
                tools.path(),
                &["triggers", "schema_only"],
            ),
            None,
        )
        .unwrap(),
    );
    assert_eq!(backup.program, exe(tools.path(), "mysqldump"));
    assert_eq!(
        backup.args,
        [
            "--host=db.example".to_owned(),
            "--port=6000".into(),
            "--protocol=TCP".into(),
            "--user=owner".into(),
            "--ssl-mode=VERIFY_CA".into(),
            "--ssl-ca=/certs/root.pem".into(),
            "--verbose".into(),
            "--skip-routines".into(),
            "--triggers".into(),
            "--no-data".into(),
            "--skip-add-drop-table".into(),
            format!("--result-file={}", file.display()),
            "shop".into(),
        ]
    );
    assert_eq!(backup.password_env, "MYSQL_PWD");
    assert_eq!(backup.password.as_ref().unwrap().expose(), "p4ss");
    assert_eq!(backup.stdin, None);

    let mariadb = BackupTarget::from_options(&server("mariadb", TlsMode::Require, None));
    let restore = tool(
        plan_backup(
            &mariadb,
            &request(
                "mariadb",
                BackupDirection::Restore,
                &file,
                tools.path(),
                &["force"],
            ),
            None,
        )
        .unwrap(),
    );
    assert_eq!(restore.program, exe(tools.path(), "mysql"));
    assert_eq!(
        restore.args[4..],
        [
            "--ssl".to_owned(),
            "--ssl-ca=/certs/root.pem".into(),
            "--database=shop".into(),
            "--force".into(),
        ]
    );
    assert_eq!(restore.stdin.as_deref(), Some(file.as_path()));
    assert!(
        restore
            .preview()
            .ends_with(&format!("< {}", file.display()))
    );

    let native = tool_folder(&["mariadb-dump", "mysqldump"]);
    let dump = tool(
        plan_backup(
            &mariadb,
            &request(
                "mariadb",
                BackupDirection::Backup,
                &out.path().join("new.sql"),
                native.path(),
                &[],
            ),
            None,
        )
        .unwrap(),
    );
    assert_eq!(dump.program, exe(native.path(), "mariadb-dump"));
}

#[test]
fn tunnels_and_proxies_are_unavailable() {
    let tunnel = SshTunnel {
        host: "bastion".into(),
        port: 22,
        user: "ops".into(),
        authentication: choscordb_driver_api::SshAuthentication::Agent,
        identity_file: None,
        identity_source: Default::default(),
        options: Default::default(),
    };
    let target = BackupTarget::from_options(&server("postgres", TlsMode::Prefer, Some(tunnel)));
    let out = tempfile::tempdir().unwrap();
    let error = plan_backup(
        &target,
        &request(
            "postgres",
            BackupDirection::Backup,
            &out.path().join("a.dump"),
            Path::new(""),
            &[],
        ),
        None,
    )
    .unwrap_err();
    assert_eq!(error.kind, ErrorKind::Unsupported);
    assert_eq!(
        error.message,
        "Backup and restore cannot use an SSH tunnel. The client tools connect to the server directly."
    );
}

fn wait(engine: &mut Engine, predicate: impl Fn(&Event) -> bool) -> Event {
    let until = std::time::Instant::now() + std::time::Duration::from_secs(10);
    loop {
        if let Some(event) = engine.try_event()
            && predicate(&event)
        {
            return event;
        }
        assert!(std::time::Instant::now() < until, "event timed out");
        std::thread::sleep(std::time::Duration::from_millis(1));
    }
}

#[test]
fn the_engine_backs_up_a_sqlite_database_into_a_new_file() {
    let dir = tempfile::tempdir().unwrap();
    let database = dir.path().join("db.sqlite");
    rusqlite::Connection::open(&database)
        .unwrap()
        .execute_batch("CREATE TABLE t (a INTEGER); INSERT INTO t VALUES (1), (2);")
        .unwrap();
    let mut engine = Engine::new(
        Default::default(),
        vec![std::sync::Arc::new(choscordb_driver_sqlite::SqliteDriver)],
    )
    .unwrap();
    let connection = engine
        .connect(
            "sqlite",
            ConnectionOptions::Sqlite {
                path: database,
                read_only: true,
            },
        )
        .unwrap();
    wait(&mut engine, |event| {
        matches!(event, Event::Connected { .. })
    });
    let backup = dir.path().join("it's backup.sqlite");
    let settled = |event: &Event| {
        matches!(
            event,
            Event::BackupOutput { .. } | Event::BackupFinished { .. } | Event::BackupFailed { .. }
        )
    };
    let start = |engine: &mut Engine| {
        engine
            .start_backup(
                connection,
                request(
                    "sqlite",
                    BackupDirection::Backup,
                    &backup,
                    Path::new(""),
                    &[],
                ),
            )
            .unwrap()
    };
    let run = start(&mut engine);
    let Event::BackupOutput { run: shown, line } = wait(&mut engine, settled) else {
        panic!("no output");
    };
    assert_eq!(shown, run);
    assert_eq!(
        line,
        format!(
            "VACUUM INTO '{}'",
            backup.display().to_string().replace('\'', "''")
        )
    );
    let event = wait(&mut engine, settled);
    assert!(
        matches!(event, Event::BackupFinished { run: done } if done == run),
        "{event:?}"
    );
    let rows: i64 = rusqlite::Connection::open(&backup)
        .unwrap()
        .query_row("SELECT count(*) FROM t", [], |row| row.get(0))
        .unwrap();
    assert_eq!(rows, 2);

    start(&mut engine);
    let event = wait(&mut engine, |event| {
        matches!(
            event,
            Event::BackupFinished { .. } | Event::BackupFailed { .. }
        )
    });
    assert!(
        matches!(&event, Event::BackupFailed { error, .. }
            if error.message == "Choose a new file. SQLite cannot back up into an existing file."),
        "{event:?}"
    );
}

#[test]
fn local_mysql_uses_tcp_and_postgres_quotes_the_database() {
    let tools = tool_folder(&["mysqldump", "pg_dump"]);
    let out = tempfile::tempdir().unwrap();
    let file = out.path().join("shop.sql");
    let command = |driver: &str, database: &str| {
        tool(
            plan_backup(
                &BackupTarget::from_options(&server_at(
                    driver,
                    "localhost",
                    database,
                    TlsMode::Prefer,
                    None,
                )),
                &request(driver, BackupDirection::Backup, &file, tools.path(), &[]),
                None,
            )
            .unwrap(),
        )
    };
    let mysql = command("mysql", "shop");
    assert_eq!(
        mysql.args[..4],
        [
            "--host=localhost".to_owned(),
            "--port=6000".into(),
            "--protocol=TCP".into(),
            "--user=owner".into(),
        ]
    );
    let postgres = command("postgres", r"host=evil it's\x");
    assert!(
        postgres
            .args
            .contains(&r"--dbname=dbname='host=evil it\'s\\x'".to_owned()),
        "{:?}",
        postgres.args
    );
}

struct LocalDriver;
#[async_trait::async_trait]
impl choscordb_driver_api::DatabaseDriver for LocalDriver {
    fn id(&self) -> &'static str {
        "postgres"
    }
    fn capabilities(&self) -> choscordb_driver_api::DriverCapabilities {
        Default::default()
    }
    async fn connect(
        &self,
        _: ConnectionOptions,
    ) -> choscordb_driver_api::Result<Box<dyn choscordb_driver_api::Connection>> {
        choscordb_driver_sqlite::SqliteDriver
            .connect(ConnectionOptions::Sqlite {
                path: ":memory:".into(),
                read_only: false,
            })
            .await
    }
}

struct SavedPassword;
impl choscordb_credentials::CredentialStore for SavedPassword {
    fn get(&self, reference: &str) -> choscordb_credentials::Result<choscordb_credentials::Secret> {
        assert_eq!(reference, "saved-ref");
        Ok(choscordb_credentials::Secret::new("saved-password"))
    }
    fn put(&self, _: &str, _: &choscordb_credentials::Secret) -> choscordb_credentials::Result<()> {
        panic!("a backup must not save a credential")
    }
    fn delete(&self, _: &str) -> choscordb_credentials::Result<()> {
        panic!("a backup must not delete a credential")
    }
}

#[cfg(unix)]
#[test]
fn saved_profiles_give_the_tool_their_password_and_keep_read_only() {
    use std::os::unix::fs::PermissionsExt;
    let tools = tempfile::tempdir().unwrap();
    for name in ["pg_dump", "pg_restore"] {
        let script = tools.path().join(name);
        std::fs::write(&script, "#!/bin/sh\necho \"password=$PGPASSWORD\"\n").unwrap();
        std::fs::set_permissions(&script, std::fs::Permissions::from_mode(0o755)).unwrap();
    }
    let out = tempfile::tempdir().unwrap();
    let archive = out.path().join("shop.dump");
    std::fs::write(&archive, b"PGDMP").unwrap();
    for safety in ["silent", "read_only"] {
        let profile: choscordb_core::ConnectionProfile =
            serde_json::from_value(serde_json::json!({
                "id":"saved", "name":"Saved", "credential_ref":"saved-ref", "safety":safety,
                "configuration":{"driver":"postgres", "host":"db.example", "port":5432,
                    "database":"shop", "user":"alice", "tls":{"mode":"Disable"}}
            }))
            .unwrap();
        let mut engine = Engine::new_with_credentials(
            Default::default(),
            vec![std::sync::Arc::new(LocalDriver)],
            std::sync::Arc::new(SavedPassword),
        )
        .unwrap();
        let connection = engine.connect_profile(profile, None).unwrap();
        wait(&mut engine, |event| {
            matches!(event, Event::Connected { .. })
        });
        let mut run = |direction, file: &Path| {
            engine
                .start_backup(
                    connection,
                    request("postgres", direction, file, tools.path(), &[]),
                )
                .unwrap();
            let mut lines = Vec::new();
            loop {
                match wait(&mut engine, |event| {
                    matches!(
                        event,
                        Event::BackupOutput { .. }
                            | Event::BackupFinished { .. }
                            | Event::BackupFailed { .. }
                    )
                }) {
                    Event::BackupOutput { line, .. } => lines.push(line),
                    Event::BackupFinished { .. } => return Ok(lines),
                    Event::BackupFailed { error, .. } => return Err(error),
                    _ => unreachable!(),
                }
            }
        };
        let lines = run(BackupDirection::Backup, &out.path().join("new.dump")).unwrap();
        assert!(lines[0].starts_with("$ PGPASSWORD=<hidden> "), "{lines:?}");
        assert_eq!(lines[1], "password=saved-password");
        let restored = run(BackupDirection::Restore, &archive);
        if safety == "read_only" {
            let error = restored.unwrap_err();
            assert_eq!(error.kind, ErrorKind::Unsupported);
            assert_eq!(
                error.message,
                "This connection is read-only. A restore can change data."
            );
        } else {
            assert_eq!(restored.unwrap()[1], "password=saved-password");
        }
    }
}

struct SwitchingDriver;
#[async_trait::async_trait]
impl choscordb_driver_api::DatabaseDriver for SwitchingDriver {
    fn id(&self) -> &'static str {
        "postgres"
    }
    fn capabilities(&self) -> choscordb_driver_api::DriverCapabilities {
        Default::default()
    }
    async fn connect(
        &self,
        options: ConnectionOptions,
    ) -> choscordb_driver_api::Result<Box<dyn choscordb_driver_api::Connection>> {
        if matches!(&options, ConnectionOptions::Postgres { database, .. } if database == "gone") {
            return Err(choscordb_driver_api::DriverError::new(
                ErrorKind::Connection,
                "database \"gone\" does not exist",
            ));
        }
        LocalDriver.connect(options).await
    }
}

#[cfg(unix)]
#[test]
fn a_failed_database_switch_keeps_the_backup_target_of_the_open_database() {
    use std::os::unix::fs::PermissionsExt;
    let tools = tempfile::tempdir().unwrap();
    let script = tools.path().join("pg_dump");
    std::fs::write(&script, "#!/bin/sh\nexit 0\n").unwrap();
    std::fs::set_permissions(&script, std::fs::Permissions::from_mode(0o755)).unwrap();
    let out = tempfile::tempdir().unwrap();
    let profile = |database: &str| -> choscordb_core::ConnectionProfile {
        serde_json::from_value(serde_json::json!({
            "id":"saved", "name":"Saved", "credential_ref":"saved-ref",
            "configuration":{"driver":"postgres", "host":"db.example", "port":5432,
                "database":database, "user":"alice", "tls":{"mode":"Disable"}}
        }))
        .unwrap()
    };
    let secrets = || choscordb_core::ProfileSecrets {
        ssh_private_key: None,
        ssh_jump_private_keys: Default::default(),
        ssh_jumps: Default::default(),
        database: None,
        ssh: None,
        tls: None,
        proxy: None,
    };
    let mut engine = Engine::new_with_credentials(
        Default::default(),
        vec![std::sync::Arc::new(SwitchingDriver)],
        std::sync::Arc::new(SavedPassword),
    )
    .unwrap();
    let connection = engine.connect_profile(profile("shop"), None).unwrap();
    wait(&mut engine, |event| {
        matches!(event, Event::Connected { .. })
    });
    let command = |engine: &mut Engine, file: &str| {
        engine
            .start_backup(
                connection,
                request(
                    "postgres",
                    BackupDirection::Backup,
                    &out.path().join(file),
                    tools.path(),
                    &[],
                ),
            )
            .unwrap();
        match wait(engine, |event| {
            matches!(
                event,
                Event::BackupOutput { .. } | Event::BackupFailed { .. }
            )
        }) {
            Event::BackupOutput { line, .. } => line,
            Event::BackupFailed { error, .. } => panic!("{}", error.message),
            _ => unreachable!(),
        }
    };
    engine
        .switch_profile_database(connection, profile("gone"), secrets(), 4)
        .unwrap();
    wait(&mut engine, |event| {
        matches!(
            event,
            Event::DatabaseSwitchFailed {
                request_token: 4,
                ..
            }
        )
    });
    let line = command(&mut engine, "after-failure.dump");
    assert!(line.contains("shop") && !line.contains("gone"), "{line}");
    engine
        .switch_profile_database(connection, profile("other"), secrets(), 5)
        .unwrap();
    wait(&mut engine, |event| {
        matches!(
            event,
            Event::DatabaseSwitched {
                request_token: 5,
                ..
            }
        )
    });
    let line = command(&mut engine, "after-switch.dump");
    assert!(line.contains("other") && !line.contains("shop"), "{line}");
}
