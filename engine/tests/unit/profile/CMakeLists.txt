add_executable(profile_tests profile_tests.cpp)
target_link_libraries(profile_tests PRIVATE trading_profile GTest::gtest_main)
target_compile_definitions(profile_tests PRIVATE TRADING_REPO_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME profile_tests COMMAND profile_tests)
