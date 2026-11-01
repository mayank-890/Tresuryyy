find_package(GTest CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

function(trading_add_module target)
  add_library(${target} STATIC ${ARGN})
  target_include_directories(${target} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
  target_compile_features(${target} PUBLIC cxx_std_20)
endfunction()

function(trading_add_smoke_test target)
  set(test_target "${target}_tests")
  add_executable(${test_target} "tests/unit/${target}/smoke_test.cpp")
  target_link_libraries(${test_target} PRIVATE ${target} GTest::gtest_main)
  add_test(NAME ${test_target} COMMAND ${test_target})
endfunction()

trading_add_module(trading_core src/core/module.cpp)
trading_add_smoke_test(trading_core)
add_subdirectory(tests/unit/core)

trading_add_module(trading_marketdata src/marketdata/module.cpp
                   src/marketdata/delta_normalize.cpp)
target_link_libraries(trading_marketdata PUBLIC trading_core
                                                nlohmann_json::nlohmann_json)
trading_add_smoke_test(trading_marketdata)
add_executable(trading_marketdata_delta_tests
               tests/unit/trading_marketdata/delta_normalize_tests.cpp)
target_link_libraries(trading_marketdata_delta_tests PRIVATE trading_marketdata
                                                             GTest::gtest_main)
add_test(NAME trading_marketdata_delta_tests
         COMMAND trading_marketdata_delta_tests)

trading_add_module(trading_strategy src/strategy/module.cpp src/strategy/registry.cpp
                   src/strategy/strategies/flat_echo/strategy.cpp)
target_include_directories(trading_strategy PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/strategy")
target_link_libraries(trading_strategy PUBLIC trading_core trading_marketdata)
trading_add_smoke_test(trading_strategy)
add_executable(trading_strategy_interface_tests
               src/strategy/strategies/flat_echo/tests/flat_echo_tests.cpp)
target_include_directories(trading_strategy_interface_tests PRIVATE
                           "${CMAKE_CURRENT_SOURCE_DIR}/src/strategy")
target_compile_definitions(trading_strategy_interface_tests PRIVATE
                           TRADING_REPO_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(trading_strategy_interface_tests PRIVATE trading_strategy
                                                             GTest::gtest_main)
add_test(NAME trading_strategy_interface_tests COMMAND trading_strategy_interface_tests)

trading_add_module(trading_risk src/risk/module.cpp)
target_link_libraries(trading_risk PUBLIC trading_core trading_strategy)
trading_add_smoke_test(trading_risk)

trading_add_module(trading_orders src/orders/module.cpp)
target_link_libraries(trading_orders PUBLIC trading_core)
trading_add_smoke_test(trading_orders)

trading_add_module(trading_journal src/journal/module.cpp src/journal/writer.cpp
                   src/journal/segment_footer.cpp src/journal/rotation.cpp)
target_link_libraries(trading_journal PUBLIC trading_core)
trading_add_smoke_test(trading_journal)
add_executable(trading_journal_writer_tests
               tests/unit/trading_journal/writer_tests.cpp)
target_link_libraries(trading_journal_writer_tests PRIVATE trading_journal
                                                           GTest::gtest_main)
add_test(NAME trading_journal_writer_tests COMMAND trading_journal_writer_tests)
add_executable(trading_journal_rotation_tests
               tests/unit/trading_journal/rotation_tests.cpp)
target_link_libraries(trading_journal_rotation_tests PRIVATE trading_journal
                                                             GTest::gtest_main)
add_test(NAME trading_journal_rotation_tests COMMAND trading_journal_rotation_tests)
add_executable(journal_retention "${CMAKE_SOURCE_DIR}/tools/journal_retention/main.cpp")
target_link_libraries(journal_retention PRIVATE trading_journal)
add_test(
  NAME journal_retention_cli
  COMMAND journal_retention status --root "${CMAKE_SOURCE_DIR}/tests/fixtures/compat/v1")
set_tests_properties(journal_retention_cli PROPERTIES PASS_REGULAR_EXPRESSION "segments=")

trading_add_module(trading_broker_common src/broker/common/module.cpp)
target_link_libraries(trading_broker_common PUBLIC trading_core)
trading_add_smoke_test(trading_broker_common)

trading_add_module(trading_broker_delta src/broker/delta/module.cpp)
target_link_libraries(trading_broker_delta PUBLIC trading_broker_common)
trading_add_smoke_test(trading_broker_delta)

trading_add_module(trading_broker_mock src/broker/mock/module.cpp)
target_link_libraries(trading_broker_mock PUBLIC trading_broker_common)
trading_add_smoke_test(trading_broker_mock)

trading_add_module(trading_control src/control/module.cpp)
target_link_libraries(trading_control PUBLIC trading_core trading_journal)
trading_add_smoke_test(trading_control)

add_library(trading_profile STATIC
            src/apps/trader/toml_json.cpp
            src/apps/trader/content_hash.cpp
            src/apps/trader/schema_validator.cpp
            src/apps/trader/secret_store.cpp
            src/apps/trader/promotion_verifier.cpp
            src/apps/trader/profile_startup.cpp
            src/calendar/stub.cpp)
target_include_directories(
  trading_profile
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/apps/trader"
         "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(
  trading_profile
  PUBLIC trading_core tomlplusplus::tomlplusplus nlohmann_json::nlohmann_json
         OpenSSL::Crypto)
target_compile_features(trading_profile PUBLIC cxx_std_20)

add_executable(trader src/apps/trader/main.cpp)
target_compile_definitions(trader PRIVATE TRADING_SYSTEM_VERSION="${CMAKE_PROJECT_VERSION}")
target_link_libraries(trader PRIVATE trading_profile)
add_test(NAME trader_version COMMAND trader --profile paper --config-root
                                             "${CMAKE_SOURCE_DIR}/config" --smoke)
set_tests_properties(
  trader_version
  PROPERTIES
    ENVIRONMENT "TRADING_SECRET_TRADING_PG_PROJECTOR=ci-test-secret-not-real"
    PASS_REGULAR_EXPRESSION "RUNNING \\(no trading logic\\)")

add_subdirectory(tests/unit/profile)

if(TRADING_BUILD_WATCHDOG)
  add_executable(watchdog src/apps/watchdog/main.cpp)
  target_compile_definitions(watchdog PRIVATE TRADING_SYSTEM_VERSION="${CMAKE_PROJECT_VERSION}")
  target_link_libraries(
    watchdog
    PRIVATE trading_broker_common trading_broker_delta trading_control trading_journal)
  add_test(NAME watchdog_version COMMAND watchdog)
  set_tests_properties(watchdog_version PROPERTIES PASS_REGULAR_EXPRESSION "trading-watchdog 0\\.1\\.0")
endif()

if(TRADING_BUILD_BINDINGS)
  # ADR-0005 accepts pybind11. The vcpkg port depends on vcpkg's python3, which
  # is a second interpreter and did not build here (Q-24). Fetch the same
  # 3.1.0 tag and SHA512 the port records, and bind the Python passed in
  # Python_EXECUTABLE. Default presets keep TRADING_BUILD_BINDINGS=OFF.
  include(FetchContent)
  find_package(Python COMPONENTS Interpreter Development.Module REQUIRED)
  set(PYBIND11_TEST OFF CACHE BOOL "" FORCE)
  set(PYBIND11_NOPYTHON OFF CACHE BOOL "" FORCE)
  set(PYBIND11_FINDPYTHON ON CACHE BOOL "" FORCE)
  FetchContent_Declare(
    pybind11
    URL https://github.com/pybind/pybind11/archive/v3.1.0.tar.gz
    URL_HASH
      SHA512=a79f7ed52d23c27325f37c50926bd0d0561aafefcf847219ed1378ba0a5a94479e15943e4da49117606695f40762b21ddcc8c32a6b8be61afc6de503c635af74
  )
  FetchContent_MakeAvailable(pybind11)
  set_property(TARGET trading_core trading_marketdata trading_strategy trading_risk
               PROPERTY POSITION_INDEPENDENT_CODE ON)
  pybind11_add_module(trading_bindings bindings/python/trading_bindings.cpp)
  target_include_directories(trading_bindings PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/strategy")
  target_link_libraries(trading_bindings PRIVATE trading_strategy trading_risk)
endif()
