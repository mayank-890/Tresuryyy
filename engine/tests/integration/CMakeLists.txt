add_executable(trading_engine_pipeline_tests engine_pipeline_tests.cpp
                                            ${CMAKE_CURRENT_SOURCE_DIR}/../../src/apps/trader/engine_pipeline.cpp
                                            ${CMAKE_CURRENT_SOURCE_DIR}/../../src/apps/trader/broker_execution.cpp)
target_include_directories(
  trading_engine_pipeline_tests
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../src/apps/trader"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src/strategy")
target_link_libraries(
  trading_engine_pipeline_tests
  PRIVATE trading_strategy trading_risk trading_orders trading_marketdata trading_core GTest::gtest_main)
add_test(NAME trading_engine_pipeline_tests COMMAND trading_engine_pipeline_tests)

add_executable(trading_paper_pipeline_tests paper_pipeline_tests.cpp
                                            ${CMAKE_CURRENT_SOURCE_DIR}/../../src/apps/trader/engine_pipeline.cpp
                                            ${CMAKE_CURRENT_SOURCE_DIR}/../../src/apps/trader/broker_execution.cpp)
target_compile_definitions(trading_paper_pipeline_tests PRIVATE TRADING_REPO_ROOT="${CMAKE_SOURCE_DIR}")
target_include_directories(
  trading_paper_pipeline_tests
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../src/apps/trader"
          "${CMAKE_CURRENT_SOURCE_DIR}/../../src/strategy")
target_link_libraries(
  trading_paper_pipeline_tests
  PRIVATE trading_profile trading_strategy trading_risk trading_orders trading_marketdata
          trading_broker_mock trading_core GTest::gtest_main)
add_test(NAME trading_paper_pipeline_tests COMMAND trading_paper_pipeline_tests)
set_tests_properties(
  trading_paper_pipeline_tests
  PROPERTIES ENVIRONMENT "TRADING_SECRET_TRADING_PG_PROJECTOR=ci-test-secret-not-real")
