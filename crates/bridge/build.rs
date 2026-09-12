fn main() {
    cxx_build::bridge("src/lib.rs")
        .std("c++17")
        .compile("choscordb-cxx-bridge");
    println!("cargo:rerun-if-changed=src/lib.rs");
    println!("cargo:rerun-if-env-changed=CHOSCORDB_CXX_INCLUDE_DIR");
    if let Some(destination) = std::env::var_os("CHOSCORDB_CXX_INCLUDE_DIR") {
        let source = std::path::PathBuf::from(std::env::var_os("OUT_DIR").expect("Cargo OUT_DIR"))
            .join("cxxbridge/include");
        let destination = std::path::PathBuf::from(destination);
        for file in ["choscordb-bridge/src/lib.rs.h", "rust/cxx.h"] {
            let target = destination.join(file);
            std::fs::create_dir_all(target.parent().expect("header parent"))
                .expect("create CXX include directory");
            std::fs::copy(source.join(file), target).expect("copy generated CXX header");
        }
    }
}
