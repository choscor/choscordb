use super::{convert, ffi};
use choscordb_core::quick_filter::{self, Operator};
fn operator(value: ffi::QuickFilterOperator) -> Option<Operator> {
    use ffi::QuickFilterOperator as F;
    Some(match value {
        F::Equals => Operator::Equals,
        F::NotEquals => Operator::NotEquals,
        F::Less => Operator::Less,
        F::LessEqual => Operator::LessEqual,
        F::Greater => Operator::Greater,
        F::GreaterEqual => Operator::GreaterEqual,
        F::In => Operator::In,
        F::Like => Operator::Like,
        F::IsNull => Operator::IsNull,
        F::IsNotNull => Operator::IsNotNull,
        _ => return None,
    })
}
pub fn quick_filter_options_policy(
    column: &str,
    value: ffi::CellDto,
) -> Vec<ffi::QuickFilterOptionDto> {
    let value = convert::value(value).unwrap_or(choscordb_driver_api::Value::Unavailable {
        database_type: String::new(),
        reason: "Invalid cell transport".into(),
    });
    quick_filter::options(column, &value)
        .into_iter()
        .map(|item| ffi::QuickFilterOptionDto {
            operation: match item.operator {
                Operator::Equals => ffi::QuickFilterOperator::Equals,
                Operator::NotEquals => ffi::QuickFilterOperator::NotEquals,
                Operator::Less => ffi::QuickFilterOperator::Less,
                Operator::LessEqual => ffi::QuickFilterOperator::LessEqual,
                Operator::Greater => ffi::QuickFilterOperator::Greater,
                Operator::GreaterEqual => ffi::QuickFilterOperator::GreaterEqual,
                Operator::In => ffi::QuickFilterOperator::In,
                Operator::Like => ffi::QuickFilterOperator::Like,
                Operator::IsNull => ffi::QuickFilterOperator::IsNull,
                Operator::IsNotNull => ffi::QuickFilterOperator::IsNotNull,
            },
            label: item.template,
            enabled: item.expression.is_some(),
            reason: item.reason,
        })
        .collect()
}
pub fn quick_filter_compose_policy(
    columns: Vec<String>,
    draft: &str,
    column: &str,
    value: ffi::CellDto,
    operation: ffi::QuickFilterOperator,
) -> ffi::QuickFilterCompositionDto {
    let result = (|| {
        let value = convert::value(value).map_err(|_| "Invalid cell transport".to_string())?;
        let operation = operator(operation).ok_or("Invalid Quick Filter operator")?;
        let predicate =
            quick_filter::predicate(column, &value, operation).map_err(|error| error.message)?;
        let columns = columns
            .into_iter()
            .map(|name| choscordb_driver_api::Column {
                name,
                database_type: String::new(),
                precision: None,
                scale: None,
                timezone: None,
                nullable: None,
            })
            .collect::<Vec<_>>();
        quick_filter::compose(&columns, draft, &predicate).map_err(|error| error.message)
    })();
    match result {
        Ok(composed) => ffi::QuickFilterCompositionDto {
            expression: composed.expression,
            validation_error: composed.validation_error,
            error: String::new(),
        },
        Err(error) => ffi::QuickFilterCompositionDto {
            error,
            ..Default::default()
        },
    }
}
