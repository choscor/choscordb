use choscordb_driver_api::TlsMode;

#[test]
fn tls_policies_round_trip_and_keep_verified_default() {
    for mode in ["Disable", "Prefer", "Require", "VerifyCa", "VerifyFull"] {
        let encoded = format!("\"{mode}\"");
        let value: TlsMode = serde_json::from_str(&encoded).expect("supported TLS policy");
        assert_eq!(serde_json::to_string(&value).unwrap(), encoded);
    }
    assert_eq!(TlsMode::default(), TlsMode::VerifyFull);
}
