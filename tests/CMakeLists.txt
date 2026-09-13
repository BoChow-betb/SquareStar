if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.24)
    project(SquareStarPortableTests LANGUAGES CXX)
    enable_testing()
endif()

option(SQUARESTAR_STRICT_WARNINGS
    "Enable capability-checked warnings-as-errors for first-party sources" OFF)
option(SQUARESTAR_BUILD_FUZZERS
    "Build Clang libFuzzer targets for untrusted input boundaries" OFF)
option(SQUARESTAR_ENABLE_SANITIZERS
    "Instrument portable tests with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

if(SQUARESTAR_ENABLE_SANITIZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "SQUARESTAR_ENABLE_SANITIZERS requires GCC or Clang")
    endif()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../cmake")
include(SquareStarWarnings)
find_package(Threads REQUIRED)

if(NOT TARGET squarestar_yyjson)
    enable_language(C)
    add_library(squarestar_yyjson STATIC
        "${CMAKE_CURRENT_LIST_DIR}/../third_party/yyjson/yyjson.c")
    target_include_directories(squarestar_yyjson SYSTEM PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/../third_party/yyjson")
endif()

# Windows application builds reuse the exact component archive linked by
# SquareStar. Standalone/non-Windows test builds compile the portable subset
# once, instead of recompiling the same production .cpp files per test target.
if(TARGET squarestar_components)
    set(SQUARESTAR_TEST_COMPONENT_TARGET squarestar_components)
else()
    include("${CMAKE_CURRENT_LIST_DIR}/../cmake/SquareStarSourceGraph.cmake")
    add_library(squarestar_portable_components STATIC
        ${SQUARESTAR_PORTABLE_COMPILED_SOURCES})
    target_compile_features(squarestar_portable_components PUBLIC cxx_std_20)
    target_include_directories(squarestar_portable_components PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/../src")
    target_include_directories(squarestar_portable_components SYSTEM PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/../third_party/imgui"
        "${CMAKE_CURRENT_LIST_DIR}/../third_party/yyjson")
    target_link_libraries(squarestar_portable_components PUBLIC
        squarestar_yyjson Threads::Threads)
    if(WIN32)
        target_link_libraries(squarestar_portable_components PUBLIC
            advapi32
            crypt32
            dwmapi
            ole32
            winmm
            shell32)
    endif()
    squarestar_enable_cxx_warnings(squarestar_portable_components)
    set(SQUARESTAR_TEST_COMPONENT_TARGET squarestar_portable_components)
endif()

function(squarestar_add_component_test TARGET SOURCE)
    add_executable(${TARGET} ${SOURCE})
    target_compile_features(${TARGET} PRIVATE cxx_std_20)
    target_include_directories(${TARGET} PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/../src")
    target_include_directories(${TARGET} SYSTEM PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/../third_party/imgui"
        "${CMAKE_CURRENT_LIST_DIR}/../third_party/yyjson")
    target_link_libraries(${TARGET} PRIVATE
        ${SQUARESTAR_TEST_COMPONENT_TARGET}
        squarestar_yyjson
        Threads::Threads)
    squarestar_enable_cxx_warnings(${TARGET})
    add_test(NAME ${TARGET} COMMAND ${TARGET})
    # Unit/integration tests in this suite are expected to finish in seconds.
    # A timeout keeps a background-thread regression from blocking validation.
    set_tests_properties(${TARGET} PROPERTIES TIMEOUT 30)
endfunction()

squarestar_add_component_test(squarestar_domain_tests domain_tests.cpp)
target_sources(squarestar_domain_tests PRIVATE test_fixtures.cpp)
squarestar_add_component_test(squarestar_chart_export_tests chart_export_tests.cpp)
squarestar_add_component_test(
    squarestar_stock_data_merge_tests stock_data_merge_tests.cpp)
squarestar_add_component_test(squarestar_time_axis_ticks_tests time_axis_ticks_tests.cpp)
squarestar_add_component_test(squarestar_lite_tab_policy_tests lite_tab_policy_tests.cpp)
squarestar_add_component_test(
    squarestar_contextual_keybind_policy_tests contextual_keybind_policy_tests.cpp)
squarestar_add_component_test(
    squarestar_window_centering_policy_tests window_centering_policy_tests.cpp)
squarestar_add_component_test(
    squarestar_ui_interaction_policy_tests ui_interaction_policy_tests.cpp)
squarestar_add_component_test(squarestar_screener_payload_tests screener_payload_tests.cpp)
squarestar_add_component_test(squarestar_screener_service_tests screener_service_tests.cpp)
squarestar_add_component_test(
    squarestar_provider_payload_parser_tests provider_payload_parser_tests.cpp)
squarestar_add_component_test(squarestar_url_policy_tests url_policy_tests.cpp)
squarestar_add_component_test(
    squarestar_secret_protection_tests secret_protection_tests.cpp)
squarestar_add_component_test(
    squarestar_config_persistence_tests config_persistence_tests.cpp)
set_tests_properties(squarestar_config_persistence_tests PROPERTIES
    RUN_SERIAL TRUE)
squarestar_add_component_test(
    squarestar_config_state_codec_tests config_state_codec_tests.cpp)
squarestar_add_component_test(
    squarestar_application_paths_tests application_paths_tests.cpp)
squarestar_add_component_test(
    squarestar_gui_layout_persistence_tests gui_layout_persistence_tests.cpp)
squarestar_add_component_test(
    squarestar_native_notification_timing_tests native_notification_timing_tests.cpp)
squarestar_add_component_test(
    squarestar_main_loop_signal_tests main_loop_signal_tests.cpp)
squarestar_add_component_test(
    squarestar_notification_channel_tests notification_channel_tests.cpp)
squarestar_add_component_test(
    squarestar_executor_lifecycle_tests executor_lifecycle_tests.cpp)
squarestar_add_component_test(
    squarestar_network_executor_shutdown_tests network_executor_shutdown_tests.cpp)
squarestar_add_component_test(squarestar_alert_service_tests alert_service_tests.cpp)
squarestar_add_component_test(squarestar_screener_cache_tests screener_cache_tests.cpp)
squarestar_add_component_test(
    squarestar_screener_cache_persistence_tests screener_cache_persistence_tests.cpp)
squarestar_add_component_test(
    squarestar_screener_controller_tests screener_controller_tests.cpp)
set_tests_properties(squarestar_screener_controller_tests PROPERTIES
    RUN_SERIAL TRUE)
squarestar_add_component_test(
    squarestar_stock_data_service_tests stock_data_service_tests.cpp)
squarestar_add_component_test(
    squarestar_news_reader_policy_tests news_reader_policy_tests.cpp)
squarestar_add_component_test(squarestar_symbol_search_service_tests symbol_search_service_tests.cpp)
squarestar_add_component_test(squarestar_token_bucket_tests token_bucket_tests.cpp)

if(WIN32)
    squarestar_add_component_test(
        squarestar_windows_config_save_retry_tests windows_config_save_retry_tests.cpp)
    set_tests_properties(squarestar_windows_config_save_retry_tests PROPERTIES
        TIMEOUT 30
        RUN_SERIAL TRUE)
    squarestar_add_component_test(
        squarestar_windows_state_integration_tests windows_state_integration_tests.cpp)
    set_tests_properties(squarestar_windows_state_integration_tests PROPERTIES
        TIMEOUT 30
        RUN_SERIAL TRUE)
endif()


if(SQUARESTAR_BUILD_FUZZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "SQUARESTAR_BUILD_FUZZERS requires Clang/libFuzzer")
    endif()

    # Keep the code under test inside each fuzz target so sanitizer coverage is
    # not lost behind a precompiled, non-instrumented component archive.
    add_executable(squarestar_fuzz_provider_payload
        fuzz_provider_payload.cpp
        "${CMAKE_CURRENT_LIST_DIR}/../src/services/provider_payload_parser.cpp")
    target_include_directories(squarestar_fuzz_provider_payload PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/../third_party/yyjson")
    target_link_libraries(squarestar_fuzz_provider_payload PRIVATE squarestar_yyjson)
    foreach(SQUARESTAR_FUZZ_TARGET
            squarestar_fuzz_provider_payload)
        target_compile_features(${SQUARESTAR_FUZZ_TARGET} PRIVATE cxx_std_20)
        target_include_directories(${SQUARESTAR_FUZZ_TARGET} PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/../src")
        target_compile_options(${SQUARESTAR_FUZZ_TARGET} PRIVATE
            -fsanitize=fuzzer,address,undefined
            -fno-omit-frame-pointer)
        target_link_options(${SQUARESTAR_FUZZ_TARGET} PRIVATE
            -fsanitize=fuzzer,address,undefined)
    endforeach()
endif()
