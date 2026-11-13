function(choscordb_export_cxx_headers target cargo_target source_dir output_dir)
    set(headers
        "${output_dir}/choscordb-bridge/src/lib.rs.h"
        "${output_dir}/rust/cxx.h"
    )
    set(staged_headers
        "${source_dir}/choscordb-bridge/src/lib.rs.h"
        "${source_dir}/rust/cxx.h"
    )
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
