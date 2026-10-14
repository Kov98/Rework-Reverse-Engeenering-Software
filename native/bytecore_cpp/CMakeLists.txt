cmake_minimum_required(VERSION 3.20)
project(reworkbench_bytecore_cpp VERSION 0.1.0 LANGUAGES CXX)

find_package(Threads REQUIRED)

add_library(reworkbench_bytecore_cpp STATIC
    src/scanner.cpp
)
target_include_directories(reworkbench_bytecore_cpp PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_features(reworkbench_bytecore_cpp PUBLIC cxx_std_20)
target_link_libraries(reworkbench_bytecore_cpp PUBLIC Threads::Threads)
set_target_properties(reworkbench_bytecore_cpp PROPERTIES CXX_EXTENSIONS OFF)

add_executable(bytecore_cpp src/main.cpp)
target_link_libraries(bytecore_cpp PRIVATE reworkbench_bytecore_cpp)
set_target_properties(bytecore_cpp PROPERTIES CXX_EXTENSIONS OFF)

foreach(target reworkbench_bytecore_cpp bytecore_cpp)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /EHsc)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()

# MinGW needs the Unicode CRT entry point for wmain. MSVC selects it from
# the entry-point symbol automatically. The scanner never invokes a shell.
if(MINGW)
    target_link_options(bytecore_cpp PRIVATE -municode)
endif()

install(TARGETS bytecore_cpp RUNTIME DESTINATION bin)
