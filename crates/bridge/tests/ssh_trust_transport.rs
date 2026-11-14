use choscordb_bridge::{ssh_known_hosts_path_error, ssh_known_hosts_path_rule};

#[test]
fn known_hosts_path_error_comes_from_the_engine() {
    let valid = if cfg!(windows) {
        "C:/Users/user/.ssh/known_hosts"
    } else {
        "/home/user/.ssh/known_hosts"
    };
    assert_eq!(ssh_known_hosts_path_error(valid), "");
    assert_eq!(
        ssh_known_hosts_path_error("~/.ssh/known_hosts"),
        ssh_known_hosts_path_rule()
    );
    assert_eq!(
        ssh_known_hosts_path_error("relative/known_hosts"),
        ssh_known_hosts_path_rule()
    );
}

#[test]
fn known_hosts_path_rule_states_the_whole_rule() {
    assert_eq!(
        ssh_known_hosts_path_rule(),
        "Use an absolute, normalized file path with forward slashes and no `.`, `..`, `~`, `$`, `%`, quote or control characters."
    );
}
