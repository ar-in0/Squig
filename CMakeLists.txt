cmake_minimum_required(VERSION 3.20) # easyRTMP compatibility

project(Squig)

# ffmpeg libs do note have a .cmake, so we need 
# pkgconfig to find them
find_package(PkgConfig REQUIRED)
pkg_check_modules(AVCODEC REQUIRED libavcodec)
pkg_check_modules(AVUTIL REQUIRED libavutil)

find_package( OpenCV REQUIRED )

add_executable(squig)
target_sources(squig
  PRIVATE
  src/main.cpp
  src/streamdecoder.cpp
)

# easyrtmp cmake has PUBLIC on its include
# directory, so any target linking with it
# will have default access to it.
add_subdirectory(deps/easyrtmp)
target_include_directories(squig
  PRIVATE
  # deps/easyrtmp/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include # for rtmp_server.h
  ${AVCODEC_INCLUDE_DIRS}
  ${AVUTIL_INCLUDE_DIRS}
)

target_link_libraries(squig
  PRIVATE
  easyrtmp
  swscale
  ${OpenCV_LIBS}
  ${AVCODEC_LIBRARIES}
  ${AVUTIL_LIBRARIES}
  ) # lib name: from the library CMakeLists
