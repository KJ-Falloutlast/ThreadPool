cmake_minimum_required(VERSION 3.2)
project(ThreadPool)

set(CMAKE_CXX_STANDARD 17)#强制使用17标准
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}-pthread")

include_directories(include)

add_executable(threadpool src/threadpool.cpp src/main.cpp)
target_link_libraries(threadpool pthread)

add_executable(test_any src/test_any.cpp)
target_link_libraries(test_any pthread)