cmake_minimum_required(VERSION 3.5)
project(SGCL_Tests LANGUAGES CXX)

enable_testing()

file(GLOB TEST_SOURCES "*.cpp")

# static_init_test builds a tracked object before main(), so it gets its own process.
list(REMOVE_ITEM TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/static_init.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/static_init_factory.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/external_state.cpp"
)

add_executable(tests ${TEST_SOURCES} external_state.cpp types.h)
target_link_libraries(tests gtest gtest_main sgcl)
target_include_directories(tests PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME tests COMMAND tests)

add_executable(static_init_test
    static_init.cpp
    static_init_factory.cpp
    static_init.h
    external_state.cpp
)
target_link_libraries(static_init_test gtest gtest_main sgcl)
target_include_directories(static_init_test PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME static_init_test COMMAND static_init_test)

add_subdirectory(multi_image)
