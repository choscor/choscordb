use choscordb_driver_api::Presentation;
use std::collections::{BTreeMap, BTreeSet};
use std::fmt::Debug;

pub struct Table {
    states: BTreeSet<String>,
    events: BTreeSet<String>,
    transitions: BTreeSet<(String, String, String)>,
    presentation: BTreeMap<String, serde_json::Value>,
    json: serde_json::Value,
}

pub fn load(family: &str) -> Table {
    let path = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../../specs")
        .join(family)
        .join("transitions.json");
    let text = std::fs::read_to_string(&path)
        .unwrap_or_else(|error| panic!("{}: {error}", path.display()));
    let json: serde_json::Value = serde_json::from_str(&text).unwrap();
    let names = |key: &str| -> BTreeSet<String> {
        json[key]
            .as_array()
            .unwrap()
            .iter()
            .map(|name| name.as_str().unwrap().to_owned())
            .collect()
    };
    Table {
        states: names("states"),
        events: names("events"),
        transitions: json["transitions"]
            .as_array()
            .unwrap()
            .iter()
            .map(|row| {
                let cell = |index: usize| row[index].as_str().unwrap().to_owned();
                (cell(0), cell(1), cell(2))
            })
            .collect(),
        presentation: json["presentation"]
            .as_object()
            .into_iter()
            .flatten()
            .map(|(state, row)| (state.clone(), row.clone()))
            .collect(),
        json,
    }
}

fn name(value: &impl Debug) -> String {
    format!("{value:?}")
}

impl Table {
    pub fn rows(&self, key: &str) -> BTreeSet<String> {
        self.json[key]
            .as_array()
            .unwrap_or_else(|| panic!("the table has no {key}"))
            .iter()
            .map(|row| row.to_string())
            .collect()
    }

    pub fn assert_step<S: Debug, E: Debug, F: Debug>(
        &self,
        states: &[S],
        events: &[E],
        step: impl Fn(&S, &E) -> Result<S, F>,
    ) {
        assert_eq!(
            states.iter().map(name).collect::<BTreeSet<_>>(),
            self.states
        );
        assert_eq!(
            events.iter().map(name).collect::<BTreeSet<_>>(),
            self.events
        );
        let mut actual = BTreeSet::new();
        for state in states {
            for event in events {
                if let Ok(next) = step(state, event) {
                    actual.insert((name(state), name(event), name(&next)));
                }
            }
        }
        let missing: Vec<_> = self.transitions.difference(&actual).collect();
        let extra: Vec<_> = actual.difference(&self.transitions).collect();
        assert!(
            missing.is_empty() && extra.is_empty(),
            "step disagrees with the spec table\nmissing from step: {missing:#?}\nnot in the table: {extra:#?}"
        );
    }

    pub fn assert_present<S: Debug>(&self, states: &[S], present: impl Fn(&S) -> Presentation) {
        for state in states {
            let shown = present(state);
            let row = &self.presentation[&name(state)];
            let actual = serde_json::json!({
                "tone": name(&shown.tone),
                "busy": shown.busy,
                "body": name(&shown.body),
                "pane": name(&shown.pane),
                "toast": shown.toast.is_some(),
            });
            assert_eq!(&actual, row, "presentation of {state:?}");
        }
    }
}
