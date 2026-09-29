pub(crate) fn archive(entries: &[(&str, &[u8])]) -> Vec<u8> {
    let mut output = Vec::new();
    let mut directory = Vec::new();
    for (name, content) in entries {
        let name = name.as_bytes();
        let offset = output.len() as u32;
        let size = content.len() as u32;
        let crc = crc32(content);
        u32le(&mut output, 0x0403_4b50);
        u16le(&mut output, 20);
        u16le(&mut output, 0);
        u16le(&mut output, 0);
        u16le(&mut output, 0);
        u16le(&mut output, 0);
        u32le(&mut output, crc);
        u32le(&mut output, size);
        u32le(&mut output, size);
        u16le(&mut output, name.len() as u16);
        u16le(&mut output, 0);
        output.extend_from_slice(name);
        output.extend_from_slice(content);
        u32le(&mut directory, 0x0201_4b50);
        u16le(&mut directory, 20);
        u16le(&mut directory, 20);
        u16le(&mut directory, 0);
        u16le(&mut directory, 0);
        u16le(&mut directory, 0);
        u16le(&mut directory, 0);
        u32le(&mut directory, crc);
        u32le(&mut directory, size);
        u32le(&mut directory, size);
        u16le(&mut directory, name.len() as u16);
        u16le(&mut directory, 0);
        u16le(&mut directory, 0);
        u16le(&mut directory, 0);
        u16le(&mut directory, 0);
        u32le(&mut directory, 0);
        u32le(&mut directory, offset);
        directory.extend_from_slice(name);
    }
    let directory_offset = output.len() as u32;
    output.extend_from_slice(&directory);
    u32le(&mut output, 0x0605_4b50);
    u16le(&mut output, 0);
    u16le(&mut output, 0);
    u16le(&mut output, entries.len() as u16);
    u16le(&mut output, entries.len() as u16);
    u32le(&mut output, directory.len() as u32);
    u32le(&mut output, directory_offset);
    u16le(&mut output, 0);
    output
}
fn u16le(output: &mut Vec<u8>, value: u16) {
    output.extend_from_slice(&value.to_le_bytes());
}
fn u32le(output: &mut Vec<u8>, value: u32) {
    output.extend_from_slice(&value.to_le_bytes());
}
fn crc32(input: &[u8]) -> u32 {
    let mut crc = !0u32;
    for byte in input {
        crc ^= u32::from(*byte);
        for _ in 0..8 {
            crc = (crc >> 1) ^ (0xedb8_8320 & (0u32.wrapping_sub(crc & 1)));
        }
    }
    !crc
}
