use crate::ffi;
use choscordb_core::{ObjectAction, prepare_object_action as prepare_core_object_action};

pub fn prepare_object_action(
    driver: &str,
    kind: &str,
    object_id: &str,
    qualified_name: &str,
    new_name: &str,
    relation_subtype: &str,
    rename: bool,
) -> ffi::ObjectActionStatementDto {
    let action = if rename {
        ObjectAction::Rename
    } else {
        ObjectAction::Drop
    };
    match prepare_core_object_action(
        action,
        driver,
        kind,
        object_id,
        qualified_name,
        new_name,
        relation_subtype,
    ) {
        Ok(statement) => ffi::ObjectActionStatementDto {
            valid: true,
            sql: statement.sql,
            error: String::new(),
            new_object_id: statement.new_object_id,
            new_qualified_name: statement.new_qualified_name,
        },
        Err(error) => ffi::ObjectActionStatementDto {
            error: error.to_owned(),
            ..Default::default()
        },
    }
}

pub fn object_display_identity_policy(
    object_id: &str,
    qualified_name: &str,
) -> crate::ffi::ObjectDisplayDto {
    let (schema, name) = choscordb_core::object_display_identity(object_id, qualified_name);
    crate::ffi::ObjectDisplayDto {
        has_schema: schema.is_some(),
        schema: schema.unwrap_or_default(),
        name,
    }
}

pub fn object_kind_traits_policy(kind: &str) -> ffi::ObjectKindTraitsDto {
    let traits = choscordb_driver_api::object_kind_traits(kind);
    ffi::ObjectKindTraitsDto {
        opens_object_tab: traits.opens_object_tab,
        ddl: traits.ddl,
        relation: traits.relation,
        container: traits.container,
        pinnable: traits.pinnable,
        navigation_anchor: traits.navigation_anchor,
        search_descends: traits.search_descends,
        completion_candidate: traits.completion_candidate,
        has_detail_pane: traits.detail_pane.is_some(),
        detail_pane: traits.detail_pane.unwrap_or_default(),
        connection: traits.connection,
        column: traits.column,
        diagram: traits.diagram,
        has_initial_pane: traits.initial_pane.is_some(),
        initial_pane: traits.initial_pane.unwrap_or_default(),
        repeats_across_parents: traits.repeats_across_parents,
    }
}

pub fn driver_workflow_policy(driver: &str) -> ffi::DriverWorkflowDto {
    let workflow = choscordb_driver_api::driver_workflow(driver);
    ffi::DriverWorkflowDto {
        inspect_after_result: workflow.inspect_after_result,
        sql_mode_before_execution: workflow.sql_mode_before_execution,
    }
}

pub fn transaction_guard_policy(transaction_active: bool) -> ffi::TransactionGuardDto {
    let guard = choscordb_driver_api::transaction_guard(transaction_active);
    ffi::TransactionGuardDto {
        apply_edits: guard.apply_edits.unwrap_or_default().into(),
        enable_auto_commit: guard.enable_auto_commit.unwrap_or_default().into(),
    }
}

pub fn navigator_search_budget() -> ffi::NavigatorSearchBudgetDto {
    let budget = choscordb_driver_api::NAVIGATOR_SEARCH_BUDGET;
    ffi::NavigatorSearchBudgetDto {
        quick_visits: budget.quick_visits,
        quick_requests: budget.quick_requests,
        quick_results: budget.quick_results,
        filter_visits: budget.filter_visits,
        filter_requests: budget.filter_requests,
    }
}

pub fn sidebar_child_visible_policy(parent_kind: &str, kind: &str) -> bool {
    choscordb_driver_api::sidebar_child_visible(parent_kind, kind)
}

pub fn object_action_unavailable_reason(
    rename: bool,
    driver: &str,
    kind: &str,
    relation_subtype: &str,
) -> String {
    let action = if rename {
        ObjectAction::Rename
    } else {
        ObjectAction::Drop
    };
    choscordb_core::object_action_availability(action, driver, kind, relation_subtype)
        .err()
        .unwrap_or_default()
        .into()
}
