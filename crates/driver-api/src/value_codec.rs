//! The binary form of one value. The result store and the driver spools share it.
use crate::{Handle, Value};

/// A destination for encoded values.
pub trait ValueWriter {
    type Error;
    fn byte(&mut self, n: u8) -> Result<(), Self::Error>;
    fn u32(&mut self, n: u32) -> Result<(), Self::Error>;
    fn u64(&mut self, n: u64) -> Result<(), Self::Error>;
    /// Writes the length and then the bytes.
    fn bytes(&mut self, bytes: &[u8]) -> Result<(), Self::Error>;
}

/// A source of encoded values. Each read checks its length before it allocates.
pub trait ValueReader {
    type Error;
    fn byte(&mut self) -> Result<u8, Self::Error>;
    fn u32(&mut self) -> Result<u32, Self::Error>;
    fn u64(&mut self) -> Result<u64, Self::Error>;
    /// Reads a length and then the bytes.
    fn bytes(&mut self) -> Result<Vec<u8>, Self::Error>;
    fn corrupt(&self) -> Self::Error;
}

pub fn write_value<W: ValueWriter>(out: &mut W, value: &Value) -> Result<(), W::Error> {
    let text = |out: &mut W, tag, text: &str| {
        out.byte(tag)?;
        out.bytes(text.as_bytes())
    };
    let deferred = |out: &mut W, tag, handle: &Handle, length, database_type: &str| {
        out.byte(tag)?;
        out.u32(handle.slot)?;
        out.u32(handle.generation)?;
        out.u64(length)?;
        out.bytes(database_type.as_bytes())
    };
    match value {
        Value::Null => out.byte(0),
        Value::Bool(v) => {
            out.byte(1)?;
            out.byte(u8::from(*v))
        }
        Value::Integer(v) => {
            out.byte(2)?;
            out.u64(*v as u64)
        }
        Value::Real(v) => {
            out.byte(3)?;
            out.u64(v.to_bits())
        }
        Value::Decimal(v) => text(out, 4, v),
        Value::Text(v) => text(out, 5, v),
        Value::Date(v) => text(out, 6, v),
        Value::Time(v) => text(out, 7, v),
        Value::Timestamp(v) => text(out, 8, v),
        Value::Uuid(v) => text(out, 9, v),
        Value::Json(v) => text(out, 10, v),
        Value::Binary(v) => {
            out.byte(11)?;
            out.bytes(v)
        }
        Value::Deferred {
            handle,
            byte_length,
            database_type,
        } => deferred(out, 12, handle, *byte_length, database_type),
        Value::FallbackText {
            text: value,
            database_type,
        } => {
            text(out, 13, database_type)?;
            out.bytes(value.as_bytes())
        }
        Value::DeferredFallback {
            handle,
            byte_length,
            database_type,
        } => deferred(out, 14, handle, *byte_length, database_type),
        Value::Unavailable {
            database_type,
            reason,
        } => {
            text(out, 15, database_type)?;
            out.bytes(reason.as_bytes())
        }
    }
}

pub fn read_value<R: ValueReader>(input: &mut R) -> Result<Value, R::Error> {
    let text = |input: &mut R| {
        let bytes = input.bytes()?;
        String::from_utf8(bytes).map_err(|_| input.corrupt())
    };
    let handle = |input: &mut R| {
        Ok(Handle {
            slot: input.u32()?,
            generation: input.u32()?,
        })
    };
    Ok(match input.byte()? {
        0 => Value::Null,
        1 => Value::Bool(match input.byte()? {
            0 => false,
            1 => true,
            _ => return Err(input.corrupt()),
        }),
        2 => Value::Integer(input.u64()? as i64),
        3 => Value::Real(f64::from_bits(input.u64()?)),
        4 => Value::Decimal(text(input)?),
        5 => Value::Text(text(input)?),
        6 => Value::Date(text(input)?),
        7 => Value::Time(text(input)?),
        8 => Value::Timestamp(text(input)?),
        9 => Value::Uuid(text(input)?),
        10 => Value::Json(text(input)?),
        11 => Value::Binary(input.bytes()?),
        12 => Value::Deferred {
            handle: handle(input)?,
            byte_length: input.u64()?,
            database_type: text(input)?,
        },
        13 => Value::FallbackText {
            database_type: text(input)?,
            text: text(input)?,
        },
        14 => Value::DeferredFallback {
            handle: handle(input)?,
            byte_length: input.u64()?,
            database_type: text(input)?,
        },
        15 => Value::Unavailable {
            database_type: text(input)?,
            reason: text(input)?,
        },
        _ => return Err(input.corrupt()),
    })
}
