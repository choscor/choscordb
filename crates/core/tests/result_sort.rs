use choscordb_core::{
    Event, MAX_SORT_KEYS, ResultSort, SortDirection, toggle_sort, validate_sorts,
};
use choscordb_driver_api::{ConnectionOptions, PageSize, QueryOptions, Value};
use choscordb_driver_sqlite::SqliteDriver;
use std::{
    sync::Arc,
    time::{Duration, Instant},
};

fn asc(column: usize) -> ResultSort {
    ResultSort {
        column,
        direction: SortDirection::Ascending,
    }
}
fn desc(column: usize) -> ResultSort {
    ResultSort {
        column,
        direction: SortDirection::Descending,
    }
}

#[test]
fn a_plain_click_cycles_one_column() {
    assert_eq!(toggle_sort(&[], 2, false).unwrap(), vec![asc(2)]);
    assert_eq!(toggle_sort(&[asc(2)], 2, false).unwrap(), vec![desc(2)]);
    assert_eq!(toggle_sort(&[desc(2)], 2, false).unwrap(), vec![]);
    assert_eq!(
        toggle_sort(&[asc(2), desc(4)], 3, false).unwrap(),
        vec![asc(3)]
    );
    assert_eq!(
        toggle_sort(&[asc(2), asc(4)], 4, false).unwrap(),
        vec![desc(4)]
    );
}

#[test]
fn an_additive_click_keeps_the_other_keys_in_order() {
    assert_eq!(
        toggle_sort(&[asc(2)], 3, true).unwrap(),
        vec![asc(2), asc(3)]
    );
    assert_eq!(
        toggle_sort(&[asc(2), asc(3)], 3, true).unwrap(),
        vec![asc(2), desc(3)]
    );
    assert_eq!(
        toggle_sort(&[asc(2), desc(3)], 3, true).unwrap(),
        vec![asc(2)]
    );
    assert_eq!(
        toggle_sort(&[asc(2), desc(3)], 2, true).unwrap(),
        vec![desc(2), desc(3)]
    );
    let full: Vec<_> = (0..MAX_SORT_KEYS).map(asc).collect();
    assert!(toggle_sort(&full, MAX_SORT_KEYS, true).is_err());
}

#[test]
fn duplicate_or_too_many_keys_are_rejected() {
    assert!(validate_sorts(&[asc(1), desc(1)]).is_err());
    let too_many: Vec<_> = (0..=MAX_SORT_KEYS).map(asc).collect();
    assert!(validate_sorts(&too_many).is_err());
    assert!(validate_sorts(&[asc(1), desc(0)]).is_ok());
}

fn event(engine: &mut choscordb_core::Engine, wanted: impl Fn(&Event) -> bool) -> Event {
    let deadline = Instant::now() + Duration::from_secs(5);
    loop {
        if let Some(event) = engine.try_event() {
            if let Event::QueryFailed { error, .. } | Event::ResultViewFailed { error, .. } = &event
            {
                panic!("{error}");
            }
            if wanted(&event) {
                return event;
            }
        }
        assert!(Instant::now() < deadline, "event timed out");
        std::thread::sleep(Duration::from_millis(2));
    }
}

#[test]
fn rows_sort_by_each_key_in_order() {
    let mut engine =
        choscordb_core::Engine::new(Default::default(), vec![Arc::new(SqliteDriver)]).unwrap();
    let connection = engine
        .connect(
            "sqlite",
            ConnectionOptions::Sqlite {
                path: ":memory:".into(),
                read_only: false,
            },
        )
        .unwrap();
    let query = engine
        .execute(
            connection,
            "VALUES (1,'b'),(2,'a'),(1,'c'),(2,'z'),(1,NULL)".into(),
            QueryOptions {
                page_size: PageSize::new(100).unwrap(),
                ..Default::default()
            },
        )
        .unwrap();
    event(&mut engine, |e| matches!(e, Event::Schema { .. }));
    engine
        .apply_result_view(query, vec![], vec![asc(0), desc(1)])
        .unwrap();
    event(&mut engine, |e| {
        matches!(e, Event::ResultViewApplied { .. })
    });
    engine.fetch_page_at(query, 0).unwrap();
    let Event::StoredPage { page, .. } =
        event(&mut engine, |e| matches!(e, Event::StoredPage { .. }))
    else {
        unreachable!()
    };
    let text = |value: &Value| match value {
        Value::Text(text) => text.clone(),
        Value::Integer(number) => number.to_string(),
        Value::Null => "null".into(),
        _ => panic!("unexpected value"),
    };
    let rows: Vec<String> = page
        .rows
        .iter()
        .map(|row| format!("{}:{}", text(&row[0]), text(&row[1])))
        .collect();
    assert_eq!(rows, ["1:c", "1:b", "1:null", "2:z", "2:a"]);
}
