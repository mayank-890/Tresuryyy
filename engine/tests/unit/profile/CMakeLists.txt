add_executable(profile_tests profile_tests.cpp calendar_instrument_tests.cpp)
target_link_libraries(profile_tests PRIVATE trading_profile GTest::gtest_main)
target_include_directories(
  profile_tests
  PRIVATE "${CMAKE_SOURCE_DIR}/engine/include"
          "${CMAKE_SOURCE_DIR}/engine/src/calendar"
          "${CMAKE_SOURCE_DIR}/engine/src/instruments")
target_compile_definitions(profile_tests PRIVATE TRADING_REPO_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME profile_tests COMMAND profile_tests)
