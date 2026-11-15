use crate::{BridgeEngine, ffi, submit, unpack};
use choscordb_core::{
    ObjectAction, ObjectScript, ObjectScriptText,
    prepare_object_action as prepare_core_object_action,
};

fn planned(
    request: &ffi::ObjectActionRequestDto,
) -> Result<choscordb_core::ObjectActionStatement, String> {
    let action = ObjectAction::from_code(
        &request.action,
        request.cascade,
        request.restart_identity,
        request.with_data,
    )
    .ok_or_else(|| format!("Unknown object action {}.", request.action))?;
    prepare_core_object_action(
        action,
        &request.driver,
        &request.kind,
        &request.object_id,
        &request.qualified_name,
        &request.new_name,
        &request.relation_subtype,
    )
    .map_err(str::to_owned)
}

pub fn prepare_object_action(
    request: ffi::ObjectActionRequestDto,
) -> ffi::ObjectActionStatementDto {
    match planned(&request) {
        Ok(statement) => ffi::ObjectActionStatementDto {
            valid: true,
            sql: statement.sql,
            statements: statement.statements,
            error: String::new(),
            new_object_id: statement.new_object_id,
            new_qualified_name: statement.new_qualified_name,
        },
        Err(error) => ffi::ObjectActionStatementDto {
            error,
            ..Default::default()
        },
    }
}

pub fn apply_object_action(
    engine: &mut BridgeEngine,
    connection: u64,
    request: &ffi::ObjectActionRequestDto,
    request_token: u64,
) -> ffi::Submit {
    let statements = planned(request).map(|statement| statement.statements);
    submit(engine, |e| {
        e.apply_ddl(unpack(connection), statements?, request_token)
            .map(|()| connection)
            .map_err(|error| error.to_string())
    })
}

pub fn truncate_traits_policy(driver: &str) -> ffi::TruncateTraitsDto {
    let traits = choscordb_core::truncate_traits(driver);
    ffi::TruncateTraitsDto {
        cascade: traits.cascade,
        restart_identity: traits.restart_identity,
    }
}

pub fn object_scripts() -> Vec<ffi::CodeLabelDto> {
    ObjectScript::ALL
        .into_iter()
        .map(|script| ffi::CodeLabelDto {
            code: script.code().into(),
            label: script.label(),
        })
        .collect()
}

pub fn object_script_unavailable_reason(
    script: &str,
    driver: &str,
    kind: &str,
    relation_subtype: &str,
) -> String {
    let Some(script) = ObjectScript::from_code(script) else {
        return format!("Unknown object script {script}.");
    };
    match choscordb_core::object_script_availability(script, driver, kind, relation_subtype) {
        Ok(()) => String::new(),
        Err(reason) => reason.into(),
    }
}

pub fn object_script_policy(
    script: &str,
    driver: &str,
    kind: &str,
    object_id: &str,
    qualified_name: &str,
    relation_subtype: &str,
) -> ffi::ObjectScriptDto {
    let Some(script_kind) = ObjectScript::from_code(script) else {
        return ffi::ObjectScriptDto {
            error: format!("Unknown object script {script}."),
            ..Default::default()
        };
    };
    match choscordb_core::object_script(
        script_kind,
        driver,
        kind,
        object_id,
        qualified_name,
        relation_subtype,
    ) {
        Ok(ObjectScriptText::Sql(sql)) => ffi::ObjectScriptDto {
            valid: true,
            sql,
            ..Default::default()
        },
        Ok(ObjectScriptText::Ddl) => ffi::ObjectScriptDto {
            valid: true,
            from_ddl: true,
            ..Default::default()
        },
        Err(error) => ffi::ObjectScriptDto {
            error: error.into(),
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
        schema_diagram: traits.schema_diagram,
        object_actions: traits.object_actions,
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
    }
}

pub fn row_status_policy(cause: ffi::RowStatusCauseDto, detail: &str) -> ffi::RowStatusDto {
    use choscordb_driver_api::{RetryAction, RowStatusCause as Cause, RowStatusKind};
    use ffi::RowStatusCauseDto as Dto;
    let cause = match cause {
        Dto::Connecting => Cause::Connecting,
        Dto::ChildrenLoading => Cause::ChildrenLoading,
        Dto::ChildrenFailed => Cause::ChildrenFailed,
        Dto::ConnectionFailed => Cause::ConnectionFailed,
        Dto::ConnectionUnavailable => Cause::ConnectionUnavailable,
        Dto::ConnectionNotReady => Cause::ConnectionNotReady,
        Dto::ConnectionDeselected => Cause::ConnectionDeselected,
        Dto::RevealFailed => Cause::RevealFailed,
        Dto::ObjectUnavailable => Cause::ObjectUnavailable,
        Dto::Connected => Cause::Connected,
        Dto::Hidden => Cause::Hidden,
        Dto::Disconnected => Cause::Disconnected,
        Dto::ChildrenCached => Cause::ChildrenCached,
        Dto::ChildrenCachedFailed => Cause::ChildrenCachedFailed,
        _ => {
            return ffi::RowStatusDto {
                kind: ffi::RowStatusKindDto::Failed,
                retry: ffi::RowRetryDto::None,
                message: format!("Unknown row status cause {}.", cause.repr),
            };
        }
    };
    let status = choscordb_driver_api::row_status(cause, detail);
    ffi::RowStatusDto {
        kind: match status.kind {
            RowStatusKind::None => ffi::RowStatusKindDto::None,
            RowStatusKind::Loading => ffi::RowStatusKindDto::Loading,
            RowStatusKind::Failed => ffi::RowStatusKindDto::Failed,
            RowStatusKind::Unavailable => ffi::RowStatusKindDto::Unavailable,
        },
        retry: match status.retry {
            RetryAction::None => ffi::RowRetryDto::None,
            RetryAction::Refresh => ffi::RowRetryDto::Refresh,
        },
        message: status.message,
    }
}

pub fn recovery_hint_text(hint: ffi::RecoveryHintDto) -> String {
    use choscordb_driver_api::RecoveryHint as Hint;
    use ffi::RecoveryHintDto as Dto;
    let hint = match hint {
        Dto::Retry => Hint::Retry,
        Dto::RetryQuery => Hint::RetryQuery,
        Dto::RefineOrRetry => Hint::RefineOrRetry,
        Dto::Refresh => Hint::Refresh,
        Dto::ActivatePin => Hint::ActivatePin,
        Dto::RetryFromNavigator => Hint::RetryFromNavigator,
        Dto::RetryFromHistory => Hint::RetryFromHistory,
        Dto::OpenPreferences => Hint::OpenPreferences,
        Dto::RetryOrResetAppearance => Hint::RetryOrResetAppearance,
        Dto::RetryAppearanceBeforeSave => Hint::RetryAppearanceBeforeSave,
        Dto::RestoreConnection => Hint::RestoreConnection,
        Dto::RefreshNavigator => Hint::RefreshNavigator,
        Dto::CheckConnection => Hint::CheckConnection,
        Dto::ReconnectAndRefresh => Hint::ReconnectAndRefresh,
        Dto::RetryRecovery => Hint::RetryRecovery,
        Dto::RetryConnection => Hint::RetryConnection,
        Dto::InspectSshServer => Hint::InspectSshServer,
        Dto::UseApprovedKey => Hint::UseApprovedKey,
        Dto::RetryApproval => Hint::RetryApproval,
        Dto::ReconnectManually => Hint::ReconnectManually,
        Dto::ReconnectAndRefreshNavigator => Hint::ReconnectAndRefreshNavigator,
        _ => return format!("Unknown recovery hint {}.", hint.repr),
    };
    choscordb_driver_api::recovery_hint(hint).to_owned()
}

pub fn panel_notice_milliseconds() -> u32 {
    choscordb_driver_api::PANEL_NOTICE_MILLISECONDS
}

pub fn filter_delay_milliseconds() -> u32 {
    choscordb_driver_api::FILTER_DELAY_MILLISECONDS
}

pub fn layout_save_delay_milliseconds() -> u32 {
    choscordb_driver_api::LAYOUT_SAVE_DELAY_MILLISECONDS
}

pub fn tab_count_sample_milliseconds() -> u32 {
    choscordb_driver_api::TAB_COUNT_SAMPLE_MILLISECONDS
}

pub fn pin_reveal_retry_milliseconds() -> u32 {
    choscordb_driver_api::PIN_REVEAL_RETRY_MILLISECONDS
}

pub fn recovery_save_delay_milliseconds() -> u32 {
    choscordb_driver_api::RECOVERY_SAVE_DELAY_MILLISECONDS
}

pub fn completion_delay_milliseconds() -> u32 {
    choscordb_driver_api::COMPLETION_DELAY_MILLISECONDS
}

pub fn json_menu_retry_milliseconds() -> u32 {
    choscordb_driver_api::JSON_MENU_RETRY_MILLISECONDS
}

pub fn trust_validity_poll_milliseconds() -> u32 {
    choscordb_driver_api::TRUST_VALIDITY_POLL_MILLISECONDS
}

pub fn signal_coalesce_milliseconds() -> u32 {
    choscordb_driver_api::SIGNAL_COALESCE_MILLISECONDS
}

pub fn startup_poll_milliseconds() -> u32 {
    choscordb_driver_api::STARTUP_POLL_MILLISECONDS
}

pub fn history_page_rows() -> u32 {
    choscordb_driver_api::HISTORY_PAGE_ROWS
}

pub fn recent_history_rows() -> u32 {
    choscordb_driver_api::RECENT_HISTORY_ROWS
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

pub fn relation_subtype_history_policy(relation_subtype: &str) -> bool {
    choscordb_driver_api::relation_subtype_has_history(relation_subtype)
}

pub fn object_action_unavailable_reason(
    action: &str,
    driver: &str,
    kind: &str,
    relation_subtype: &str,
) -> String {
    let Some(action) = ObjectAction::from_code(action, false, false, false) else {
        return format!("Unknown object action {action}.");
    };
    match choscordb_core::object_action_availability(action, driver, kind, relation_subtype) {
        Ok(()) => String::new(),
        Err(reason) => reason.into(),
    }
}
