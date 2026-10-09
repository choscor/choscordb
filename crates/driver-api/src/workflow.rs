//! How the desktop sequences requests for each driver.

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct DriverWorkflow {
    /// Editability and cell metadata are inspected only after the whole result has
    /// arrived, because the session cannot run catalog queries while streaming.
    pub inspect_after_result: bool,
    /// Statement splitting depends on the session's SQL mode, which must be
    /// refreshed before choosing what to execute.
    pub sql_mode_before_execution: bool,
}

pub fn driver_workflow(driver: &str) -> DriverWorkflow {
    let mysql = driver.eq_ignore_ascii_case("mysql");
    DriverWorkflow {
        inspect_after_result: mysql,
        sql_mode_before_execution: mysql,
    }
}
