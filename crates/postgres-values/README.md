# PostgreSQL scalar values

This adapter-independent crate decodes PostgreSQL binary field payloads into the
shared `choscordb-driver-api::Value` contract. It contains no connection, runtime,
UI, SQL execution, or driver registration code.

`decode_bounded(oid, bytes, max_bytes)` checks a conservative owned payload
capacity before allocating. The caller accounts for the fixed `Value` structure
and the input wire buffer separately. Temporal output reserves 48 bytes; NUMERIC
reserves its maximum formatted width from the protocol header. A small budget may
therefore reject a value whose eventual text is shorter. `decode` is the explicitly
unbounded convenience entry point. SQL NULL has no owned payload.

NUMERIC is rendered directly from base-10000 digits with its declared decimal
scale, including NaN and infinities, without any floating point conversion.
Dates and timestamps use the PostgreSQL 2000 epoch and full server ranges;
timestamptz is rendered in UTC, since the wire value has no original zone.
BC output uses PostgreSQL's era suffix. Time accepts the valid 24:00:00 endpoint. Time with time zone preserves its
wire offset, including offset seconds, up to 15:59:59 east or west of UTC.
XML is UTF-8 text. The internal `"char"` type accepts exactly one UTF-8 byte;
non-UTF-8 internal bytes return `InvalidInput` instead of replacement text.
UTF-8 client encoding is a caller requirement. JSON text is preserved, and JSONB's
binary version byte is checked; JSON syntax validation remains the server's job.

Unknown OIDs (including arrays, domains, composites, extension types, and interval)
return `Unsupported`. A driver must explicitly resolve domain base types or add
another codec; it must not interpret their bytes as UTF-8 by default.

Protocol references:
- [NUMERIC send/receive](https://github.com/postgres/postgres/blob/master/src/backend/utils/adt/numeric.c)
- [Temporal epoch, units, bounds and infinity](https://github.com/postgres/postgres/blob/master/src/include/datatype/timestamp.h)
- [JSONB binary version](https://github.com/postgres/postgres/blob/master/src/backend/utils/adt/jsonb.c)

- [Time with time zone binary layout and validation](https://github.com/postgres/postgres/blob/REL_17_STABLE/src/backend/utils/adt/date.c)
