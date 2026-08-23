cmake_minimum_required(VERSION 3.16)

project(IT_SCPU_TestCases LANGUAGES C CXX)

include(CTest)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(HPU_INLINE_ASM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/inline-asm")
if(NOT EXISTS "${HPU_INLINE_ASM_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "Missing third_party/inline-asm; run git submodule update --init --recursive"
    )
endif()

add_custom_target(hpu_it_vector_assets
    COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/scripts/prepare_inline_vectors.sh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    USES_TERMINAL
    COMMENT "Generating and validating inline-asm Q4 NTT/INTT assets"
)

set(HPU_IT_CATEGORY_DIRS
    01_configuration
    02_data_paths
    03_compute_instructions
    04_composite_instruction_sequences
    05_cpu_hpu_structural_connectivity
    06_performance
    07_full_application
)

set(HPU_IT_CASE_SOURCES)
foreach(category IN LISTS HPU_IT_CATEGORY_DIRS)
    file(GLOB_RECURSE category_sources CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/${category}/*.c"
    )
    list(APPEND HPU_IT_CASE_SOURCES ${category_sources})
endforeach()
list(SORT HPU_IT_CASE_SOURCES)
list(LENGTH HPU_IT_CASE_SOURCES HPU_IT_CASE_COUNT)
if(NOT HPU_IT_CASE_COUNT EQUAL 49)
    message(FATAL_ERROR "Expected 49 testcase sources, found ${HPU_IT_CASE_COUNT}")
endif()

add_library(hpu_it_runtime STATIC
    runtime/hpu_test.c
    runtime/hpu_vectors.c
    runtime/hpu_generated_ops.c
    "${HPU_INLINE_ASM_DIR}/outputs/encode/encode.c"
    "${HPU_INLINE_ASM_DIR}/outputs/rescale/rescale.c"
    "${HPU_INLINE_ASM_DIR}/outputs/mm/mm.c"
    "${HPU_INLINE_ASM_DIR}/outputs/pmult/pmult.c"
    "${HPU_INLINE_ASM_DIR}/outputs/cmult/cmult.c"
    "${HPU_INLINE_ASM_DIR}/outputs/modup/modup.c"
    "${HPU_INLINE_ASM_DIR}/outputs/moddown/moddown.c"
    "${HPU_INLINE_ASM_DIR}/outputs/auto/auto.c"
)
target_include_directories(hpu_it_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(hpu_it_runtime PRIVATE
    HPU_IT_VECTOR_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
    HPU_IT_USE_GENERATED_MM=1
)
add_dependencies(hpu_it_runtime hpu_it_vector_assets)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hpu_it_runtime PRIVATE -Wall -Wextra -Werror)
endif()

add_executable(hpu_it_relocation_dump
    runtime/hpu_relocation_dump.c
    runtime/hpu_fhe.c
    "${HPU_INLINE_ASM_DIR}/outputs/keyswitch/keyswitch.c"
    "${HPU_INLINE_ASM_DIR}/outputs/relinearization/relinearization.c"
    "${HPU_INLINE_ASM_DIR}/outputs/ciphertext_multiply/ciphertext_multiply.c"
)
target_link_libraries(hpu_it_relocation_dump PRIVATE hpu_it_runtime)
target_include_directories(hpu_it_relocation_dump PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(hpu_it_relocation_dump PRIVATE
    HPU_IT_VECTOR_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
    HPU_IT_USE_GENERATED_FHE=1
)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hpu_it_relocation_dump PRIVATE -Wall -Wextra -Werror)
endif()
set(HPU_IT_RESOLVED_MANIFESTS)
foreach(operator mm pmult cmult modup moddown auto encode rescale
                 keyswitch relinearization ciphertext_multiply)
    set(resolved_manifest
        "${CMAKE_BINARY_DIR}/resolved-relocations/${operator}.csv")
    add_custom_command(
        OUTPUT "${resolved_manifest}"
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "${CMAKE_BINARY_DIR}/resolved-relocations"
        COMMAND $<TARGET_FILE:hpu_it_relocation_dump>
                "${operator}"
                "${HPU_INLINE_ASM_DIR}/outputs/${operator}/dma_relocation_manifest.csv"
                "${resolved_manifest}"
        DEPENDS hpu_it_relocation_dump hpu_it_vector_assets
                "${HPU_INLINE_ASM_DIR}/outputs/${operator}/dma_relocation_manifest.csv"
        VERBATIM
    )
    list(APPEND HPU_IT_RESOLVED_MANIFESTS "${resolved_manifest}")
endforeach()
add_custom_target(hpu_it_resolved_relocations ALL
    DEPENDS ${HPU_IT_RESOLVED_MANIFESTS}
)

set(HPU_IT_EXECUTABLE_TARGETS)
set(HPU_IT_FHE_CASE_IDS
    HPU_IT_DIR_CMB_004
    HPU_IT_DIR_CMB_010
    HPU_IT_DIR_CMB_012
    HPU_IT_DIR_PERF_001
    HPU_IT_DIR_PERF_005
    HPU_IT_DIR_PERF_006
    HPU_IT_DIR_APP_001
)
foreach(case_source IN LISTS HPU_IT_CASE_SOURCES)
    get_filename_component(case_id "${case_source}" NAME_WE)
    add_executable(${case_id}
        "${case_source}"
        runtime/hpu_test_main.c
    )
    target_link_libraries(${case_id} PRIVATE hpu_it_runtime)
    target_include_directories(${case_id} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(case_id IN_LIST HPU_IT_FHE_CASE_IDS)
        target_sources(${case_id} PRIVATE
            runtime/hpu_fhe.c
            "${HPU_INLINE_ASM_DIR}/outputs/keyswitch/keyswitch.c"
            "${HPU_INLINE_ASM_DIR}/outputs/relinearization/relinearization.c"
            "${HPU_INLINE_ASM_DIR}/outputs/ciphertext_multiply/ciphertext_multiply.c"
        )
        target_compile_definitions(${case_id} PRIVATE
            HPU_IT_VECTOR_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
            HPU_IT_USE_GENERATED_FHE=1
        )
        add_dependencies(${case_id} hpu_it_vector_assets)
    endif()
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${case_id} PRIVATE -Wall -Wextra -Werror)
    endif()
    set_target_properties(${case_id} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/cases"
    )
    add_test(NAME ${case_id} COMMAND $<TARGET_FILE:${case_id}>)
    if(case_id STREQUAL "HPU_IT_DIR_CMB_015")
        set_tests_properties(${case_id} PROPERTIES
            LABELS "self-check;blocked"
            WILL_FAIL TRUE
        )
    else()
        set_tests_properties(${case_id} PROPERTIES
            LABELS "self-check"
        )
    endif()
    list(APPEND HPU_IT_EXECUTABLE_TARGETS ${case_id})
endforeach()

file(GLOB HPU_IT_GENERATED_OPERATOR_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/08_generated_inline_asm_operators/*.c"
)
list(SORT HPU_IT_GENERATED_OPERATOR_SOURCES)
list(LENGTH HPU_IT_GENERATED_OPERATOR_SOURCES HPU_IT_GENERATED_OPERATOR_COUNT)
if(NOT HPU_IT_GENERATED_OPERATOR_COUNT EQUAL 4)
    message(FATAL_ERROR
        "Expected 4 supplemental generated-operator sources, found ${HPU_IT_GENERATED_OPERATOR_COUNT}"
    )
endif()
set(HPU_IT_GENERATED_OPERATOR_TARGETS)
foreach(case_source IN LISTS HPU_IT_GENERATED_OPERATOR_SOURCES)
    get_filename_component(case_id "${case_source}" NAME_WE)
    add_executable(${case_id}
        "${case_source}"
        runtime/hpu_test_main.c
    )
    target_link_libraries(${case_id} PRIVATE hpu_it_runtime)
    target_include_directories(${case_id} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${case_id} PRIVATE -Wall -Wextra -Werror)
    endif()
    set_target_properties(${case_id} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/generated-operators"
    )
    add_test(NAME ${case_id} COMMAND $<TARGET_FILE:${case_id}>)
    set_tests_properties(${case_id} PROPERTIES
        LABELS "self-check;generated-operator"
    )
    list(APPEND HPU_IT_GENERATED_OPERATOR_TARGETS ${case_id})
endforeach()

add_custom_target(hpu_it_generated_operator_executables ALL
    DEPENDS ${HPU_IT_GENERATED_OPERATOR_TARGETS}
)

add_custom_target(hpu_it_case_executables ALL
    DEPENDS ${HPU_IT_EXECUTABLE_TARGETS}
)

# Compatibility syntax-check target retained for existing automation.
add_library(hpu_it_case_objects OBJECT EXCLUDE_FROM_ALL ${HPU_IT_CASE_SOURCES})
target_include_directories(hpu_it_case_objects PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

option(HPU_ENABLE_INLINE_ASM "Build third_party/inline-asm host tools" ON)
if(HPU_ENABLE_INLINE_ASM)
    add_subdirectory(third_party/inline-asm EXCLUDE_FROM_ALL)

    add_library(hpu_inline_asm_codegen_headers INTERFACE)
    target_include_directories(hpu_inline_asm_codegen_headers SYSTEM INTERFACE
        "${HPU_INLINE_ASM_DIR}/include"
    )
    add_library(IT_SCPU::inline_asm_codegen_headers ALIAS hpu_inline_asm_codegen_headers)
    add_library(IT_SCPU::hpu_encode ALIAS hpu_encode)

    set(HPU_INLINE_ASM_TOOL_TARGETS
        inline_asm_codegen
        inline_asm_encode_outputs
        hpu_reference_vectors
    )
    if(TARGET hpu_encode_self_test)
        list(APPEND HPU_INLINE_ASM_TOOL_TARGETS hpu_encode_self_test)
    endif()
    add_custom_target(hpu_it_inline_asm_tools
        DEPENDS ${HPU_INLINE_ASM_TOOL_TARGETS}
    )
endif()

set(HPU_NEXUS_AM_HOME "" CACHE PATH "Path to an OpenXiangShan/nexus-am checkout")
set(HPU_IT_MEM_BASE "0x87000000" CACHE STRING "Shared-DRAM HPU window base")
set(HPU_IT_MEM_LINES "19201" CACHE STRING "Shared-DRAM HPU window size in 256-byte lines")
if(HPU_NEXUS_AM_HOME)
    add_custom_target(hpu_it_nexus_images
        COMMAND ${CMAKE_COMMAND} -E env
            "AM_HOME=${HPU_NEXUS_AM_HOME}"
            "HPU_IT_MEM_BASE=${HPU_IT_MEM_BASE}"
            "HPU_IT_MEM_LINES=${HPU_IT_MEM_LINES}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/build_nexus_am.sh"
            "${CMAKE_BINARY_DIR}/nexus-am"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        USES_TERMINAL
        COMMENT "Building all 49 Nexus-AM RISC-V ELF images"
    )
endif()
