//! UTF-8-safe windows over large text and binary values for the value viewer.

/// Rows of one preview window: `boundaries` are row starts plus the end, as
/// offsets into the window; bytes past `end` belong to the next window.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct PreviewRows {
    pub end: usize,
    pub boundaries: Vec<u32>,
}

/// Splits a window into rows of at most `row_bytes`, never splitting a UTF-8
/// character in text. `more_follows` marks a window that is not the value's tail.
pub fn preview_rows(
    bytes: &[u8],
    binary: bool,
    more_follows: bool,
    row_bytes: usize,
) -> PreviewRows {
    let mut end = bytes.len();
    if !binary && more_follows && end > 0 {
        let mut lead = end - 1;
        while lead > 0 && continuation(bytes[lead]) {
            lead -= 1;
        }
        if sequence_size(bytes[lead]) > end - lead {
            end = lead;
        }
    }
    if end == 0 {
        return PreviewRows::default();
    }
    let row_bytes = row_bytes.max(1);
    let mut boundaries = vec![0];
    let mut begin = 0;
    while begin < end {
        let mut next = (begin + row_bytes).min(end);
        if !binary && next < end {
            // At most three continuation bytes belong to a valid UTF-8 character.
            let mut lead = next;
            for _ in 0..3 {
                if lead <= begin || !continuation(bytes[lead]) {
                    break;
                }
                lead -= 1;
            }
            if lead > begin && lead < next && sequence_size(bytes[lead]) > next - lead {
                next = lead;
            }
        }
        boundaries.push(next as u32);
        begin = next;
    }
    PreviewRows { end, boundaries }
}

fn continuation(byte: u8) -> bool {
    byte & 0xc0 == 0x80
}

fn sequence_size(byte: u8) -> usize {
    match byte {
        0xc2..=0xdf => 2,
        0xe0..=0xef => 3,
        0xf0..=0xf4 => 4,
        _ => 1,
    }
}

/// The first character boundary at or after `target` in a text window that starts at
/// `window_offset`; `target` itself when no complete character crosses it.
pub fn text_boundary_after(window: &[u8], window_offset: u64, target: u64) -> u64 {
    let Some(relative) = target
        .checked_sub(window_offset)
        .and_then(|relative| usize::try_from(relative).ok())
    else {
        return target;
    };
    for i in 0..relative.min(window.len()) {
        let end = i + sequence_size(window[i]);
        if end > relative && end <= window.len() {
            return if std::str::from_utf8(&window[i..end]).is_ok() {
                window_offset + end as u64
            } else {
                target
            };
        }
    }
    target
}

/// The start of the UTF-8 character containing byte `target` of `text`.
pub fn text_char_start(text: &[u8], target: usize) -> usize {
    let mut start = target.min(text.len());
    while start > 0 && text.get(start).is_some_and(|&byte| continuation(byte)) {
        start -= 1;
    }
    start
}
