/// Physical footprint as reported by the same macOS Mach task info field used by
/// the previous native diagnostics sampler. A failed kernel query is unavailable.
#[cfg(target_os = "macos")]
// This one Mach call needs raw pointers; the buffer/count invariant is documented
// at the call below and covered by the native footprint test. libc marks its
// mach_task_self wrapper deprecated in favor of mach2, but using that existing
// binding keeps this one-field sampler independent of a second Mach crate.
#[allow(unsafe_code, deprecated)]
pub(crate) fn footprint_bytes() -> Option<u64> {
    use std::mem::size_of;

    // Prefix through `phys_footprint` of Apple's task_vm_info (rev 1).
    // The kernel accepts TASK_VM_INFO_REV1_COUNT and writes only this prefix.
    #[repr(C)]
    #[derive(Default)]
    struct TaskVmInfoRev1 {
        virtual_size: u64,
        region_count: i32,
        page_size: i32,
        resident_size: u64,
        resident_size_peak: u64,
        other_sizes: [u64; 14],
        phys_footprint: u64,
    }
    const TASK_VM_INFO: libc::task_flavor_t = 22;
    const _: () = assert!(size_of::<TaskVmInfoRev1>() == 38 * size_of::<u32>());
    let mut info = TaskVmInfoRev1::default();
    let mut count =
        (size_of::<TaskVmInfoRev1>() / size_of::<u32>()) as libc::mach_msg_type_number_t;
    // SAFETY: `info` has the C task_vm_info rev-1 prefix layout, and `count`
    // limits the kernel write to the full allocated buffer. We read the field
    // by value only after success and a count that includes it.
    let status = unsafe {
        libc::task_info(
            libc::mach_task_self(),
            TASK_VM_INFO,
            (&raw mut info).cast::<libc::integer_t>(),
            &raw mut count,
        )
    };
    (status == 0 && count as usize * size_of::<u32>() >= size_of::<TaskVmInfoRev1>())
        .then_some(info.phys_footprint)
}

#[cfg(not(target_os = "macos"))]
pub(crate) fn footprint_bytes() -> Option<u64> {
    None
}
