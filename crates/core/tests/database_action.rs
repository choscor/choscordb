use choscordb_core::{DatabaseActionKind, DatabaseActionRequest, plan_database_action};
use choscordb_driver_api::ErrorKind;

fn request(driver: &str, kind: DatabaseActionKind) -> DatabaseActionRequest {
    DatabaseActionRequest {
        driver: driver.into(),
        kind,
        current: "app".into(),
        name: "sales".into(),
        new_name: String::new(),
        encoding: String::new(),
        collation: String::new(),
        confirmation: String::new(),
    }
}

#[test]
fn create_quotes_the_name_and_adds_encoding_and_collation() {
    assert_eq!(
        plan_database_action(&request("postgres", DatabaseActionKind::Create)).unwrap(),
        "CREATE DATABASE \"sales\";"
    );
    let postgres = DatabaseActionRequest {
        name: "a\"b".into(),
        encoding: "UTF8".into(),
        collation: "en_US.UTF-8".into(),
        ..request("postgres", DatabaseActionKind::Create)
    };
    assert_eq!(
        plan_database_action(&postgres).unwrap(),
        "CREATE DATABASE \"a\"\"b\" TEMPLATE template0 ENCODING 'UTF8' LC_COLLATE 'en_US.UTF-8' LC_CTYPE 'en_US.UTF-8';"
    );
    let mysql = DatabaseActionRequest {
        name: "a`b".into(),
        encoding: "utf8mb4".into(),
        collation: "utf8mb4_unicode_ci".into(),
        ..request("mariadb", DatabaseActionKind::Create)
    };
    assert_eq!(
        plan_database_action(&mysql).unwrap(),
        "CREATE DATABASE `a``b` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;"
    );
    let unsafe_encoding = DatabaseActionRequest {
        encoding: "UTF8'; DROP".into(),
        ..request("postgres", DatabaseActionKind::Create)
    };
    assert_eq!(
        plan_database_action(&unsafe_encoding).unwrap_err().message,
        "An encoding or a collation has only letters, digits, dots, hyphens, underscores and @."
    );
    let empty = DatabaseActionRequest {
        name: " ".into(),
        ..request("mysql", DatabaseActionKind::Create)
    };
    assert_eq!(
        plan_database_action(&empty).unwrap_err().message,
        "Enter a database name."
    );
}

#[test]
fn rename_and_drop_need_another_database_and_drop_needs_the_typed_name() {
    let rename = DatabaseActionRequest {
        new_name: "archive".into(),
        ..request("postgres", DatabaseActionKind::Rename)
    };
    assert_eq!(
        plan_database_action(&rename).unwrap(),
        "ALTER DATABASE \"sales\" RENAME TO \"archive\";"
    );
    assert_eq!(
        plan_database_action(&DatabaseActionRequest {
            name: "app".into(),
            ..rename.clone()
        })
        .unwrap_err()
        .message,
        "Switch to another database before you rename or drop this one."
    );
    assert_eq!(
        plan_database_action(&DatabaseActionRequest {
            new_name: "sales".into(),
            ..rename
        })
        .unwrap_err()
        .message,
        "Enter a new name that is different from the old name."
    );
    let drop = request("mysql", DatabaseActionKind::Drop);
    assert_eq!(
        plan_database_action(&drop).unwrap_err().message,
        "Type the database name sales to drop it."
    );
    assert_eq!(
        plan_database_action(&DatabaseActionRequest {
            confirmation: "sales".into(),
            ..drop
        })
        .unwrap(),
        "DROP DATABASE `sales`;"
    );
}

#[test]
fn unsupported_drivers_and_actions_return_a_typed_unsupported_error() {
    for kind in DatabaseActionKind::ALL {
        assert_eq!(
            plan_database_action(&request("sqlite", kind))
                .unwrap_err()
                .kind,
            ErrorKind::Unsupported
        );
        assert_eq!(
            plan_database_action(&request("sqlite", kind))
                .unwrap_err()
                .message,
            "SQLite keeps each database in its own file. Create, rename or delete the file instead."
        );
    }
    assert_eq!(
        plan_database_action(&DatabaseActionRequest {
            new_name: "archive".into(),
            ..request("mysql", DatabaseActionKind::Rename)
        })
        .unwrap_err()
        .message,
        "MySQL and MariaDB cannot rename a database."
    );
    assert_eq!(
        plan_database_action(&DatabaseActionRequest {
            new_name: "archive".into(),
            ..request("mariadb", DatabaseActionKind::Rename)
        })
        .unwrap_err()
        .kind,
        ErrorKind::Unsupported
    );
    assert_eq!(
        plan_database_action(&request("oracle", DatabaseActionKind::Create))
            .unwrap_err()
            .kind,
        ErrorKind::Unsupported
    );
    assert_eq!(
        plan_database_action(&DatabaseActionRequest {
            name: " ".into(),
            ..request("postgres", DatabaseActionKind::Create)
        })
        .unwrap_err()
        .kind,
        ErrorKind::InvalidInput
    );
    assert_eq!(
        DatabaseActionKind::from_code("drop"),
        Some(DatabaseActionKind::Drop)
    );
    assert_eq!(DatabaseActionKind::Create.label(), "New database");
}
