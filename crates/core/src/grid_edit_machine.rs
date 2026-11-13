use choscordb_driver_api::{
    Body, EDITS_NEED_NO_TRANSACTION, Presentation, RecoveryHint, TransitionError, detail_or,
};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum EditPhase {
    Clean,
    Pending,
    Planning,
    Applying,
    Failed,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TransactionPhase {
    Inactive,
    Active,
    Ending,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct GridEditState {
    pub edits: EditPhase,
    pub transaction: TransactionPhase,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum GridEditEvent {
    Edit,
    Plan,
    Planned,
    PlanFailed,
    Apply,
    Applied,
    ApplyFailed,
    Discard,
    Begin,
    Commit,
    Rollback,
    Ended,
    EndFailed,
    Disconnect,
}

impl EditPhase {
    pub const ALL: [Self; 5] = [
        Self::Clean,
        Self::Pending,
        Self::Planning,
        Self::Applying,
        Self::Failed,
    ];
}

impl TransactionPhase {
    pub const ALL: [Self; 3] = [Self::Inactive, Self::Active, Self::Ending];
}

impl GridEditState {
    pub const ALL: [Self; 15] = {
        let mut all = [Self {
            edits: EditPhase::Clean,
            transaction: TransactionPhase::Inactive,
        }; 15];
        let mut index = 0;
        while index < 15 {
            all[index] = Self {
                edits: EditPhase::ALL[index / 3],
                transaction: TransactionPhase::ALL[index % 3],
            };
            index += 1;
        }
        all
    };
}

impl GridEditEvent {
    pub const ALL: [Self; 14] = [
        Self::Edit,
        Self::Plan,
        Self::Planned,
        Self::PlanFailed,
        Self::Apply,
        Self::Applied,
        Self::ApplyFailed,
        Self::Discard,
        Self::Begin,
        Self::Commit,
        Self::Rollback,
        Self::Ended,
        Self::EndFailed,
        Self::Disconnect,
    ];
}

pub type GridEditTransitionError = TransitionError<GridEditState, GridEditEvent>;

pub fn step(
    state: &GridEditState,
    event: &GridEditEvent,
) -> Result<GridEditState, GridEditTransitionError> {
    use EditPhase as P;
    use GridEditEvent as E;
    use TransactionPhase as T;
    let GridEditState { edits, transaction } = *state;
    let (edits, transaction) = match (edits, transaction, *event) {
        (P::Clean | P::Pending | P::Failed, t, E::Edit) => (P::Pending, t),
        (P::Pending, t, E::Plan) => (P::Planning, t),
        (P::Planning, t, E::Planned) => (P::Pending, t),
        (P::Planning, t, E::PlanFailed) => (P::Failed, t),
        (P::Pending, T::Inactive, E::Apply) => (P::Applying, T::Inactive),
        (P::Applying, t, E::Applied) => (P::Clean, t),
        (P::Applying, t, E::ApplyFailed) => (P::Failed, t),
        (P::Pending | P::Failed, t, E::Discard) => (P::Clean, t),
        (e, T::Inactive, E::Begin) if e != P::Applying => (e, T::Active),
        (e, T::Active, E::Commit | E::Rollback) if e != P::Applying => (e, T::Ending),
        (e, T::Ending, E::Ended) => (e, T::Inactive),
        (e, T::Ending, E::EndFailed) => (e, T::Active),
        (P::Planning | P::Applying, _, E::Disconnect) => (P::Failed, T::Inactive),
        (e, _, E::Disconnect) => (e, T::Inactive),
        _ => {
            return Err(TransitionError {
                state: *state,
                event: *event,
            });
        }
    };
    Ok(GridEditState { edits, transaction })
}

pub fn present(state: &GridEditState, detail: &str) -> Presentation {
    use EditPhase as P;
    use TransactionPhase as T;
    match (state.edits, state.transaction) {
        (P::Planning, _) => Presentation::busy(detail_or(detail, "Checking the changes…")),
        (P::Applying, _) => Presentation::busy(detail_or(detail, "Applying the changes…")),
        (_, T::Ending) => Presentation::busy(detail_or(detail, "Ending the transaction…")),
        (P::Failed, _) => Presentation {
            body: Body::None,
            ..Presentation::failed(
                "Could not apply the grid changes",
                detail,
                RecoveryHint::Retry,
            )
        },
        (P::Pending, T::Active) => Presentation::idle(EDITS_NEED_NO_TRANSACTION),
        (P::Pending, T::Inactive) => Presentation::idle(detail_or(detail, "Changes pending.")),
        (P::Clean, _) => Presentation::idle(detail.trim()),
    }
}
