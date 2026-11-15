use crate::{ExportSink, format::plain};
use async_trait::async_trait;
use choscordb_driver_api::*;
use std::io::Write;

pub const XLSX_MAX_ROWS: u64 = 1_048_576;
pub const XLSX_MAX_COLUMNS: usize = 16_384;
pub const XLSX_MAX_CELL_CHARS: u64 = 32_767;
const MAX_ZIP_BYTES: u64 = u32::MAX as u64;
const MAX_EXACT_INTEGER: i64 = 1 << 53;
const MAX_EXACT_DIGITS: usize = 15;
const FLUSH_BYTES: usize = 64 * 1024;
pub(crate) const SHEET_START: &str = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><sheetData>";
pub(crate) const SHEET_END: &str = "</sheetData></worksheet>";
pub(crate) const ROW_START: &str = "<row>";
pub(crate) const ROW_END: &str = "</row>";
pub(crate) const TEXT_START: &str = "<c t=\"inlineStr\"><is><t xml:space=\"preserve\">";
pub(crate) const TEXT_END: &str = "</t></is></c>";
const PARTS: [(&str, &str); 4] = [
    (
        "[Content_Types].xml",
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"><Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/><Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/><Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/></Types>",
    ),
    (
        "_rels/.rels",
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/></Relationships>",
    ),
    (
        "xl/workbook.xml",
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><sheets><sheet name=\"Sheet1\" sheetId=\"1\" r:id=\"rId1\"/></sheets></workbook>",
    ),
    (
        "xl/_rels/workbook.xml.rels",
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/></Relationships>",
    ),
];
const SHEET_PART: &str = "xl/worksheets/sheet1.xml";

fn limit(message: &str) -> DriverError {
    DriverError::new(ErrorKind::ResourceLimit, message)
}

pub(crate) fn cell_too_long() -> DriverError {
    limit("An Excel cell holds at most 32,767 characters.")
}

pub(crate) fn too_many_rows() -> DriverError {
    limit("An Excel sheet holds at most 1,048,576 rows, including the header row.")
}

pub(crate) fn too_many_columns() -> DriverError {
    limit("An Excel sheet holds at most 16,384 columns.")
}

fn too_large() -> DriverError {
    limit("An Excel export is limited to 4 GiB.")
}

fn escape_marker_follows(rest: &str) -> bool {
    let bytes = rest.as_bytes();
    bytes.len() >= 6
        && bytes[0] == b'x'
        && bytes[1..5].iter().all(u8::is_ascii_hexdigit)
        && bytes[5] == b'_'
}

pub(crate) fn escape(text: &str) -> String {
    escape_prefix(text, text.len())
}

pub(crate) fn escape_prefix(text: &str, end: usize) -> String {
    let mut out = String::with_capacity(end);
    for (index, c) in text[..end].char_indices() {
        match c {
            '&' => out.push_str("&amp;"),
            '<' => out.push_str("&lt;"),
            '>' => out.push_str("&gt;"),
            '_' if escape_marker_follows(&text[index + 1..]) => out.push_str("_x005F_"),
            '\t' | '\n' => out.push(c),
            '\r' => out.push_str("&#13;"),
            c if (c as u32) < 0x20 || c == '\u{FFFE}' || c == '\u{FFFF}' => {
                out.push_str(&format!("_x{:04X}_", c as u32));
            }
            c => out.push(c),
        }
    }
    out
}

pub(crate) fn unfinished_marker(text: &str) -> usize {
    let tail_start = text.len().saturating_sub(6);
    text[tail_start..]
        .find('_')
        .map_or(text.len(), |index| tail_start + index)
}

fn text_cell(text: &str) -> Result<String> {
    if text.encode_utf16().count() as u64 > XLSX_MAX_CELL_CHARS {
        return Err(cell_too_long());
    }
    Ok(format!("{TEXT_START}{}{TEXT_END}", escape(text)))
}

fn exact_number(decimal: &str) -> bool {
    let mantissa = decimal.split(['e', 'E']).next().unwrap_or_default();
    let digits: String = mantissa.chars().filter(char::is_ascii_digit).collect();
    let significant = digits.trim_start_matches('0').trim_end_matches('0');
    crate::valid_decimal(decimal)
        && significant.len() <= MAX_EXACT_DIGITS
        && decimal.parse::<f64>().is_ok_and(f64::is_finite)
}

pub(crate) fn header_cell(name: &str) -> Result<String> {
    text_cell(name)
}

pub(crate) fn cell(value: &Value) -> Result<String> {
    Ok(match value {
        Value::Null => "<c/>".into(),
        Value::Bool(v) => format!("<c t=\"b\"><v>{}</v></c>", u8::from(*v)),
        Value::Integer(v) if (-MAX_EXACT_INTEGER..=MAX_EXACT_INTEGER).contains(v) => {
            format!("<c><v>{v}</v></c>")
        }
        Value::Real(v) if v.is_finite() => format!("<c><v>{v}</v></c>"),
        Value::Decimal(v) if exact_number(v) => format!("<c><v>{v}</v></c>"),
        _ => text_cell(&plain(value)?)?,
    })
}

struct Entry {
    name: &'static str,
    crc: u32,
    compressed: u32,
    size: u32,
    offset: u32,
    method: u16,
    flags: u16,
}

const DOS_DATE_1980_01_01: u16 = (1 << 5) | 1;

fn local_header(entry: &Entry) -> Vec<u8> {
    let mut out = Vec::with_capacity(30 + entry.name.len());
    out.extend_from_slice(&0x0403_4b50u32.to_le_bytes());
    out.extend_from_slice(&20u16.to_le_bytes());
    out.extend_from_slice(&entry.flags.to_le_bytes());
    out.extend_from_slice(&entry.method.to_le_bytes());
    out.extend_from_slice(&0u16.to_le_bytes());
    out.extend_from_slice(&DOS_DATE_1980_01_01.to_le_bytes());
    out.extend_from_slice(&entry.crc.to_le_bytes());
    out.extend_from_slice(&entry.compressed.to_le_bytes());
    out.extend_from_slice(&entry.size.to_le_bytes());
    out.extend_from_slice(&(entry.name.len() as u16).to_le_bytes());
    out.extend_from_slice(&0u16.to_le_bytes());
    out.extend_from_slice(entry.name.as_bytes());
    out
}

fn central_header(entry: &Entry) -> Vec<u8> {
    let mut out = Vec::with_capacity(46 + entry.name.len());
    out.extend_from_slice(&0x0201_4b50u32.to_le_bytes());
    out.extend_from_slice(&20u16.to_le_bytes());
    out.extend_from_slice(&20u16.to_le_bytes());
    out.extend_from_slice(&entry.flags.to_le_bytes());
    out.extend_from_slice(&entry.method.to_le_bytes());
    out.extend_from_slice(&0u16.to_le_bytes());
    out.extend_from_slice(&DOS_DATE_1980_01_01.to_le_bytes());
    out.extend_from_slice(&entry.crc.to_le_bytes());
    out.extend_from_slice(&entry.compressed.to_le_bytes());
    out.extend_from_slice(&entry.size.to_le_bytes());
    out.extend_from_slice(&(entry.name.len() as u16).to_le_bytes());
    out.extend_from_slice(&[0; 12]);
    out.extend_from_slice(&entry.offset.to_le_bytes());
    out.extend_from_slice(entry.name.as_bytes());
    out
}

fn small(value: u64) -> Result<u32> {
    u32::try_from(value).map_err(|_| too_large())
}

pub(crate) struct XlsxSink<'a> {
    inner: &'a mut dyn ExportSink,
    entries: Vec<Entry>,
    written: u64,
    sheet_offset: u64,
    started: bool,
    encoder: flate2::write::DeflateEncoder<Vec<u8>>,
    crc: crc32fast::Hasher,
    size: u64,
    compressed: u64,
}

impl<'a> XlsxSink<'a> {
    pub(crate) fn new(inner: &'a mut dyn ExportSink) -> Self {
        Self {
            inner,
            entries: Vec::new(),
            written: 0,
            sheet_offset: 0,
            started: false,
            encoder: flate2::write::DeflateEncoder::new(Vec::new(), flate2::Compression::fast()),
            crc: crc32fast::Hasher::new(),
            size: 0,
            compressed: 0,
        }
    }

    async fn emit(&mut self, bytes: &[u8]) -> Result<()> {
        self.inner.write(bytes).await?;
        self.written += bytes.len() as u64;
        if self.written > MAX_ZIP_BYTES {
            return Err(too_large());
        }
        Ok(())
    }

    async fn start(&mut self) -> Result<()> {
        if self.started {
            return Ok(());
        }
        self.started = true;
        for (name, text) in PARTS {
            let entry = Entry {
                name,
                crc: crc32fast::hash(text.as_bytes()),
                compressed: text.len() as u32,
                size: text.len() as u32,
                offset: small(self.written)?,
                method: 0,
                flags: 0,
            };
            self.emit(&local_header(&entry)).await?;
            self.emit(text.as_bytes()).await?;
            self.entries.push(entry);
        }
        self.sheet_offset = self.written;
        let sheet = Entry {
            name: SHEET_PART,
            crc: 0,
            compressed: 0,
            size: 0,
            offset: small(self.written)?,
            method: 8,
            flags: 1 << 3,
        };
        self.emit(&local_header(&sheet)).await
    }

    async fn drain(&mut self) -> Result<()> {
        let bytes = std::mem::take(self.encoder.get_mut());
        self.compressed += bytes.len() as u64;
        self.emit(&bytes).await
    }

    async fn close(&mut self) -> Result<()> {
        self.start().await?;
        self.encoder
            .try_finish()
            .map_err(|error| DriverError::new(ErrorKind::Io, error.to_string()))?;
        self.drain().await?;
        let sheet = Entry {
            name: SHEET_PART,
            crc: self.crc.clone().finalize(),
            compressed: small(self.compressed)?,
            size: small(self.size)?,
            offset: small(self.sheet_offset)?,
            method: 8,
            flags: 1 << 3,
        };
        let mut descriptor = Vec::with_capacity(16);
        descriptor.extend_from_slice(&0x0807_4b50u32.to_le_bytes());
        descriptor.extend_from_slice(&sheet.crc.to_le_bytes());
        descriptor.extend_from_slice(&sheet.compressed.to_le_bytes());
        descriptor.extend_from_slice(&sheet.size.to_le_bytes());
        self.emit(&descriptor).await?;
        self.entries.push(sheet);
        let directory_offset = small(self.written)?;
        let directory: Vec<u8> = self.entries.iter().flat_map(central_header).collect();
        self.emit(&directory).await?;
        let count = self.entries.len() as u16;
        let mut end = Vec::with_capacity(22);
        end.extend_from_slice(&0x0605_4b50u32.to_le_bytes());
        end.extend_from_slice(&[0; 4]);
        end.extend_from_slice(&count.to_le_bytes());
        end.extend_from_slice(&count.to_le_bytes());
        end.extend_from_slice(&(directory.len() as u32).to_le_bytes());
        end.extend_from_slice(&directory_offset.to_le_bytes());
        end.extend_from_slice(&0u16.to_le_bytes());
        self.emit(&end).await
    }
}

#[async_trait]
impl ExportSink for XlsxSink<'_> {
    async fn write(&mut self, bytes: &[u8]) -> Result<()> {
        self.start().await?;
        self.size += bytes.len() as u64;
        if self.size > MAX_ZIP_BYTES {
            return Err(too_large());
        }
        self.crc.update(bytes);
        self.encoder
            .write_all(bytes)
            .map_err(|error| DriverError::new(ErrorKind::Io, error.to_string()))?;
        if self.encoder.get_ref().len() >= FLUSH_BYTES {
            self.drain().await?;
        }
        Ok(())
    }
    async fn finish(&mut self) -> Result<()> {
        self.close().await?;
        self.inner.finish().await
    }
    async fn abort(&mut self) {
        self.inner.abort().await;
    }
}
