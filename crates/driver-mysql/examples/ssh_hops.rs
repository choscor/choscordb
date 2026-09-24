#[path = "../../driver-api/tests/support/ssh_hop_profile.rs"]
mod profile;
use choscordb_driver_api::*;
use choscordb_driver_mysql::MysqlDriver;
fn main() {
    if let Some(code) = run_ssh_askpass_if_requested() {
        std::process::exit(code);
    }
    let mode = std::env::args().nth(1).expect("fixture scenario");
    tokio::runtime::Builder::new_multi_thread()
        .enable_all()
        .build()
        .unwrap()
        .block_on(async {
            if mode.ends_with("reconnect") || mode.ends_with("reconnect-fixed") {
                profile::reconnect_shared(
                    &MysqlDriver,
                    || {
                        options(if mode.starts_with("two-hops") {
                            "two-hops"
                        } else {
                            "password"
                        })
                    },
                    mode.ends_with("fixed"),
                )
                .await;
                return;
            }
            if mode == "binding" || mode == "two-hops-binding" {
                profile::binding(&MysqlDriver, || {
                    options(if mode.starts_with("two-hops") {
                        "two-hops"
                    } else {
                        "password"
                    })
                })
                .await;
                return;
            }
            if mode == "sharing" || mode == "two-hops-sharing" {
                profile::sharing(&MysqlDriver, || {
                    options(if mode.starts_with("two-hops") {
                        "two-hops"
                    } else {
                        "password"
                    })
                })
                .await;
                return;
            }
            if mode == "certificate-context" {
                profile::certificate_context(&MysqlDriver, || options("target-key")).await;
                return;
            }
            if mode == "trust" {
                profile::trust(&MysqlDriver, || options("inline-both")).await;
                return;
            }
            let options = options(&mode);
            if mode == "inline-abort" {
                profile::abort_preflight(&MysqlDriver, options).await;
            } else if mode == "deadline" || mode == "inline-deadline" {
                profile::deadline(&MysqlDriver, options).await;
            } else if mode == "aux-stall" || mode == "inline-aux-stall" {
                auxiliary_preflight_cancellation(options).await;
            } else {
                profile::verify(&MysqlDriver, options, &mode).await;
            }
        });
}

async fn auxiliary_preflight_cancellation(options: ConnectionOptions) {
    use tokio::io::AsyncReadExt;
    let mut connection = MysqlDriver.connect(options).await.unwrap();
    let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
    std::fs::write(
        profile::value("CHOSCORDB_SSH_STALL_ADDRESS"),
        listener.local_addr().unwrap().to_string(),
    )
    .unwrap();
    std::fs::write(profile::value("CHOSCORDB_SSH_STALL_FLAG"), "owned-fixture").unwrap();
    let mut observed = {
        let object = ObjectId(r#"["choscordb_test","pending_preflight"]"#.into());
        let attempt = connection.open_object(&object, 1024 * 1024);
        tokio::pin!(attempt);
        tokio::select! {
            accepted=listener.accept()=>accepted.unwrap().0,
            result=&mut attempt=>panic!("auxiliary preflight completed instead of stalling: {:?}",result.err()),
            _=tokio::time::sleep(std::time::Duration::from_secs(3))=>panic!("controlled preflight did not start"),
        }
    }; // Dropping the future closes only the auxiliary local client, not the main tunnel.
    let mut bytes = Vec::new();
    tokio::time::timeout(
        std::time::Duration::from_secs(2),
        observed.read_to_end(&mut bytes),
    )
    .await
    .expect("cancelled auxiliary preflight retained its owned child")
    .unwrap();
    std::fs::remove_file(profile::value("CHOSCORDB_SSH_STALL_FLAG")).unwrap();
    let mut cursor = connection
        .execute("SELECT 917", QueryOptions::default())
        .await
        .unwrap();
    assert_eq!(
        cursor.fetch_page(PageSize::default()).await.unwrap().rows,
        vec![vec![Value::Integer(917)]]
    );
    cursor.close().await.unwrap();
    connection.close().await.unwrap();
}

fn options(mode: &str) -> ConnectionOptions {
    let (mut ssh, ssh_secret, ssh_jump_secrets) = profile::settings(mode);
    if mode == "wrong-tls" {
        ssh.options.remote_host = Some("mysql-ssh-target".into());
    }
    let (ssh_private_key, ssh_jump_private_keys) = profile::inline_keys(mode);
    ConnectionOptions::Mysql {
        proxy: None,
        proxy_secret: None,
        host: if mode == "wrong-tls" {
            "wrong-cert.invalid"
        } else {
            "mysql-ssh-target"
        }
        .into(),
        port: 3306,
        user: "root".into(),
        database: "choscordb_test".into(),
        password: Some(Secret::new("choscordb-test-password")),
        ssh: Some(ssh),
        ssh_secret,
        ssh_jump_secrets,
        ssh_private_key,
        ssh_jump_private_keys,
        tls: TlsMode::VerifyFull,
        root_certificate: Some(profile::value("CHOSCORDB_SSH_DB_CA").into()),
        tls_identity: None,
    }
}
