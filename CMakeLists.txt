cmake_minimum_required(VERSION 3.20) # easyRTMP compatibility

project(Squig)

# only works with Cmake-based libs
# --
# @Jan13: Making changes to the easyRTMP lib. 
# Probably better to work with a fork of the original?
include(FetchContent)
FetchContent_Declare(
  easyrtmp
  GIT_REPOSITORY https://github.com/AlexanderBabansky/EasyRTMP.git
  GIT_TAG        7acf54cc7cc97f1bd71707b28d399ea8225903a6 # with conan support. Cached, so delete build folder if changing
)

# Defines
# easyrtmp_SOURCE_DIR: where source was downloaded (_deps/..-src)
# easyrtmp_BINARY_DIR: where source is built (_deps/..-build)
FetchContent_MakeAvailable(easyrtmp)
message(STATUS "easyrtmp source dir: ${easyrtmp_SOURCE_DIR}")

add_executable(squig)

target_sources(squig
  PRIVATE
    src/main.cpp
)

target_include_directories(squig
    PRIVATE
        deps/easyrtmp/include
)

target_include_directories(squig
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(squig PRIVATE easyrtmp) # lib name: from the library CMakeLists