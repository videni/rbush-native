cmake_minimum_required(VERSION 3.20)
project(RBushNative VERSION 4.0.1 LANGUAGES C CXX)

add_library(rbush_core INTERFACE)
add_library(rbush::core ALIAS rbush_core)
target_include_directories(rbush_core INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(rbush_core INTERFACE cxx_std_17)
set_target_properties(rbush_core PROPERTIES EXPORT_NAME core)

add_library(rbush_c STATIC bindings/c/src/rbush_c.cpp)
add_library(rbush::c ALIAS rbush_c)
target_link_libraries(rbush_c PUBLIC rbush_core)
target_include_directories(rbush_c PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/bindings/c/include>
    $<INSTALL_INTERFACE:include>)
set_target_properties(rbush_c PROPERTIES CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON EXPORT_NAME c)

option(RBUSH_SANITIZERS "Enable address and undefined-behavior sanitizers for native validation" OFF)
if(RBUSH_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU" AND NOT EMSCRIPTEN)
    target_compile_options(rbush_c PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(rbush_c PUBLIC -fsanitize=address,undefined)
endif()

include(CTest)
if(BUILD_TESTING)
    foreach(test_name core_smoke upstream_tests)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/${test_name}.cpp")
            add_executable(rbush_${test_name} "tests/${test_name}.cpp")
            target_link_libraries(rbush_${test_name} PRIVATE rbush_c)
            # The source smoke checks use assert; keep them active in Release.
            if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
                target_compile_options(rbush_${test_name} PRIVATE -UNDEBUG)
            elseif(MSVC)
                target_compile_options(rbush_${test_name} PRIVATE /UNDEBUG)
            endif()
            add_test(NAME ${test_name} COMMAND rbush_${test_name})
        endif()
    endforeach()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/c_adapter_test.cpp")
        # This private bookkeeping fixture includes the bridge implementation.
        # Linking rbush_c too would define the C API twice.
        add_executable(rbush_c_adapter_tests tests/c_adapter_test.cpp)
        target_link_libraries(rbush_c_adapter_tests PRIVATE rbush_core)
        target_include_directories(rbush_c_adapter_tests PRIVATE bindings/c/include)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(rbush_c_adapter_tests PRIVATE -UNDEBUG)
        elseif(MSVC)
            target_compile_options(rbush_c_adapter_tests PRIVATE /UNDEBUG)
        endif()
        if(RBUSH_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU" AND NOT EMSCRIPTEN)
            target_compile_options(rbush_c_adapter_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(rbush_c_adapter_tests PRIVATE -fsanitize=address,undefined)
        endif()
        add_test(NAME c_adapter COMMAND rbush_c_adapter_tests)
    endif()
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/parity_cli.cpp")
    add_executable(rbush_parity_cli tests/parity_cli.cpp)
    target_link_libraries(rbush_parity_cli PRIVATE rbush_c)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/perf.cpp")
    add_executable(rbush_bench benchmarks/perf.cpp)
    target_link_libraries(rbush_bench PRIVATE rbush_c)
endif()

include(GNUInstallDirs)
install(TARGETS rbush_core rbush_c EXPORT RBushTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/rbush.hpp include/quickselect.hpp bindings/c/include/rbush.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT RBushTargets NAMESPACE rbush:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RBush)

# Same serialized canvas fixture for original JavaScript and native C++.
add_executable(rbush_canvas_fixture benchmarks/canvas_fixture.cpp)
target_link_libraries(rbush_canvas_fixture PRIVATE rbush_core)
