use crate::{Result, StoreError};
pub const INDEX_RECORD_BYTES: usize = 40;
pub(crate) struct Record {
    pub offset: u64,
    pub length: u64,
    pub first_row: u64,
    pub rows: u32,
    pub checksum: u32,
}
impl Record {
    pub fn encode(&self) -> [u8; INDEX_RECORD_BYTES] {
        let mut bytes = [0; INDEX_RECORD_BYTES];
        bytes[0..8].copy_from_slice(&self.offset.to_le_bytes());
        bytes[8..16].copy_from_slice(&self.length.to_le_bytes());
        bytes[16..24].copy_from_slice(&self.first_row.to_le_bytes());
        bytes[24..28].copy_from_slice(&self.rows.to_le_bytes());
        bytes[28..32].copy_from_slice(&self.checksum.to_le_bytes());
        let checksum = crc32fast::hash(&bytes[..32]);
        bytes[32..36].copy_from_slice(&checksum.to_le_bytes());
        bytes[36..40].copy_from_slice(b"PG01");
        bytes
    }
    pub fn decode(bytes: [u8; INDEX_RECORD_BYTES]) -> Result<Self> {
        if &bytes[36..40] != b"PG01"
            || crc32fast::hash(&bytes[..32])
                != u32::from_le_bytes(bytes[32..36].try_into().unwrap())
        {
            return Err(StoreError::Corrupt);
        }
        Ok(Self {
            offset: u64::from_le_bytes(bytes[0..8].try_into().unwrap()),
            length: u64::from_le_bytes(bytes[8..16].try_into().unwrap()),
            first_row: u64::from_le_bytes(bytes[16..24].try_into().unwrap()),
            rows: u32::from_le_bytes(bytes[24..28].try_into().unwrap()),
            checksum: u32::from_le_bytes(bytes[28..32].try_into().unwrap()),
        })
    }
}
