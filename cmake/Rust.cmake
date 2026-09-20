include(FetchContent)
FetchContent_Declare(Corrosion
    GIT_REPOSITORY https://github.com/corrosion-rs/corrosion.git
    GIT_TAG 1499b14e4906a2890f5cee1547c8848db261753d # v0.6.1
)
FetchContent_MakeAvailable(Corrosion)
corrosion_import_crate(MANIFEST_PATH "${PROJECT_SOURCE_DIR}/Cargo.toml" CRATES choscordb-bridge LOCKED)
# Corrosion's global RUSTFLAGS joins arguments with spaces. Cargo's encoded form
# preserves paths containing spaces and applies to every Rust dependency, unlike
# target-local flags passed after `cargo rustc --`.
get_target_property(choscordb_rust_compiler Rust::Rustc IMPORTED_LOCATION)
choscordb_rust_privacy_environment(choscordb_privacy_environment "${choscordb_rust_compiler}")
if(choscordb_privacy_environment)
    corrosion_set_env_vars(choscordb_bridge ${choscordb_privacy_environment})
endif()
# build.rs generates and compiles the CXX implementation. Corrosion owns Cargo
# invocation and Rust runtime link dependencies; consumers wait for headers.
corrosion_set_env_vars(choscordb_bridge "CHOSCORDB_CXX_INCLUDE_DIR=${PROJECT_BINARY_DIR}/generated/cxxbridge")
if(CHOSCORDB_UPDATE_REHEARSAL)
    corrosion_set_env_vars(choscordb_bridge "CHOSCORDB_CREDENTIAL_SERVICE=com.choscor.ChoscorDB.tests.rehearsal")
else()
    corrosion_set_env_vars(choscordb_bridge "CHOSCORDB_CREDENTIAL_SERVICE=com.choscor.ChoscorDB")
endif()
if(APPLE AND CMAKE_OSX_DEPLOYMENT_TARGET)
    corrosion_set_env_vars(choscordb_bridge "MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}")
endif()
add_library(choscordb-rust INTERFACE)
target_link_libraries(choscordb-rust INTERFACE choscordb_bridge)
target_include_directories(choscordb-rust INTERFACE "${PROJECT_BINARY_DIR}/generated/cxxbridge")
add_dependencies(choscordb-rust cargo-build_choscordb_bridge)

# Rust's standard-library native link list does not include credential backend libraries.
if(APPLE)
    find_library(CHOSCORDB_SECURITY_FRAMEWORK Security REQUIRED)
    find_library(CHOSCORDB_COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    find_library(CHOSCORDB_SYSTEM_CONFIGURATION_FRAMEWORK SystemConfiguration REQUIRED)
    target_link_libraries(choscordb-rust INTERFACE ${CHOSCORDB_SECURITY_FRAMEWORK} ${CHOSCORDB_COREFOUNDATION_FRAMEWORK} ${CHOSCORDB_SYSTEM_CONFIGURATION_FRAMEWORK})
elseif(WIN32)
    target_link_libraries(choscordb-rust INTERFACE advapi32 crypt32 secur32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(CHOSCORDB_DBUS REQUIRED IMPORTED_TARGET dbus-1)
    find_package(OpenSSL REQUIRED)
    target_link_libraries(choscordb-rust INTERFACE PkgConfig::CHOSCORDB_DBUS OpenSSL::SSL OpenSSL::Crypto)
endif()
