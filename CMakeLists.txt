cmake_minimum_required(VERSION 3.2)
project(concurrent-deque)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_library(Catch INTERFACE)
target_include_directories(Catch INTERFACE tests)

include_directories(include)
include_directories(tests)

add_executable(deque_test tests/deque_test.cpp)
target_link_libraries(deque_test Threads::Threads)
target_link_libraries(deque_test Catch)
