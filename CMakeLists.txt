cmake_minimum_required(VERSION 3.16)
project(RetroC VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RC_SANITIZE "Enable Address and Undefined sanitizers" OFF)

if (MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Werror)
    if (RC_SANITIZE)
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

# Common library
add_library(rc_common STATIC
    src/common/value.cpp
)
target_include_directories(rc_common PUBLIC src)

# Lexer library
add_library(rc_lexer STATIC
    src/lexer/token.cpp
    src/lexer/lexer.cpp
)
target_link_libraries(rc_lexer PUBLIC rc_common)
target_include_directories(rc_lexer PUBLIC src)

# Parser library
add_library(rc_parser STATIC
    src/parser/ast.cpp
    src/parser/parser.cpp
)
target_link_libraries(rc_parser PUBLIC rc_lexer)
target_include_directories(rc_parser PUBLIC src)

# Sema library
add_library(rc_sema STATIC
    src/sema/types.cpp
    src/sema/sema.cpp
)
target_link_libraries(rc_sema PUBLIC rc_parser)
target_include_directories(rc_sema PUBLIC src)

# IR library
add_library(rc_ir STATIC
    src/ir/expr_ir.cpp
    src/ir/node.cpp
    src/ir/graph.cpp
    src/ir/dump.cpp
    src/ir/builder.cpp
)
target_link_libraries(rc_ir PUBLIC rc_sema)
target_include_directories(rc_ir PUBLIC src)

# Bytecode library
add_library(rc_bytecode STATIC
    src/bytecode/bytecode.cpp
)
target_link_libraries(rc_bytecode PUBLIC rc_ir)
target_include_directories(rc_bytecode PUBLIC src)

# Runtime library
add_library(rc_runtime STATIC
    src/runtime/csv.cpp
    src/runtime/executor.cpp
)
target_link_libraries(rc_runtime PUBLIC rc_bytecode)
target_include_directories(rc_runtime PUBLIC src)

# Edit library
add_library(rc_edit STATIC
    src/edit/edit.cpp
    src/edit/source_rewrite.cpp
)
target_link_libraries(rc_edit PUBLIC rc_lexer rc_common)
target_include_directories(rc_edit PUBLIC src)

# Reuse library
add_library(rc_reuse STATIC
    src/reuse/propagator.cpp
)
target_link_libraries(rc_reuse PUBLIC rc_runtime rc_edit)
target_include_directories(rc_reuse PUBLIC src)

# Verify library
add_library(rc_verify STATIC
    src/verify/verifier.cpp
)
target_link_libraries(rc_verify PUBLIC rc_reuse)
target_include_directories(rc_verify PUBLIC src)

# retroc CLI executable
add_executable(retroc
    src/cli/main.cpp
)
target_link_libraries(retroc PRIVATE rc_verify)

# Testing
enable_testing()

add_executable(unit_tests
    tests/unit/test_sanity.cpp
    tests/unit/test_lexer.cpp
    tests/unit/test_parser.cpp
    tests/unit/test_sema.cpp
    tests/unit/test_ir.cpp
    tests/unit/test_runtime.cpp
    tests/unit/test_edit.cpp
)
target_link_libraries(unit_tests PRIVATE rc_verify)
target_include_directories(unit_tests PRIVATE tests/unit)

add_test(NAME unit_tests COMMAND unit_tests)

find_package(Python3 COMPONENTS Interpreter QUIET)
if (Python3_Interpreter_FOUND)
    add_test(NAME golden_tests_lexer
        COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/lexer/runner.py $<TARGET_FILE:retroc>
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
    add_test(NAME golden_tests_parser
        COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/parser/runner.py $<TARGET_FILE:retroc>
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
    add_test(NAME golden_tests_sema
        COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/sema/runner.py $<TARGET_FILE:retroc>
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
    add_test(NAME golden_tests_ir
        COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/ir/runner.py $<TARGET_FILE:retroc>
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
    add_test(NAME golden_tests_run
        COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/run/runner.py $<TARGET_FILE:retroc>
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
    add_test(NAME golden_tests_edit
        COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/edit/runner.py $<TARGET_FILE:retroc>
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()

# Differential testing executable (difftest)
add_executable(difftest
    tests/diff/runner.cpp
)
target_link_libraries(difftest PRIVATE rc_verify)

add_test(NAME diff_tests
    COMMAND difftest --trials 1000 --seed 1
)

# Benchmark executables
add_executable(gen_workload
    bench/gen_workload.cpp
)

add_executable(bench_runner
    bench/runner.cpp
)
target_link_libraries(bench_runner PRIVATE rc_verify)



