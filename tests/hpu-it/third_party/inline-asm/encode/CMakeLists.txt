add_library(hpu_encode
        src/instruction.cpp
        src/parser.cpp
        src/encoder.cpp
        src/assembler.cpp
        src/executable.cpp
)

target_include_directories(hpu_encode
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(hpu_encode PUBLIC cxx_std_17)
