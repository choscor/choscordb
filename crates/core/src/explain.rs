use choscordb_driver_api::{GridEditDriver, Row, Value};
use serde_json::{Map, Value as Json};
use std::collections::HashMap;

pub const MAX_PLAN_NODES: usize = 10_000;

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct PlanNode {
    pub depth: u32,
    pub operation: String,
    pub target: String,
    pub cost: String,
    pub rows: String,
    pub detail: String,
}

const UNREADABLE: &str = "The database returned a plan that ChoscorDB cannot read.";

pub fn explain_sql(driver: GridEditDriver, statement: &str) -> Result<String, String> {
    let statement = statement.trim().trim_end_matches(';').trim();
    if statement.is_empty() {
        return Err("Place the cursor in a statement to explain.".into());
    }
    if statement
        .split_whitespace()
        .next()
        .is_some_and(|word| word.eq_ignore_ascii_case("explain"))
    {
        return Err("The statement is already an EXPLAIN statement.".into());
    }
    let prefix = match driver {
        GridEditDriver::Postgres => "EXPLAIN (FORMAT JSON)",
        GridEditDriver::Mysql => "EXPLAIN FORMAT=JSON",
        GridEditDriver::Sqlite => "EXPLAIN QUERY PLAN",
    };
    Ok(format!("{prefix} {statement}"))
}

pub fn parse_explain(driver: GridEditDriver, rows: &[Row]) -> Result<Vec<PlanNode>, String> {
    let mut nodes = Vec::new();
    match driver {
        GridEditDriver::Sqlite => sqlite_plan(rows, &mut nodes)?,
        GridEditDriver::Postgres => {
            let plans = json_cell(rows)?;
            for plan in plans.as_array().ok_or(UNREADABLE)? {
                let root = plan
                    .get("Plan")
                    .and_then(Json::as_object)
                    .ok_or(UNREADABLE)?;
                postgres_node(root, 0, &mut nodes)?;
            }
        }
        GridEditDriver::Mysql => {
            let plan = json_cell(rows)?;
            for (key, value) in plan.as_object().ok_or(UNREADABLE)? {
                mysql_node(key, value, 0, &mut nodes)?;
            }
        }
    }
    if nodes.is_empty() {
        return Err(UNREADABLE.into());
    }
    Ok(nodes)
}

fn push(nodes: &mut Vec<PlanNode>, node: PlanNode) -> Result<(), String> {
    if nodes.len() >= MAX_PLAN_NODES {
        return Err(format!("The plan has more than {MAX_PLAN_NODES} steps."));
    }
    nodes.push(node);
    Ok(())
}

fn text(value: &Value) -> Option<&str> {
    match value {
        Value::Text(text) | Value::Json(text) => Some(text),
        _ => None,
    }
}

fn integer(value: &Value) -> Option<i64> {
    match value {
        Value::Integer(value) => Some(*value),
        _ => text(value).and_then(|text| text.parse().ok()),
    }
}

fn json_cell(rows: &[Row]) -> Result<Json, String> {
    let text = rows
        .first()
        .and_then(|row| row.first())
        .and_then(text)
        .ok_or(UNREADABLE)?;
    serde_json::from_str(text).map_err(|_| UNREADABLE.into())
}

fn sqlite_plan(rows: &[Row], nodes: &mut Vec<PlanNode>) -> Result<(), String> {
    let mut depths: HashMap<i64, u32> = HashMap::new();
    for row in rows {
        let [id, parent, _, detail] = row.as_slice() else {
            return Err(UNREADABLE.into());
        };
        let (Some(id), Some(parent), Some(detail)) = (integer(id), integer(parent), text(detail))
        else {
            return Err(UNREADABLE.into());
        };
        let depth = depths.get(&parent).map_or(0, |depth| depth + 1);
        depths.insert(id, depth);
        push(
            nodes,
            PlanNode {
                depth,
                operation: detail.to_owned(),
                ..PlanNode::default()
            },
        )?;
    }
    Ok(())
}

fn scalar(value: Option<&Json>) -> String {
    match value {
        Some(Json::String(text)) => text.clone(),
        Some(Json::Number(number)) => number.to_string(),
        Some(Json::Array(items)) => items
            .iter()
            .map(|item| scalar(Some(item)))
            .collect::<Vec<_>>()
            .join(", "),
        _ => String::new(),
    }
}

fn target(table: String, index: String) -> String {
    match (table.is_empty(), index.is_empty()) {
        (_, true) => table,
        (true, false) => index,
        (false, false) => format!("{table} using {index}"),
    }
}

const POSTGRES_DETAILS: [&str; 7] = [
    "Join Type",
    "Filter",
    "Index Cond",
    "Hash Cond",
    "Merge Cond",
    "Sort Key",
    "Group Key",
];

fn postgres_node(
    plan: &Map<String, Json>,
    depth: u32,
    nodes: &mut Vec<PlanNode>,
) -> Result<(), String> {
    let number = |key: &str| plan.get(key).and_then(Json::as_f64);
    let cost = match (number("Startup Cost"), number("Total Cost")) {
        (Some(startup), Some(total)) => format!("{startup:.2}..{total:.2}"),
        _ => String::new(),
    };
    let detail = POSTGRES_DETAILS
        .iter()
        .filter_map(|key| {
            let value = scalar(plan.get(*key));
            (!value.is_empty()).then(|| format!("{key}: {value}"))
        })
        .collect::<Vec<_>>()
        .join(" · ");
    push(
        nodes,
        PlanNode {
            depth,
            operation: scalar(plan.get("Node Type")),
            target: target(
                scalar(plan.get("Relation Name")),
                scalar(plan.get("Index Name")),
            ),
            cost,
            rows: scalar(plan.get("Plan Rows")),
            detail,
        },
    )?;
    for child in plan
        .get("Plans")
        .and_then(Json::as_array)
        .into_iter()
        .flatten()
    {
        postgres_node(child.as_object().ok_or(UNREADABLE)?, depth + 1, nodes)?;
    }
    Ok(())
}

fn mysql_node(
    key: &str,
    value: &Json,
    depth: u32,
    nodes: &mut Vec<PlanNode>,
) -> Result<(), String> {
    match value {
        Json::Object(fields) => {
            let mut operation = key.replace('_', " ");
            if let Some(access) = fields.get("access_type").and_then(Json::as_str) {
                operation = format!("{operation} ({access})");
            }
            let costs = fields.get("cost_info");
            let cost = ["query_cost", "prefix_cost"]
                .iter()
                .map(|name| scalar(costs.and_then(|costs| costs.get(*name))))
                .find(|cost| !cost.is_empty())
                .unwrap_or_default();
            let condition = scalar(fields.get("attached_condition"));
            push(
                nodes,
                PlanNode {
                    depth,
                    operation,
                    target: target(scalar(fields.get("table_name")), scalar(fields.get("key"))),
                    cost,
                    rows: scalar(fields.get("rows_examined_per_scan")),
                    detail: if condition.is_empty() {
                        condition
                    } else {
                        format!("Condition: {condition}")
                    },
                },
            )?;
            for (child, value) in fields {
                if child != "cost_info" {
                    mysql_children(child, value, depth + 1, nodes)?;
                }
            }
        }
        Json::Array(_) => mysql_children(key, value, depth, nodes)?,
        _ => {}
    }
    Ok(())
}

fn mysql_children(
    key: &str,
    value: &Json,
    depth: u32,
    nodes: &mut Vec<PlanNode>,
) -> Result<(), String> {
    match value {
        Json::Object(_) => mysql_node(key, value, depth, nodes),
        Json::Array(items) if items.iter().any(Json::is_object) => {
            push(
                nodes,
                PlanNode {
                    depth,
                    operation: key.replace('_', " "),
                    ..PlanNode::default()
                },
            )?;
            for item in items {
                match item.as_object() {
                    Some(wrapper) if wrapper.len() == 1 => {
                        for (inner, value) in wrapper {
                            mysql_node(inner, value, depth + 1, nodes)?;
                        }
                    }
                    Some(_) => mysql_node(key, item, depth + 1, nodes)?,
                    None => {}
                }
            }
            Ok(())
        }
        _ => Ok(()),
    }
}
