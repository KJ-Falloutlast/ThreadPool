cmake_minimum_required(VERSION 3.2)
project(ThreadPool_change)

set(CMAKE_CXX_STANDARD 17)#强制使用17标准
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}-pthread")

include_directories(include)

add_executable(threadpool  src/main.cpp)
target_link_libraries(threadpool pthread)
