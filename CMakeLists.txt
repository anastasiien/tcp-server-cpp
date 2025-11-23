cmake_minimum_required(VERSION 3.10)
project(ACOS_TCP_CHAT CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_executable(server server.cpp)
target_link_libraries(server PUBLIC pthread)

add_executable(client client.cpp)
target_link_libraries(client PUBLIC pthread)