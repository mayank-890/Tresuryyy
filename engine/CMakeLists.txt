find_package(GTest CONFIG REQUIRED)

function(trading_add_module target source)
  add_library(${target} STATIC ${source})
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

trading_add_module(trading_marketdata src/marketdata/module.cpp)
target_link_libraries(trading_marketdata PUBLIC trading_core)
trading_add_smoke_test(trading_marketdata)

trading_add_module(trading_strategy src/strategy/module.cpp)
target_link_libraries(trading_strategy PUBLIC trading_core trading_marketdata)
trading_add_smoke_test(trading_strategy)

trading_add_module(trading_risk src/risk/module.cpp)
target_link_libraries(trading_risk PUBLIC trading_core trading_strategy)
trading_add_smoke_test(trading_risk)

trading_add_module(trading_orders src/orders/module.cpp)
target_link_libraries(trading_orders PUBLIC trading_core)
trading_add_smoke_test(trading_orders)

trading_add_module(trading_journal src/journal/module.cpp)
target_link_libraries(trading_journal PUBLIC trading_core)
trading_add_smoke_test(trading_journal)

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

add_executable(trader src/apps/trader/main.cpp)
target_compile_definitions(trader PRIVATE TRADING_SYSTEM_VERSION="${CMAKE_PROJECT_VERSION}")
target_link_libraries(
  trader
  PRIVATE trading_marketdata
          trading_strategy
          trading_risk
          trading_orders
          trading_journal
          trading_broker_common
          trading_broker_delta
          trading_broker_mock
          trading_control)
add_test(NAME trader_version COMMAND trader)
set_tests_properties(trader_version PROPERTIES PASS_REGULAR_EXPRESSION "trading-engine 0\\.1\\.0")

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
  message(FATAL_ERROR "TRADING_BUILD_BINDINGS is not implemented in T-0003.")
endif()
