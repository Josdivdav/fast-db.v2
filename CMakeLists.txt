cmake_minimum_required(VERSION 3.16)
project(fastdb CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

find_package(Threads REQUIRED)

set(LIB_SOURCES
    src/crypto.cpp
    src/keygen.cpp
    src/user.cpp
    src/database.cpp
    src/server.cpp
    src/client.cpp
)

add_library(fastdb_lib STATIC ${LIB_SOURCES})
target_link_libraries(fastdb_lib PUBLIC Threads::Threads)

# Server executable
add_executable(fastdb_server src/main.cpp)
target_link_libraries(fastdb_server PRIVATE fastdb_lib)

# Client example executable
add_executable(fastdb_client examples/client.cpp)
target_link_libraries(fastdb_client PRIVATE fastdb_lib)

# Unit test executable
add_executable(test_unit tests/test_unit.cpp)
target_link_libraries(test_unit PRIVATE fastdb_lib)

# Integration test executable
add_executable(test_integration tests/test_integration.cpp)
target_link_libraries(test_integration PRIVATE fastdb_lib)

enable_testing()
add_test(NAME UnitTests COMMAND test_unit)
add_test(NAME IntegrationTests COMMAND test_integration)
