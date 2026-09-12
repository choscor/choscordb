use choscordb_driver_api::{ErrorKind, Value};
use choscordb_postgres_values::{decode, decode_bounded};
#[test]
fn exact_numeric_protocol_vector() {
    // 12345678901234567890.001200, four decimal digits per network-order group.
    let bytes = [
        0, 7, 0, 4, 0, 0, 0, 6, 4, 210, 22, 46, 35, 52, 13, 128, 30, 210, 0, 12, 0, 0,
    ];
    assert_eq!(
        decode(1700, Some(&bytes)).unwrap(),
        Value::Decimal("12345678901234567890.001200".into())
    );
    assert_eq!(
        decode_bounded(1700, Some(&bytes), 10).unwrap_err().kind,
        ErrorKind::ResourceLimit
    );
}
#[test]
fn temporal_epoch_negative_and_infinity() {
    assert_eq!(
        decode(1082, Some(&(-1_i32).to_be_bytes())).unwrap(),
        Value::Date("1999-12-31".into())
    );
    assert_eq!(
        decode(1114, Some(&(-1_i64).to_be_bytes())).unwrap(),
        Value::Timestamp("1999-12-31 23:59:59.999999".into())
    );
    assert_eq!(
        decode(1184, Some(&0_i64.to_be_bytes())).unwrap(),
        Value::Timestamp("2000-01-01 00:00:00+00:00".into())
    );
    assert_eq!(
        decode(1083, Some(&86_400_000_000_i64.to_be_bytes())).unwrap(),
        Value::Time("24:00:00".into())
    );
    assert_eq!(
        decode(1114, Some(&i64::MAX.to_be_bytes())).unwrap(),
        Value::Timestamp("infinity".into())
    );
    assert_eq!(
        decode(1082, Some(&i32::MIN.to_be_bytes())).unwrap(),
        Value::Date("-infinity".into())
    );
}
#[test]
fn scalars_and_budgets() {
    assert_eq!(decode(16, Some(&[1])).unwrap(), Value::Bool(true));
    assert_eq!(
        decode(20, Some(&i64::MIN.to_be_bytes())).unwrap(),
        Value::Integer(i64::MIN)
    );
    assert_eq!(
        decode(21, Some(&(-32768_i16).to_be_bytes())).unwrap(),
        Value::Integer(-32768)
    );
    assert_eq!(
        decode(23, Some(&(-42_i32).to_be_bytes())).unwrap(),
        Value::Integer(-42)
    );
    assert_eq!(
        decode(700, Some(&1.5_f32.to_be_bytes())).unwrap(),
        Value::Real(1.5)
    );
    assert_eq!(
        decode(701, Some(&(-1.25_f64).to_be_bytes())).unwrap(),
        Value::Real(-1.25)
    );
    assert_eq!(
        decode(
            2950,
            Some(&[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15])
        )
        .unwrap(),
        Value::Uuid("00010203-0405-0607-0809-0a0b0c0d0e0f".into())
    );
    assert_eq!(
        decode(3802, Some(b"\x01{\"x\":1}")).unwrap(),
        Value::Json("{\"x\":1}".into())
    );
    assert_eq!(
        decode(17, Some(&[0, 255])).unwrap(),
        Value::Binary(vec![0, 255])
    );
    for oid in [19, 25, 1042, 1043] {
        assert_eq!(
            decode(oid, Some("é".as_bytes())).unwrap(),
            Value::Text("é".into())
        );
        assert_eq!(
            decode_bounded(oid, Some("é".as_bytes()), 1)
                .unwrap_err()
                .kind,
            ErrorKind::ResourceLimit
        );
    }
    assert_eq!(
        decode(9999, Some(&[])).unwrap_err().kind,
        ErrorKind::Unsupported
    );
    assert_eq!(decode(9999, None).unwrap(), Value::Null);
}
#[test]
fn malformed_values_are_rejected() {
    for (oid, b) in [
        (16, &[2][..]),
        (21, &[0][..]),
        (20, &[0; 9][..]),
        (25, &[255][..]),
        (3802, &[2, 123, 125][..]),
        (2950, &[0; 15][..]),
        (1700, &[0; 7][..]),
        (1700, &[0, 1, 0, 0, 0, 0, 0, 0][..]),
        (1700, &[0, 0, 0, 0, 0x20, 0, 0, 0][..]),
        (1700, &[0, 1, 0, 0, 0, 0, 0, 0, 0x27, 0x10][..]),
    ] {
        assert_eq!(
            decode(oid, Some(b)).unwrap_err().kind,
            ErrorKind::InvalidInput
        );
    }
    for v in [-1_i64, 86_400_000_001] {
        assert_eq!(
            decode(1083, Some(&v.to_be_bytes())).unwrap_err().kind,
            ErrorKind::InvalidInput
        );
    }
    assert_eq!(
        decode(1114, Some(&(-211_813_488_000_000_001_i64).to_be_bytes()))
            .unwrap_err()
            .kind,
        ErrorKind::InvalidInput
    );
}
#[test]
fn numeric_fraction_sign_and_specials() {
    assert_eq!(
        decode(1700, Some(&[0, 1, 255, 255, 0x40, 0, 0, 6, 0, 12])).unwrap(),
        Value::Decimal("-0.001200".into())
    );
    for (sign, text) in [(0xc0, "NaN"), (0xd0, "Infinity"), (0xf0, "-Infinity")] {
        assert_eq!(
            decode(1700, Some(&[0, 0, 0, 0, sign, 0, 0, 0])).unwrap(),
            Value::Decimal(text.into())
        );
    }
    assert_eq!(
        decode(1700, Some(&[0, 0, 0, 0, 0, 0, 0, 3])).unwrap(),
        Value::Decimal("0.000".into())
    );
}
#[test]
fn numeric_does_not_silently_discard_nonzero_digits() {
    // Fraction .1234 cannot be represented at scale two without changing value.
    assert_eq!(
        decode(1700, Some(&[0, 1, 255, 255, 0, 0, 0, 2, 4, 210]))
            .unwrap_err()
            .kind,
        ErrorKind::InvalidInput
    );
}
#[test]
fn temporal_full_server_boundaries() {
    assert_eq!(
        decode(1082, Some(&(-2_451_545_i32).to_be_bytes())).unwrap(),
        Value::Date("4714-11-24 BC".into())
    );
    assert_eq!(
        decode(1082, Some(&2_145_031_948_i32.to_be_bytes())).unwrap(),
        Value::Date("5874897-12-31".into())
    );
    assert_eq!(
        decode(1114, Some(&9_223_371_331_199_999_999_i64.to_be_bytes())).unwrap(),
        Value::Timestamp("294276-12-31 23:59:59.999999".into())
    );
}
#[test]
fn postgres_17_numeric_send_infinities() {
    // Actual numeric_send output: the special value's dscale is not zero.
    for (sign, expected) in [(0xd0, "Infinity"), (0xf0, "-Infinity")] {
        assert_eq!(
            decode(1700, Some(&[0, 0, 0, 0, sign, 0, 0, 32])).unwrap(),
            Value::Decimal(expected.into())
        );
    }
}
#[test]
fn era_boundary_has_no_year_zero() {
    assert_eq!(
        decode(1082, Some(&(-730_119_i32).to_be_bytes())).unwrap(),
        Value::Date("0001-01-01".into())
    );
    assert_eq!(
        decode(1082, Some(&(-730_120_i32).to_be_bytes())).unwrap(),
        Value::Date("0001-12-31 BC".into())
    );
}
#[test]
fn numeric_maximum_scale_and_integer_padding() {
    // A numeric with a negative declared typmod scale is rounded by the server;
    // its transmitted display scale is zero. 12000 remains an exact integer.
    assert_eq!(
        decode(1700, Some(&[0, 2, 0, 1, 0, 0, 0, 0, 0, 1, 7, 208])).unwrap(),
        Value::Decimal("12000".into())
    );
    // A single base-10000 group at maximum weight: 1 followed by 131068 zeros.
    let Value::Decimal(s) = decode(1700, Some(&[0, 1, 127, 255, 0, 0, 0, 0, 0, 1])).unwrap() else {
        panic!()
    };
    assert_eq!(s.len(), 131069);
    assert!(s.starts_with('1'));
    assert!(s[1..].bytes().all(|b| b == b'0'));
    // Full legal display scale of 16383 decimal places, including trailing zeros.
    let Value::Decimal(s) = decode(1700, Some(&[0, 0, 0, 0, 0, 0, 63, 255])).unwrap() else {
        panic!()
    };
    assert_eq!(s.len(), 16385);
    assert!(s.starts_with("0."));
    assert!(s[2..].bytes().all(|b| b == b'0'));
}
#[test]
fn xml_and_internal_char() {
    assert_eq!(
        decode(142, Some(b"<a>value</a>")).unwrap(),
        Value::Text("<a>value</a>".into())
    );
    assert_eq!(decode(18, Some(b"r")).unwrap(), Value::Text("r".into()));
    for b in [&[][..], &[1, 2][..], &[255][..]] {
        assert_eq!(
            decode(18, Some(b)).unwrap_err().kind,
            ErrorKind::InvalidInput
        );
    }
}
#[test]
fn timetz_preserves_offset_and_seconds() {
    // Independently obtained from PostgreSQL 17 timetz_send on the live fixture.
    let wire = [0, 0, 0, 10, 14, 237, 146, 64, 255, 255, 178, 123];
    assert_eq!(
        decode(1266, Some(&wire)).unwrap(),
        Value::Time("12:00:00.123456+05:30:45".into())
    );

    let mut b = 43_200_123_456_i64.to_be_bytes().to_vec();
    b.extend_from_slice(&(-19_845_i32).to_be_bytes());
    assert_eq!(
        decode(1266, Some(&b)).unwrap(),
        Value::Time("12:00:00.123456+05:30:45".into())
    );
    b[8..].copy_from_slice(&57_599_i32.to_be_bytes());
    assert_eq!(
        decode(1266, Some(&b)).unwrap(),
        Value::Time("12:00:00.123456-15:59:59".into())
    );
    for zone in [-57_600_i32, 57_600] {
        b[8..].copy_from_slice(&zone.to_be_bytes());
        assert_eq!(
            decode(1266, Some(&b)).unwrap_err().kind,
            ErrorKind::InvalidInput
        );
    }
    assert_eq!(
        decode(1266, Some(&[0; 11])).unwrap_err().kind,
        ErrorKind::InvalidInput
    );
}
