cmake_minimum_required(VERSION 3.10)
project(RTMPServer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

add_library(rtmp SHARED
    src/rtmp_server.cpp
    src/rtmp_capi.cpp
)

target_include_directories(rtmp PUBLIC 
    BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include;
    INSTALL_INTERFACE:include;
)

set_target_properties(rtmp PROPERTIES
    PUBLIC_HEADER include/rtmp_capi.h
)

add_executable(rtmp_server_cpp server/cpp/main.cpp)
target_link_libraries(rtmp_server_cpp rtmp)

install(TARGETS rtmp 
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    PUBLIC_HEADER DESTINATION include
)

install(TARGETS rtmp_server_cpp DESTINATION bin)
