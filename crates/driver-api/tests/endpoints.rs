use choscordb_driver_api::{SshAuthentication, SshTunnel};

#[test]
fn ssh_hosts_reject_url_userinfo_and_forwarding_syntax() {
    for host in [
        "ssh://bastion",
        "other@bastion",
        "bastion:2222",
        "db/name",
        "[::1",
        "[[::1]]",
    ] {
        let settings = SshTunnel {
            options: Default::default(),
            host: host.into(),
            port: 22,
            user: "operator".into(),
            authentication: SshAuthentication::Agent,
            identity_source: Default::default(),
            identity_file: None,
        };
        assert!(settings.validate().is_err(), "accepted {host}");
    }
}

#[test]
fn tcp_host_preserves_dns_ipv4_and_normalizes_only_ipv6_brackets() {
    use choscordb_driver_api::tcp_host;
    for (input, expected) in [
        ("localhost", "localhost"),
        ("127.0.0.1", "127.0.0.1"),
        ("db.internal", "db.internal"),
        ("::1", "::1"),
        ("[2001:db8::12]", "2001:db8::12"),
        ("[fe80::1%en0]", "fe80::1%en0"),
    ] {
        assert_eq!(tcp_host(input).unwrap(), expected);
    }
    for input in [
        "[localhost]",
        "[[::1]]",
        "bad host",
        "::1%",
        "-host",
        "",
        "host\n",
    ] {
        assert!(tcp_host(input).is_err(), "accepted {input}");
    }
}
