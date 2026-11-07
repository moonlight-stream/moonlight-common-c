# Standalone: cmake -S tests -B build-mic-tests && cmake --build build-mic-tests
# This target compiles the same production source without network/crypto deps.
cmake_minimum_required(VERSION 3.1...4.0)
project(mic-negotiation-tests LANGUAGES C)
enable_testing()
set(CMAKE_C_STANDARD 11)
add_executable(test-mic-negotiation test_mic_negotiation.c)
target_include_directories(test-mic-negotiation PRIVATE ../src)
if(MSVC)
  target_compile_options(test-mic-negotiation PRIVATE /W4 /WX)
else()
  target_compile_options(test-mic-negotiation PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME mic-negotiation COMMAND test-mic-negotiation)
