pub const EDITS_NEED_NO_TRANSACTION: &str =
    "Commit or roll back the open transaction before applying grid changes.";

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct TransactionGuard {
    pub apply_edits: Option<&'static str>,
}

pub fn transaction_guard(transaction_active: bool) -> TransactionGuard {
    TransactionGuard {
        apply_edits: transaction_active.then_some(EDITS_NEED_NO_TRANSACTION),
    }
}
