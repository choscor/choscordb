#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct DriverWorkflow {
    pub inspect_after_result: bool,
    pub sql_mode_before_execution: bool,
}

pub fn driver_workflow(driver: &str) -> DriverWorkflow {
    let mysql = driver.eq_ignore_ascii_case("mysql") || driver.eq_ignore_ascii_case("mariadb");
    DriverWorkflow {
        inspect_after_result: mysql,
        sql_mode_before_execution: mysql,
    }
}
