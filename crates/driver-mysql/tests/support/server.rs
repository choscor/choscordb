//! The disposable server for live tests: MySQL on `CHOSCORDB_MYSQL_PORT`
//! (default 33306), or MariaDB when `CHOSCORDB_MARIADB_PORT` is set.
use choscordb_driver_api::*;
use std::sync::Arc;

pub fn mariadb() -> bool {
    std::env::var_os("CHOSCORDB_MARIADB_PORT").is_some()
}

pub fn port() -> u16 {
    std::env::var("CHOSCORDB_MARIADB_PORT")
        .or_else(|_| std::env::var("CHOSCORDB_MYSQL_PORT"))
        .unwrap_or_else(|_| "33306".into())
        .parse()
        .unwrap()
}

pub fn driver() -> Arc<dyn DatabaseDriver> {
    if mariadb() {
        Arc::new(choscordb_driver_mysql::MariadbDriver)
    } else {
        Arc::new(choscordb_driver_mysql::MysqlDriver)
    }
}

pub fn options(tls: TlsMode) -> ConnectionOptions {
    let (host, port, database, user) = (
        "127.0.0.1".into(),
        port(),
        "choscordb_test".into(),
        "root".into(),
    );
    let password = Some(Secret::new("choscordb-test-password"));
    if mariadb() {
        ConnectionOptions::Mariadb {
            proxy: None,
            proxy_secret: None,
            host,
            port,
            database,
            user,
            password,
            ssh_secret: None,
            ssh_jump_secrets: Default::default(),
            ssh_private_key: None,
            ssh_jump_private_keys: Default::default(),
            tls,
            root_certificate: None,
            tls_identity: None,
            ssh: None,
        }
    } else {
        ConnectionOptions::Mysql {
            proxy: None,
            proxy_secret: None,
            host,
            port,
            database,
            user,
            password,
            ssh_secret: None,
            ssh_jump_secrets: Default::default(),
            ssh_private_key: None,
            ssh_jump_private_keys: Default::default(),
            tls,
            root_certificate: None,
            tls_identity: None,
            ssh: None,
        }
    }
}
