cmake_minimum_required(VERSION 3.16)
project(hpu_seal_cmb013_generator LANGUAGES C CXX)

if(NOT DEFINED INLINE_ASM_ROOT)
    message(FATAL_ERROR "INLINE_ASM_ROOT must point at the pinned HPU_SEAL checkout")
endif()

set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(HPU_ENABLE_SEAL_INTEGRATION ON CACHE BOOL "" FORCE)
set(HPU_ENABLE_SEAL_DIFFERENTIAL_ORACLE OFF CACHE BOOL "" FORCE)
set(HPU_ENABLE_SEAL_BFV_ORACLE OFF CACHE BOOL "" FORCE)
set(SEAL_USE_MSGSL OFF CACHE BOOL "" FORCE)
set(SEAL_USE_ZLIB OFF CACHE BOOL "" FORCE)
set(SEAL_USE_ZSTD OFF CACHE BOOL "" FORCE)
set(SEAL_USE_INTEL_HEXL OFF CACHE BOOL "" FORCE)

add_subdirectory("${INLINE_ASM_ROOT}" inline-asm EXCLUDE_FROM_ALL)

add_executable(hpu_seal_cmb013_generator main.cpp)
target_compile_features(hpu_seal_cmb013_generator PRIVATE cxx_std_17)
target_compile_options(hpu_seal_cmb013_generator PRIVATE -Wall -Wextra -Werror)
target_link_libraries(hpu_seal_cmb013_generator
    PRIVATE
    hpu_seal_ckks_codegen
    hpu_filesystem_compat)
