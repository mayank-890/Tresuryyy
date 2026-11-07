add_executable(trading_engine_pipeline_tests engine_pipeline_tests.cpp
                                            ${CMAKE_CURRENT_SOURCE_DIR}/../../src/apps/trader/engine_pipeline.cpp)
target_include_directories(
  trading_engine_pipeline_tests
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../src/apps/trader"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src/strategy")
target_link_libraries(
  trading_engine_pipeline_tests
  PRIVATE trading_strategy trading_risk trading_orders trading_marketdata trading_core GTest::gtest_main)
add_test(NAME trading_engine_pipeline_tests COMMAND trading_engine_pipeline_tests)
