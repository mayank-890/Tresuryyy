cmake_minimum_required(VERSION 3.25)

project(trading-system VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TRADING_ENABLE_GROWW "Build Groww broker adapter" OFF)
option(TRADING_BUILD_BINDINGS "Build pybind11 research bindings" OFF)
option(TRADING_BUILD_WATCHDOG "Build watchdog executable" ON)
option(TRADING_ENABLE_LIVE_PROFILE "Compile live profile startup path" OFF)

if(TRADING_ENABLE_GROWW)
  message(FATAL_ERROR "TRADING_ENABLE_GROWW must remain OFF until PRD gates G-LIVE-1 and G-LIVE-2 are cleared.")
endif()

include(CTest)
enable_testing()

add_subdirectory(engine)
