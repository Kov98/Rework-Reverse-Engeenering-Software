cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(REWorkbench VERSION 0.3.0 LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    foreach(configuration DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${configuration} "${CMAKE_BINARY_DIR}/bin")
    endforeach()
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../bytecore_cpp" "${CMAKE_CURRENT_BINARY_DIR}/bytecore_cpp")
endif()
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Concurrent Sql)
qt_standard_project_setup()
add_library(reworkbench_native STATIC
    src/types.cpp src/binary_image.cpp src/loaders.cpp
    src/decoder.cpp src/analysis.cpp src/ir.cpp
    src/project.cpp src/serialization.cpp
    src/flow_recovery.cpp src/enrichment.cpp src/debug_symbols.cpp
    src/decompiler.cpp src/decompiler_lift.cpp src/decompilation_cache.cpp
    src/investigation_files.cpp src/investigation_emulator.cpp
    src/debugger.cpp src/plugin.cpp src/benchmark.cpp
)
target_compile_features(reworkbench_native PUBLIC cxx_std_20)
target_include_directories(reworkbench_native PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(reworkbench_native PUBLIC Qt6::Core Qt6::Sql reworkbench_bytecore_cpp)
if(WIN32)
    target_link_libraries(reworkbench_native PRIVATE kernel32)
endif()
option(REWORKBENCH_WITH_CAPSTONE "Use an already installed Capstone5 instruction decoder" ON)
if(REWORKBENCH_WITH_CAPSTONE)
    find_path(REWORKBENCH_CAPSTONE_INCLUDE_DIR NAMES capstone/capstone.h)
    find_library(REWORKBENCH_CAPSTONE_LIBRARY NAMES capstone capstone_static)
endif()
if(REWORKBENCH_WITH_CAPSTONE AND REWORKBENCH_CAPSTONE_INCLUDE_DIR AND REWORKBENCH_CAPSTONE_LIBRARY)
    target_compile_definitions(reworkbench_native PRIVATE REWORKBENCH_HAS_CAPSTONE=1)
    target_include_directories(reworkbench_native PRIVATE "${REWORKBENCH_CAPSTONE_INCLUDE_DIR}")
    target_link_libraries(reworkbench_native PRIVATE "${REWORKBENCH_CAPSTONE_LIBRARY}")
else()
    target_compile_definitions(reworkbench_native PRIVATE REWORKBENCH_HAS_CAPSTONE=0)
    message(STATUS "Capstone5 not found: format/bytes/string analysis available; assembly and IR decoding unavailable")
endif()
qt_add_executable(reworkbench WIN32
    src/main.cpp ui/record_model.cpp ui/hex_view.cpp
    ui/graph_view.cpp ui/import_dialog.cpp ui/main_window.cpp
    ui/investigation_actions.cpp ui/code_highlighter.cpp ui/debugger_dialog.cpp
)
target_include_directories(reworkbench PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/ui")
target_link_libraries(reworkbench PRIVATE reworkbench_native Qt6::Widgets Qt6::Concurrent)
qt_add_executable(reworkbench-cli src/cli_main.cpp)
target_link_libraries(reworkbench-cli PRIVATE reworkbench_native)
foreach(target reworkbench_native reworkbench reworkbench-cli)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /EHsc)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()
install(TARGETS reworkbench RUNTIME DESTINATION bin BUNDLE DESTINATION .)
install(TARGETS reworkbench-cli RUNTIME DESTINATION bin)
option(REWORKBENCH_BUILD_EXAMPLE_EXTENSION "Build the optional native extension example" OFF)
if(REWORKBENCH_BUILD_EXAMPLE_EXTENSION)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../extensions/example" "${CMAKE_CURRENT_BINARY_DIR}/example_extension")
endif()
