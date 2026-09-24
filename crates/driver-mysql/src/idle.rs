//! Conservative, wire-free idle transaction snapshots for the primary session.
use choscordb_driver_api::IdleTransactionState;
use mysql_async::Conn;
use std::{
    sync::{Arc, Mutex},
    time::Instant,
};
#[derive(Clone)]
pub(super) struct Tracker(Arc<Mutex<Option<IdleTransactionState>>>);
impl Default for Tracker {
    fn default() -> Self {
        Self(Arc::new(Mutex::new(Some(IdleTransactionState::default()))))
    }
}
impl Tracker {
    pub fn snapshot(&self) -> Option<IdleTransactionState> {
        self.0.lock().ok().and_then(|value| *value)
    }
    pub fn reconcile(&self, connection: &Conn) {
        if let Ok(mut state) = self.0.lock() {
            match super::transaction_active(connection) {
                Some(false) => *state = Some(IdleTransactionState::default()),
                Some(true) => {}
                None => *state = None,
            }
        }
    }
    pub fn observe(&self, connection: &Conn, sql: &str, started: Instant) {
        let Ok(mut state) = self.0.lock() else {
            return;
        };
        match super::transaction_active(connection) {
            Some(false) => {
                *state = Some(IdleTransactionState::default());
                return;
            }
            None => {
                *state = None;
                return;
            }
            Some(true) => {}
        }
        let Some(words) = words(sql) else {
            *state = None;
            return;
        };
        let first = words.first().map(String::as_str).unwrap_or("");
        let boundary = matches!(first, "BEGIN" | "START" | "COMMIT")
            || (first == "ROLLBACK" && !words.iter().any(|word| word == "TO"));
        let write = matches!(first, "INSERT" | "UPDATE" | "DELETE" | "REPLACE" | "LOAD");
        let known = boundary
            || write
            || matches!(
                first,
                "SELECT"
                    | "SHOW"
                    | "DESCRIBE"
                    | "DESC"
                    | "EXPLAIN"
                    | "SET"
                    | "SAVEPOINT"
                    | "RELEASE"
                    | "CREATE"
                    | "DROP"
                    | "ALTER"
                    | "TRUNCATE"
            );
        if !known {
            *state = None;
            return;
        }
        if boundary || state.is_some_and(|state| !state.active) {
            *state = Some(IdleTransactionState {
                active: true,
                manual: true,
                write_pending: false,
                started_at: Some(started),
            });
        }
        if let Some(state) = state.as_mut()
            && write
        {
            state.write_pending = true;
        }
    }
}
// Only inspect complete leading words, skipping ordinary comments. Executable
// MySQL comments and unsupported forms remain unknown, never rollback-eligible.
fn words(mut sql: &str) -> Option<Vec<String>> {
    // A query can contain several server statements; do not infer a generation
    // across ambiguous mode-dependent semicolon escaping.
    if sql.trim_end().trim_end_matches(';').contains(';') {
        return None;
    }
    if choscordb_sql_language::statement_ranges_mysql_with_mode(sql, Default::default()).len() != 1
    {
        return None;
    }
    let mut words = Vec::new();
    loop {
        sql = sql.trim_start();
        if let Some(rest) = sql.strip_prefix("/*") {
            if rest.starts_with(['!', '+']) {
                return None;
            }
            sql = &rest[rest.find("*/")? + 2..];
            continue;
        }
        if sql.starts_with('#')
            || (sql.starts_with("--") && sql.as_bytes().get(2).is_some_and(u8::is_ascii_whitespace))
        {
            sql = sql.split_once('\n').map_or("", |(_, tail)| tail);
            continue;
        }
        let end = sql
            .find(|c: char| !c.is_ascii_alphabetic() && c != '_')
            .unwrap_or(sql.len());
        if end == 0 {
            break;
        }
        words.push(sql[..end].to_ascii_uppercase());
        sql = &sql[end..];
        if words.len() == 4 {
            break;
        }
    }
    Some(words)
}
