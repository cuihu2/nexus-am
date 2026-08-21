cmake_minimum_required(VERSION 3.16)

project(inline_asm_codegen LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(encode)

add_executable(inline_asm_codegen
        src/main.cpp
        src/util/ntt.cpp
        src/util/mm.cpp
        src/util/bconv.cpp
        src/poly/auto.cpp
        src/poly/modup.cpp
        src/poly/moddown.cpp
        src/poly/pmult.cpp
        src/poly/cmult.cpp
        src/operator/keyswitch.cpp
        src/operator/relinearization.cpp
        src/operator/ciphertext_multiply.cpp
        src/operator/encode.cpp
        src/operator/rescale.cpp
)

target_include_directories(inline_asm_codegen
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_subdirectory(test/encode)
add_subdirectory(test/reference)

set(HPU_DELIVERY_TEST_DEPENDENCIES)
if(BUILD_TESTING)
    list(APPEND HPU_DELIVERY_TEST_DEPENDENCIES hpu_encode_self_test)
endif()

add_custom_target(hpu_delivery
        COMMAND $<TARGET_FILE:inline_asm_codegen> both
        COMMAND $<TARGET_FILE:inline_asm_encode_outputs>
        COMMAND $<TARGET_FILE:hpu_reference_vectors>
                ${CMAKE_CURRENT_SOURCE_DIR}/outputs/ciphertext_multiply/test_data
                ${CMAKE_CURRENT_SOURCE_DIR}/outputs
        COMMAND ${CMAKE_COMMAND}
                -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/test/delivery/check_delivery.cmake
        DEPENDS
        inline_asm_codegen
        inline_asm_encode_outputs
        hpu_reference_vectors
        ${HPU_DELIVERY_TEST_DEPENDENCIES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating HPU instruction streams and FHE reference deliverables"
)
