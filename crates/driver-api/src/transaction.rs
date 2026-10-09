//! What a manual transaction blocks until it is committed or rolled back.

/// Grid edits run in their own transaction, so they cannot join a manual one.
pub const EDITS_NEED_NO_TRANSACTION: &str =
    "Commit or roll back the manual transaction before applying grid changes.";
/// Switching to auto-commit would silently end the open manual transaction.
pub const AUTO_COMMIT_NEEDS_NO_TRANSACTION: &str =
    "Commit or roll back before enabling auto-commit.";

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct TransactionGuard {
    pub apply_edits: Option<&'static str>,
    pub enable_auto_commit: Option<&'static str>,
}

pub fn transaction_guard(transaction_active: bool) -> TransactionGuard {
    TransactionGuard {
        apply_edits: transaction_active.then_some(EDITS_NEED_NO_TRANSACTION),
        enable_auto_commit: transaction_active.then_some(AUTO_COMMIT_NEEDS_NO_TRANSACTION),
    }
}
