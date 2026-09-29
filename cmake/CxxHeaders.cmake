# Publish Cargo's staged headers as declared build outputs. Order-only Cargo
# dependencies alone cannot make Ninja reconsider consumers when build.rs
# replaces a header after Ninja has computed its initial dirty graph.
function(choscordb_export_cxx_headers target cargo_target source_dir output_dir)
    set(headers
        "${output_dir}/choscordb-bridge/src/lib.rs.h"
        "${output_dir}/rust/cxx.h"
    )
    set(staged_headers
        "${source_dir}/choscordb-bridge/src/lib.rs.h"
        "${source_dir}/rust/cxx.h"
    )
    # Declare the headers written by build.rs as Cargo byproducts, so Ninja
    # propagates their changed mtimes through the publication command.
    add_custom_command(TARGET ${cargo_target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E true
        BYPRODUCTS ${staged_headers}
        VERBATIM
    )
    add_custom_command(
        OUTPUT ${headers}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${output_dir}/choscordb-bridge/src" "${output_dir}/rust"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${source_dir}/choscordb-bridge/src/lib.rs.h" "${output_dir}/choscordb-bridge/src/lib.rs.h"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${source_dir}/rust/cxx.h" "${output_dir}/rust/cxx.h"
        DEPENDS ${cargo_target} ${staged_headers}
        COMMENT "Publishing typed CXX headers after Cargo"
        VERBATIM
    )
    add_custom_target(${target}-headers DEPENDS ${headers})
    target_sources(${target} INTERFACE ${headers})
    add_dependencies(${target} ${target}-headers)
endfunction()
