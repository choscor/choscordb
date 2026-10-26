//! The server family a connection was configured for. MySQL and MariaDB share
//! the wire protocol; only catalog queries, value decoding, authentication
//! errors and MariaDB-only features depend on it.
use choscordb_driver_api::ObjectKind;

/// The navigator kind and relation subtype for an `information_schema.TABLES` row.
pub(crate) struct Relation {
    pub kind: ObjectKind,
    pub subtype: Option<&'static str>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum Flavor {
    Mysql,
    Mariadb,
}

impl Flavor {
    pub(crate) fn id(self) -> &'static str {
        match self {
            Self::Mysql => "mysql",
            Self::Mariadb => "mariadb",
        }
    }

    /// The family a `VERSION()` string reports, such as `11.4.13-MariaDB-log`.
    pub(crate) fn of_server(version: &str) -> Self {
        if version.contains("-MariaDB") {
            Self::Mariadb
        } else {
            Self::Mysql
        }
    }

    /// Whether an `information_schema.TABLES` type stores rows in a base table,
    /// as a system-versioned MariaDB table does.
    pub(crate) fn base_table(self, table_type: &str) -> bool {
        table_type == "BASE TABLE" || (self == Self::Mariadb && table_type == "SYSTEM VERSIONED")
    }

    pub(crate) fn relation(self, table_type: &str) -> Relation {
        let (kind, subtype) = match (self, table_type) {
            (_, "VIEW") | (Self::Mariadb, "SYSTEM VIEW") => (ObjectKind::View, None),
            (Self::Mariadb, "SEQUENCE") => (ObjectKind::Sequence, None),
            (Self::Mariadb, "SYSTEM VERSIONED") => (ObjectKind::Table, Some("system_versioned")),
            _ => (ObjectKind::Table, None),
        };
        Relation { kind, subtype }
    }

    /// The notice for a connection whose server is another family. The
    /// connection keeps this flavor's behavior.
    pub(crate) fn mismatch_notice(self, server: Self) -> Option<&'static str> {
        match (self, server) {
            (Self::Mysql, Self::Mariadb) => {
                Some("This server is MariaDB; use a MariaDB connection for MariaDB features")
            }
            (Self::Mariadb, Self::Mysql) => {
                Some("This server is MySQL; use a MySQL connection for MySQL behavior")
            }
            _ => None,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::Flavor;

    #[test]
    fn server_family_comes_from_the_version_suffix() {
        assert_eq!(Flavor::of_server("11.4.13-MariaDB"), Flavor::Mariadb);
        assert_eq!(Flavor::of_server("10.11.19-MariaDB-log"), Flavor::Mariadb);
        assert_eq!(Flavor::of_server("8.4.6"), Flavor::Mysql);
        assert_eq!(Flavor::of_server("8.0.36-commercial"), Flavor::Mysql);
    }

    #[test]
    fn only_a_family_mismatch_produces_a_notice() {
        assert!(
            Flavor::Mysql
                .mismatch_notice(Flavor::Mariadb)
                .unwrap()
                .contains("use a MariaDB connection")
        );
        assert!(
            Flavor::Mariadb
                .mismatch_notice(Flavor::Mysql)
                .unwrap()
                .contains("use a MySQL connection")
        );
        assert_eq!(Flavor::Mysql.mismatch_notice(Flavor::Mysql), None);
        assert_eq!(Flavor::Mariadb.mismatch_notice(Flavor::Mariadb), None);
    }
}
