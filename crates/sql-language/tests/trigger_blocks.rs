use choscordb_sql_language::{statement_ranges, statement_ranges_mysql};

fn texts(sql: &str, ranges: Vec<std::ops::Range<usize>>) -> Vec<&str> {
    ranges.into_iter().map(|range| &sql[range]).collect()
}

#[test]
fn sqlite_trigger_bodies_stay_in_one_statement() {
    let trigger = "CREATE TEMP TRIGGER t AFTER INSERT ON a BEGIN UPDATE b SET n = CASE WHEN 1 THEN 2 END; DELETE FROM c; END;";
    let sql = format!("{trigger} SELECT 1;");
    assert_eq!(texts(&sql, statement_ranges(&sql)), [trigger, "SELECT 1;"]);
}

#[test]
fn mysql_trigger_bodies_keep_if_case_and_loop_blocks() {
    let trigger = "CREATE DEFINER=`root`@`%` TRIGGER t BEFORE UPDATE ON a FOR EACH ROW BEGIN IF NEW.x < 0 THEN SET NEW.x = 0; END IF; CASE NEW.y WHEN 1 THEN SET NEW.z = 1; END CASE; l: LOOP LEAVE l; END LOOP l; BEGIN SET @a = 1; END; END;";
    let sql = format!("{trigger}\nSELECT 2;");
    assert_eq!(
        texts(&sql, statement_ranges_mysql(&sql)),
        [trigger, "SELECT 2;"]
    );
    let simple = "CREATE TRIGGER t BEFORE INSERT ON a FOR EACH ROW SET NEW.x = 1; SELECT 3;";
    assert_eq!(statement_ranges_mysql(simple).len(), 2);
}

#[test]
fn begin_and_end_outside_trigger_bodies_still_split() {
    let sql = "BEGIN; SELECT 1; END; CREATE OR REPLACE FUNCTION f() RETURNS trigger AS $$ BEGIN RETURN NEW; END; $$ LANGUAGE plpgsql; CREATE TABLE t(trigger int); BEGIN; COMMIT;";
    assert_eq!(statement_ranges(sql).len(), 7);
}
