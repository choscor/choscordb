use choscordb_result_store::{preview_rows, text_boundary_after, text_char_start};

const EURO: &[u8] = "€".as_bytes();

#[test]
fn text_rows_never_split_characters_and_hold_back_a_partial_tail() {
    let mut bytes = vec![b'a'; 255];
    bytes.extend_from_slice(EURO);
    bytes.push(b'z');
    bytes.extend_from_slice(&EURO[..2]);
    let rows = preview_rows(&bytes, false, true, 256);
    assert_eq!(rows.end, 259);
    assert_eq!(rows.boundaries, vec![0, 255, 259]);
    // The value's tail keeps an incomplete character so it stays visible as invalid.
    let tail = preview_rows(&bytes, false, false, 256);
    assert_eq!(tail.end, 261);
    assert_eq!(tail.boundaries, vec![0, 255, 261]);
}

#[test]
fn binary_and_invalid_text_split_at_the_row_width() {
    let binary = preview_rows(&[0; 40], true, true, 16);
    assert_eq!(binary.end, 40);
    assert_eq!(binary.boundaries, vec![0, 16, 32, 40]);
    let invalid = preview_rows(&[0x80; 600], false, true, 256);
    assert_eq!(invalid.boundaries, vec![0, 256, 512, 600]);
    assert_eq!(preview_rows(&[], false, true, 256), Default::default());
}

#[test]
fn backward_paging_lands_on_character_boundaries() {
    // The window starts at 100; the euro occupies 102..105.
    let window = [b'a', b'b', EURO[0], EURO[1], EURO[2], b'c', b'd'];
    assert_eq!(text_boundary_after(&window, 100, 103), 105);
    assert_eq!(text_boundary_after(&window, 100, 102), 102);
    assert_eq!(text_boundary_after(&window, 100, 106), 106);
    let text = [b'x', EURO[0], EURO[1], EURO[2]];
    assert_eq!(text_char_start(&text, 3), 1);
    assert_eq!(text_char_start(&text, 1), 1);
    assert_eq!(text_char_start(&text, 0), 0);
}
