use crate::{DriverError, ErrorKind, Result};

fn invalid() -> DriverError {
    DriverError::new(
        ErrorKind::InvalidInput,
        "Invalid or unsupported connection URL",
    )
}

fn decode(input: &str) -> Result<String> {
    let bytes = input.as_bytes();
    for (index, byte) in bytes.iter().enumerate() {
        if *byte == b'%'
            && (index + 2 >= bytes.len()
                || !bytes[index + 1].is_ascii_hexdigit()
                || !bytes[index + 2].is_ascii_hexdigit())
        {
            return Err(invalid());
        }
    }
    let value = percent_encoding::percent_decode_str(input)
        .decode_utf8()
        .map_err(|_| invalid())?;
    if value.len() > 16 * 1024 || value.contains('\0') {
        return Err(invalid());
    }
    Ok(value.into_owned())
}

/// Validate SQLite's explicit `file:` URI without rewriting its escaped path.
/// The result includes URI-imposed read-only settings. Ordinary paths are not URIs.
pub fn validate_sqlite_uri(uri: &str, read_only: bool) -> Result<bool> {
    if !uri.starts_with("file:")
        || uri.len() > 16 * 1024
        || uri.contains('\0')
        || uri.chars().any(char::is_control)
    {
        return Err(invalid());
    }
    let without_fragment = uri.split_once('#').map_or(uri, |(uri, _)| uri);
    let (location, query) = without_fragment
        .split_once('?')
        .map_or((without_fragment, None), |(location, query)| {
            (location, Some(query))
        });
    let path = location.strip_prefix("file:").ok_or_else(invalid)?;
    if path.is_empty() {
        return Err(invalid());
    }
    if let Some(authority) = path.strip_prefix("//") {
        let authority = authority.split('/').next().unwrap_or_default();
        if !authority.is_empty() && authority != "localhost" {
            return Err(invalid());
        }
    }
    decode(path)?;
    let mut mode = None;
    let mut immutable = false;
    let mut seen = std::collections::HashSet::new();
    if let Some(query) = query.filter(|query| !query.is_empty()) {
        for pair in query.split('&') {
            let (key, value) = pair.split_once('=').ok_or_else(invalid)?;
            let key = decode(key)?;
            let value = decode(value)?;
            if !seen.insert(key.clone()) {
                return Err(invalid());
            }
            match key.as_str() {
                "mode" if ["ro", "rw", "rwc", "memory"].contains(&value.as_str()) => {
                    mode = Some(value)
                }
                "cache" if ["shared", "private"].contains(&value.as_str()) => (),
                "immutable" | "nolock" | "psow" => {
                    let enabled = match value.to_ascii_lowercase().as_str() {
                        "1" | "true" | "yes" | "on" => true,
                        "0" | "false" | "no" | "off" => false,
                        _ => return Err(invalid()),
                    };
                    if key == "immutable" {
                        immutable = enabled;
                    }
                }
                "modeof" | "vfs" if !value.is_empty() => (),
                _ => return Err(invalid()),
            }
        }
    }
    if (read_only || immutable) && matches!(mode.as_deref(), Some("rw" | "rwc" | "memory")) {
        return Err(invalid());
    }
    Ok(read_only || immutable || mode.as_deref() == Some("ro"))
}
