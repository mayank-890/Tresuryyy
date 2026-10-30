add_executable(profile_tests profile_tests.cpp calendar_instrument_tests.cpp
                                   instrument_enrich_tests.cpp)
target_link_libraries(profile_tests PRIVATE trading_profile GTest::gtest_main)
target_include_directories(
  profile_tests
  PRIVATE "${CMAKE_SOURCE_DIR}/engine/include"
          "${CMAKE_SOURCE_DIR}/engine/src/calendar"
          "${CMAKE_SOURCE_DIR}/engine/src/instruments")
target_compile_definitions(profile_tests PRIVATE TRADING_REPO_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME profile_tests COMMAND profile_tests)

add_executable(instrument_enrich "${CMAKE_SOURCE_DIR}/tools/instrument_enrich/main.cpp")
target_link_libraries(instrument_enrich PRIVATE trading_profile)
target_include_directories(
  instrument_enrich
  PRIVATE "${CMAKE_SOURCE_DIR}/engine/include"
          "${CMAKE_SOURCE_DIR}/engine/src/calendar"
          "${CMAKE_SOURCE_DIR}/engine/src/instruments")
target_compile_definitions(instrument_enrich PRIVATE TRADING_REPO_ROOT="${CMAKE_SOURCE_DIR}")
add_test(
  NAME instrument_enrich_cli
  COMMAND instrument_enrich
          --seed "${CMAKE_SOURCE_DIR}/config/instruments/delta_crypto.toml"
          --patch "${CMAKE_SOURCE_DIR}/config/instruments/delta_recording_ids.toml"
          --symbols "${CMAKE_SOURCE_DIR}/tests/fixtures/instruments/journal_symbols.txt")
set_tests_properties(instrument_enrich_cli PROPERTIES PASS_REGULAR_EXPRESSION "mapped=2")
