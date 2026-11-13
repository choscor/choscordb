use crate::{RecoveryHint, RetryAction, recovery_hint};
use std::fmt;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Tone {
    Neutral,
    Success,
    Error,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Body {
    Content,
    Empty,
    NoMatch,
    FailedPlaceholder,
    None,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Pane {
    Grid,
    Messages,
    Unchanged,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ToastVariant {
    Error,
    Success,
    Warning,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ToastSpec {
    pub variant: ToastVariant,
    pub text: String,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Presentation {
    pub tone: Tone,
    pub busy: bool,
    pub body: Body,
    pub pane: Pane,
    pub retry: RetryAction,
    pub message: String,
    pub toast: Option<ToastSpec>,
}

impl Presentation {
    pub fn idle(message: impl Into<String>) -> Self {
        Self {
            tone: Tone::Neutral,
            busy: false,
            body: Body::None,
            pane: Pane::Unchanged,
            retry: RetryAction::None,
            message: message.into(),
            toast: None,
        }
    }

    pub fn busy(message: impl Into<String>) -> Self {
        Self {
            busy: true,
            ..Self::idle(message)
        }
    }

    pub fn with_tone(self, tone: Tone) -> Self {
        Self { tone, ..self }
    }

    pub fn with_body(self, body: Body) -> Self {
        Self { body, ..self }
    }

    pub fn with_pane(self, pane: Pane) -> Self {
        Self { pane, ..self }
    }

    pub fn failed(failure: &str, detail: &str, hint: RecoveryHint) -> Self {
        Self {
            tone: Tone::Error,
            body: Body::FailedPlaceholder,
            retry: RetryAction::Refresh,
            ..Self::idle(failure_message(failure, detail, hint))
        }
    }

    pub fn with_error_toast(self) -> Self {
        self.with_toast(ToastVariant::Error)
    }

    pub fn with_toast(self, variant: ToastVariant) -> Self {
        let text = self.message.clone();
        Self {
            toast: Some(ToastSpec { variant, text }),
            ..self
        }
    }
}

pub fn failure_message(failure: &str, detail: &str, hint: RecoveryHint) -> String {
    let hint = recovery_hint(hint);
    match detail.trim().trim_end_matches('.') {
        "" => format!("{failure}. {hint}"),
        reason => format!("{failure}: {reason}. {hint}"),
    }
}

pub fn detail_or<'a>(detail: &'a str, default: &'a str) -> &'a str {
    match detail.trim() {
        "" => default,
        detail => detail,
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct TransitionError<State, Event> {
    pub state: State,
    pub event: Event,
}

impl<State: fmt::Debug, Event: fmt::Debug> fmt::Display for TransitionError<State, Event> {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            formatter,
            "{:?} is not a valid event in the {:?} state",
            self.event, self.state
        )
    }
}

impl<State: fmt::Debug, Event: fmt::Debug> std::error::Error for TransitionError<State, Event> {}
