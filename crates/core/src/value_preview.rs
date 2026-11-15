use serde_json::Value as Json;

pub const VALUE_PREVIEW_MAX_BYTES: usize = 16 * 1024 * 1024;
pub const MAX_JSON_PREVIEW_NODES: usize = 10_000;
const MAX_JSON_PREVIEW_TEXT_CHARS: usize = 200;
const MAX_GEOMETRY_DEPTH: usize = 32;
const MAX_GEOMETRY_POINTS: u64 = 1_000_000;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ValuePreviewKind {
    Json,
    Image,
    Pdf,
    Geometry,
    None,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct JsonPreviewNode {
    pub parent: Option<usize>,
    pub key: String,
    pub value: String,
    pub kind: &'static str,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ValuePreview {
    pub kind: ValuePreviewKind,
    pub summary: String,
    pub pretty: String,
    pub minified: String,
    pub nodes: Vec<JsonPreviewNode>,
    pub nodes_truncated: bool,
    pub image_format: String,
}

impl ValuePreview {
    fn new(kind: ValuePreviewKind, summary: impl Into<String>) -> Self {
        Self {
            kind,
            summary: summary.into(),
            pretty: String::new(),
            minified: String::new(),
            nodes: Vec::new(),
            nodes_truncated: false,
            image_format: String::new(),
        }
    }
}

pub fn value_preview_too_large(bytes: u64) -> Option<String> {
    (bytes > VALUE_PREVIEW_MAX_BYTES as u64).then(|| {
        format!(
            "A preview is limited to {} MiB. Use the byte window for this value.",
            VALUE_PREVIEW_MAX_BYTES >> 20
        )
    })
}

pub fn value_preview(bytes: &[u8], binary: bool, database_type: &str) -> ValuePreview {
    if let Some(reason) = value_preview_too_large(bytes.len() as u64) {
        return ValuePreview::new(ValuePreviewKind::None, reason);
    }
    let database_type = database_type.to_ascii_lowercase();
    let geometry = [
        "geometry",
        "geography",
        "point",
        "linestring",
        "polygon",
        "multi",
    ]
    .iter()
    .any(|name| database_type.contains(name));
    if geometry {
        return match geometry_text(bytes, binary) {
            Ok(wkt) => {
                let mut preview = ValuePreview::new(ValuePreviewKind::Geometry, "Geometry as WKT");
                preview.pretty = wkt;
                preview
            }
            Err(reason) => ValuePreview::new(ValuePreviewKind::None, reason),
        };
    }
    if binary {
        if let Some(format) = image_format(bytes) {
            let mut preview = ValuePreview::new(ValuePreviewKind::Image, format!("{format} image"));
            preview.image_format = format.into();
            return preview;
        }
        if bytes.starts_with(b"%PDF-") {
            return ValuePreview::new(ValuePreviewKind::Pdf, pdf_summary(bytes));
        }
        return ValuePreview::new(
            ValuePreviewKind::None,
            "This binary value is not an image, a PDF document or a geometry.",
        );
    }
    let Ok(text) = std::str::from_utf8(bytes) else {
        return ValuePreview::new(ValuePreviewKind::None, "The value is not UTF-8 text.");
    };
    match serde_json::from_str::<Json>(text) {
        Ok(json) => json_preview(&json),
        Err(error) => ValuePreview::new(
            ValuePreviewKind::None,
            format!("The value is not JSON: {error}"),
        ),
    }
}

fn json_preview(json: &Json) -> ValuePreview {
    let mut preview = ValuePreview::new(ValuePreviewKind::Json, "JSON");
    preview.pretty = serde_json::to_string_pretty(json).unwrap_or_default();
    preview.minified = json.to_string();
    let mut pending: Vec<(Option<usize>, String, &Json)> = vec![(None, "$".into(), json)];
    while let Some((parent, key, value)) = pending.pop() {
        if preview.nodes.len() == MAX_JSON_PREVIEW_NODES {
            preview.nodes_truncated = true;
            preview.summary =
                format!("JSON. The tree shows the first {MAX_JSON_PREVIEW_NODES} items.");
            break;
        }
        let index = preview.nodes.len();
        let (kind, text) = match value {
            Json::Object(map) => ("object", format!("{{{} keys}}", map.len())),
            Json::Array(items) => ("array", format!("[{} items]", items.len())),
            Json::String(text) => ("string", shortened(&Json::String(text.clone()).to_string())),
            Json::Number(number) => ("number", number.to_string()),
            Json::Bool(flag) => ("boolean", flag.to_string()),
            Json::Null => ("null", "null".into()),
        };
        preview.nodes.push(JsonPreviewNode {
            parent,
            key,
            value: text,
            kind,
        });
        match value {
            Json::Object(map) => {
                for (child_key, child) in map.iter().rev() {
                    pending.push((Some(index), child_key.clone(), child));
                }
            }
            Json::Array(items) => {
                for (position, child) in items.iter().enumerate().rev() {
                    pending.push((Some(index), format!("[{position}]"), child));
                }
            }
            _ => {}
        }
    }
    preview
}

fn shortened(text: &str) -> String {
    if text.chars().count() <= MAX_JSON_PREVIEW_TEXT_CHARS {
        return text.into();
    }
    let mut short: String = text.chars().take(MAX_JSON_PREVIEW_TEXT_CHARS).collect();
    short.push('…');
    short
}

fn image_format(bytes: &[u8]) -> Option<&'static str> {
    if bytes.starts_with(b"\x89PNG\r\n\x1a\n") {
        Some("PNG")
    } else if bytes.starts_with(&[0xff, 0xd8, 0xff]) {
        Some("JPEG")
    } else if bytes.starts_with(b"GIF87a") || bytes.starts_with(b"GIF89a") {
        Some("GIF")
    } else if bytes.len() >= 12 && &bytes[..4] == b"RIFF" && &bytes[8..12] == b"WEBP" {
        Some("WEBP")
    } else if bytes.starts_with(b"BM") && bytes.len() >= 26 {
        Some("BMP")
    } else {
        None
    }
}

fn pdf_summary(bytes: &[u8]) -> String {
    let version: String = bytes[5..]
        .iter()
        .take_while(|byte| byte.is_ascii_digit() || **byte == b'.')
        .take(8)
        .map(|byte| *byte as char)
        .collect();
    let pages = bytes
        .windows(12)
        .filter(|window| window.starts_with(b"/Type") && is_page_marker(window))
        .count();
    format!(
        "PDF document, version {}, about {pages} page(s), {} bytes. ChoscorDB does not render PDF pages.",
        if version.is_empty() {
            "unknown"
        } else {
            &version
        },
        bytes.len()
    )
}

fn is_page_marker(window: &[u8]) -> bool {
    let rest = &window[5..];
    let start = rest
        .iter()
        .position(|byte| *byte != b' ')
        .unwrap_or(rest.len());
    let rest = &rest[start..];
    rest.starts_with(b"/Page") && rest.get(5).is_none_or(|byte| !byte.is_ascii_alphabetic())
}

fn geometry_text(bytes: &[u8], binary: bool) -> Result<String, String> {
    let raw;
    let wkb: &[u8] = if binary {
        bytes
    } else {
        let text = std::str::from_utf8(bytes)
            .map_err(|_| "The geometry value is not UTF-8 text.".to_string())?
            .trim();
        if text.is_empty() || !text.len().is_multiple_of(2) {
            return Err("The geometry value is not hexadecimal WKB.".into());
        }
        raw = (0..text.len())
            .step_by(2)
            .map(|index| u8::from_str_radix(&text[index..index + 2], 16))
            .collect::<Result<Vec<u8>, _>>()
            .map_err(|_| "The geometry value is not hexadecimal WKB.".to_string())?;
        &raw
    };
    let parse = |wkb: &[u8]| {
        let mut reader = Wkb {
            bytes: wkb,
            position: 0,
            points: 0,
        };
        let parsed = reader.geometry(0)?;
        if reader.position != wkb.len() {
            return Err("The geometry value has extra bytes after the WKB.".to_string());
        }
        Ok(parsed)
    };
    let (srid, text) = match parse(wkb) {
        Ok(parsed) => parsed,
        Err(error) if binary && wkb.len() > 4 => {
            let prefix = u32::from_le_bytes([wkb[0], wkb[1], wkb[2], wkb[3]]);
            let (embedded, text) = parse(&wkb[4..]).map_err(|_| error)?;
            (embedded.or(Some(prefix)), text)
        }
        Err(error) => return Err(error),
    };
    Ok(match srid.filter(|srid| *srid != 0) {
        Some(srid) => format!("SRID={srid};{text}"),
        None => text,
    })
}

struct Wkb<'a> {
    bytes: &'a [u8],
    position: usize,
    points: u64,
}

impl Wkb<'_> {
    fn take<const N: usize>(&mut self) -> Result<[u8; N], String> {
        let end = self.position + N;
        let slice = self
            .bytes
            .get(self.position..end)
            .ok_or_else(|| "The geometry WKB ends too early.".to_string())?;
        self.position = end;
        Ok(slice.try_into().unwrap_or([0; N]))
    }
    fn u32(&mut self, little: bool) -> Result<u32, String> {
        let bytes = self.take::<4>()?;
        Ok(if little {
            u32::from_le_bytes(bytes)
        } else {
            u32::from_be_bytes(bytes)
        })
    }
    fn f64(&mut self, little: bool) -> Result<f64, String> {
        let bytes = self.take::<8>()?;
        Ok(if little {
            f64::from_le_bytes(bytes)
        } else {
            f64::from_be_bytes(bytes)
        })
    }
    fn count(&mut self, little: bool) -> Result<u32, String> {
        let count = self.u32(little)?;
        if count as usize > self.bytes.len().saturating_sub(self.position) {
            return Err("The geometry WKB has an invalid count.".into());
        }
        Ok(count)
    }
    fn point(&mut self, little: bool, dimensions: usize) -> Result<String, String> {
        self.points += 1;
        if self.points > MAX_GEOMETRY_POINTS {
            return Err("The geometry has more than 1,000,000 points.".into());
        }
        let mut values = Vec::with_capacity(dimensions);
        for _ in 0..dimensions {
            values.push(self.f64(little)?.to_string());
        }
        Ok(values.join(" "))
    }
    fn points(&mut self, little: bool, dimensions: usize) -> Result<String, String> {
        let count = self.count(little)?;
        let points = (0..count)
            .map(|_| self.point(little, dimensions))
            .collect::<Result<Vec<_>, _>>()?;
        Ok(format!("({})", points.join(", ")))
    }
    fn geometry(&mut self, depth: usize) -> Result<(Option<u32>, String), String> {
        if depth > MAX_GEOMETRY_DEPTH {
            return Err("The geometry is nested too deeply.".into());
        }
        let order = self.take::<1>()?[0];
        let little = match order {
            1 => true,
            0 => false,
            _ => return Err("The geometry WKB has an invalid byte order.".into()),
        };
        let code = self.u32(little)?;
        let srid = if code & 0x2000_0000 != 0 {
            Some(self.u32(little)?)
        } else {
            None
        };
        let has_z = code & 0x8000_0000 != 0 || (code & 0xffff) / 1000 % 2 == 1;
        let has_m = code & 0x4000_0000 != 0 || (code & 0xffff) / 1000 >= 2;
        let base = (code & 0xffff) % 1000;
        let dimensions = 2 + usize::from(has_z) + usize::from(has_m);
        let suffix = match (has_z, has_m) {
            (true, true) => " ZM",
            (true, false) => " Z",
            (false, true) => " M",
            (false, false) => "",
        };
        let name = match base {
            1 => "POINT",
            2 => "LINESTRING",
            3 => "POLYGON",
            4 => "MULTIPOINT",
            5 => "MULTILINESTRING",
            6 => "MULTIPOLYGON",
            7 => "GEOMETRYCOLLECTION",
            _ => return Err(format!("The geometry type {base} is not supported.")),
        };
        let body = match base {
            1 => {
                let point = self.point(little, dimensions)?;
                if point.split(' ').all(|value| value == "NaN") {
                    "EMPTY".into()
                } else {
                    format!("({point})")
                }
            }
            2 => self.points(little, dimensions)?,
            3 => {
                let rings = self.count(little)?;
                let rings = (0..rings)
                    .map(|_| self.points(little, dimensions))
                    .collect::<Result<Vec<_>, _>>()?;
                format!("({})", rings.join(", "))
            }
            _ => {
                let count = self.count(little)?;
                let mut parts = Vec::with_capacity(count as usize);
                for _ in 0..count {
                    let (_, part) = self.geometry(depth + 1)?;
                    parts.push(if base == 7 {
                        part
                    } else {
                        part.split_once(' ')
                            .map(|(_, rest)| rest.trim_start_matches(['Z', 'M', ' ']))
                            .unwrap_or_default()
                            .to_string()
                    });
                }
                format!("({})", parts.join(", "))
            }
        };
        let text = if body == "()" || body == "EMPTY" {
            format!("{name}{suffix} EMPTY")
        } else {
            format!("{name}{suffix} {body}")
        };
        Ok((srid, text))
    }
}
