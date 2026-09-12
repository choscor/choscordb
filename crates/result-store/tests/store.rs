use choscordb_driver_api::*;
use choscordb_result_store::*;
fn schema() -> Vec<Column> {
    vec![Column {
        name: "value".into(),
        database_type: "NUMERIC".into(),
        precision: Some(38),
        scale: Some(18),
        timezone: Some("UTC".into()),
        nullable: Some(true),
    }]
}
fn page(index: u64, value: Value, more: bool) -> ResultPage {
    ResultPage {
        index,
        rows: vec![vec![value]],
        has_more: more,
    }
}

#[test]
fn exact_values_and_schema_survive_random_access_and_cleanup() {
    let parent = tempfile::tempdir().unwrap();
    let directory;
    {
        let mut store =
            ResultStore::in_directory(parent.path(), &schema(), StoreConfig::default()).unwrap();
        directory = store.directory().to_owned();
        let values = [
            Value::Null,
            Value::Text("".into()),
            Value::Decimal("12345678901234567890.123456789".into()),
            Value::Binary(vec![0, 255]),
            Value::Deferred {
                handle: Handle {
                    slot: 4,
                    generation: 8,
                },
                byte_length: 1_000_000,
                database_type: "BLOB".into(),
            },
        ];
        for (i, value) in values.iter().enumerate() {
            store
                .append(&page(i as u64, value.clone(), i + 1 < values.len()))
                .unwrap();
        }
        assert_eq!(store.schema().unwrap(), schema());
        for i in (0..values.len()).rev() {
            let stored = store.read_page(i as u64).unwrap();
            assert_eq!(stored.first_row, i as u64);
            assert_eq!(stored.page.rows[0][0], values[i]);
        }
        assert_eq!(store.page_count(), 5);
        assert!(matches!(store.read_page(5), Err(StoreError::PageMissing)));
    }
    assert!(!directory.exists());
}
#[test]
fn one_million_rows_have_constant_index_memory_and_backward_reads() {
    let mut store = ResultStore::new(&schema(), StoreConfig::default()).unwrap();
    for index in 0..1000 {
        let rows = (0..1000)
            .map(|n| vec![Value::Integer((index * 1000 + n) as i64)])
            .collect();
        store
            .append(&ResultPage {
                index,
                rows,
                has_more: index < 999,
            })
            .unwrap();
    }
    assert_eq!(store.row_count(), 1_000_000);
    assert_eq!(
        std::fs::metadata(store.directory().join("index"))
            .unwrap()
            .len(),
        1000 * INDEX_RECORD_BYTES as u64
    );
    for index in [999, 0, 500, 1] {
        let stored = store.read_page(index).unwrap();
        assert_eq!(stored.first_row, index * 1000);
        assert_eq!(
            stored.page.rows[999][0],
            Value::Integer((index * 1000 + 999) as i64)
        );
    }
}
#[test]
fn disk_limit_does_not_publish_or_damage_failed_append() {
    let mut store = ResultStore::new(
        &schema(),
        StoreConfig {
            max_disk_bytes: 500,
            ..Default::default()
        },
    )
    .unwrap();
    store.append(&page(0, Value::Integer(1), true)).unwrap();
    let prior = store.disk_bytes();
    assert!(matches!(
        store.append(&page(1, Value::Text("x".repeat(1000)), true)),
        Err(StoreError::LimitExceeded)
    ));
    assert_eq!(store.page_count(), 1);
    assert_eq!(store.disk_bytes(), prior);
    let actual_bytes: u64 = ["pages", "index", "schema"]
        .iter()
        .map(|name| {
            std::fs::metadata(store.directory().join(name))
                .unwrap()
                .len()
        })
        .sum();
    assert_eq!(
        actual_bytes, prior,
        "failed append must remove unpublished physical bytes"
    );
    assert_eq!(
        store.read_page(0).unwrap().page.rows[0][0],
        Value::Integer(1)
    );
    store.append(&page(1, Value::Integer(2), false)).unwrap();
}
#[test]
fn corrupt_index_and_payload_fail_without_allocation_bombs() {
    use std::io::{Seek, SeekFrom, Write};
    let mut store = ResultStore::new(&schema(), StoreConfig::default()).unwrap();
    store
        .append(&page(0, Value::Text("hello".into()), false))
        .unwrap();
    let mut data = std::fs::OpenOptions::new()
        .write(true)
        .open(store.directory().join("pages"))
        .unwrap();
    data.seek(SeekFrom::Start(0)).unwrap();
    data.write_all(&u64::MAX.to_le_bytes()).unwrap();
    assert!(matches!(store.read_page(0), Err(StoreError::Corrupt)));
    let mut index = std::fs::OpenOptions::new()
        .write(true)
        .open(store.directory().join("index"))
        .unwrap();
    index.seek(SeekFrom::Start(8)).unwrap();
    index.write_all(&u64::MAX.to_le_bytes()).unwrap();
    assert!(matches!(store.read_page(0), Err(StoreError::Corrupt)));
}

#[test]
fn all_scalar_variants_and_ieee_float_bits_round_trip() {
    let values = vec![
        Value::Bool(true),
        Value::Integer(i64::MIN),
        Value::Real(-0.0),
        Value::Real(f64::INFINITY),
        Value::Real(f64::from_bits(0x7ff8000000000042)),
        Value::Date("2026-09-10".into()),
        Value::Time("12:34:56".into()),
        Value::Timestamp("2026-09-10T12:34:56+07:00".into()),
        Value::Uuid("a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11".into()),
        Value::Json("{\"n\":12345678901234567890}".into()),
    ];
    let mut store = ResultStore::new(&schema(), StoreConfig::default()).unwrap();
    for (i, value) in values.iter().enumerate() {
        store
            .append(&page(i as u64, value.clone(), i + 1 < values.len()))
            .unwrap();
    }
    for (i, expected) in values.iter().enumerate() {
        let actual = store
            .read_page(i as u64)
            .unwrap()
            .page
            .rows
            .remove(0)
            .remove(0);
        if let (Value::Real(a), Value::Real(b)) = (&actual, expected) {
            assert_eq!(a.to_bits(), b.to_bits());
        } else {
            assert_eq!(&actual, expected);
        }
    }
    assert!(store.is_complete());
    assert!(matches!(
        store.append(&page(10, Value::Null, false)),
        Err(StoreError::InvalidPage)
    ));
    let path = store.directory().to_owned();
    store.close().unwrap();
    assert!(!path.exists());
}

#[test]
fn checksum_catches_shape_preserving_corruption_and_truncated_files() {
    use std::io::{Read, Seek, SeekFrom, Write};
    let mut store = ResultStore::new(&schema(), StoreConfig::default()).unwrap();
    store
        .append(&page(0, Value::Text("hello".into()), false))
        .unwrap();
    let path = store.directory().join("pages");
    let mut file = std::fs::OpenOptions::new()
        .read(true)
        .write(true)
        .open(path)
        .unwrap();
    let mut bytes = vec![];
    file.read_to_end(&mut bytes).unwrap();
    let last = bytes.len() - 1;
    bytes[last] = b'!';
    file.seek(SeekFrom::Start(0)).unwrap();
    file.write_all(&bytes).unwrap();
    assert!(matches!(store.read_page(0), Err(StoreError::Corrupt)));
    file.set_len(3).unwrap();
    assert!(matches!(store.read_page(0), Err(StoreError::Corrupt)));
    std::fs::OpenOptions::new()
        .write(true)
        .open(store.directory().join("schema"))
        .unwrap()
        .set_len(2)
        .unwrap();
    assert!(matches!(store.schema(), Err(StoreError::Corrupt)));
}

#[test]
fn invalid_shapes_and_decoded_memory_caps_leave_store_usable() {
    let mut store = ResultStore::new(
        &schema(),
        StoreConfig {
            max_page_decoded_bytes: 512,
            ..Default::default()
        },
    )
    .unwrap();
    assert!(matches!(
        store.append(&ResultPage {
            index: 0,
            rows: vec![vec![]],
            has_more: true
        }),
        Err(StoreError::InvalidPage)
    ));
    assert!(matches!(
        store.append(&page(1, Value::Null, true)),
        Err(StoreError::InvalidPage)
    ));
    assert!(matches!(
        store.append(&page(0, Value::Text("x".repeat(1000)), true)),
        Err(StoreError::LimitExceeded)
    ));
    store.append(&page(0, Value::Null, false)).unwrap();
    assert_eq!(store.page_count(), 1);
}
