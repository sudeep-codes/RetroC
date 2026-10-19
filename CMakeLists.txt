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

# Common + Lexer library
add_library(rc_lexer STATIC
    src/lexer/token.cpp
    src/lexer/lexer.cpp
)
target_include_directories(rc_lexer PUBLIC src)

# retroc CLI executable
add_executable(retroc
    src/cli/main.cpp
)
target_link_libraries(retroc PRIVATE rc_lexer)

# Testing
enable_testing()

add_executable(unit_tests
    tests/unit/test_sanity.cpp
    tests/unit/test_lexer.cpp
)
target_link_libraries(unit_tests PRIVATE rc_lexer)
target_include_directories(unit_tests PRIVATE tests/unit)

add_test(NAME unit_tests COMMAND unit_tests)

find_package(Python3 COMPONENTS Interpreter QUIET)
if (Python3_Interpreter_FOUND)
    add_test(NAME golden_tests_lexer
        COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/lexer/runner.py $<TARGET_FILE:retroc>
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()
