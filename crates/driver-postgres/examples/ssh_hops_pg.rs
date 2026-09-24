#[path = "../../driver-api/tests/support/ssh_hop_profile.rs"]
mod profile;
use choscordb_driver_api::*;
use choscordb_driver_postgres::PostgresDriver;
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
                    &PostgresDriver,
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
                profile::binding(&PostgresDriver, || {
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
                profile::sharing(&PostgresDriver, || {
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
                profile::certificate_context(&PostgresDriver, || options("target-key")).await;
                return;
            }
            if mode == "trust" {
                profile::trust(&PostgresDriver, || options("inline-both")).await;
                return;
            }
            let options = options(&mode);
            if mode == "inline-abort" {
                profile::abort_preflight(&PostgresDriver, options).await;
            } else if mode == "deadline" || mode == "inline-deadline" {
                profile::deadline(&PostgresDriver, options).await;
            } else {
                profile::verify(&PostgresDriver, options, &mode).await;
            }
        });
}

fn options(mode: &str) -> ConnectionOptions {
    let (mut ssh, ssh_secret, ssh_jump_secrets) = profile::settings(mode);
    if mode == "wrong-tls" {
        ssh.options.remote_host = Some("postgres-ssh-target".into());
    }
    let (ssh_private_key, ssh_jump_private_keys) = profile::inline_keys(mode);
    ConnectionOptions::Postgres {
        proxy: None,
        proxy_secret: None,
        host: if mode == "wrong-tls" {
            "wrong-cert.invalid"
        } else {
            "postgres-ssh-target"
        }
        .into(),
        port: 5432,
        user: "root".into(),
        database: "postgres".into(),
        password: Some(Secret::new("fixture-pg-password")),
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
