
if (GTest_FOUND)
    message(STATUS "GTest found")
    include(GoogleTest)
    add_executable(tests tests.cpp)
    target_include_directories(tests PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(tests cpprepl_lib GTest::GTest GTest::Main segvcatch)

    gtest_discover_tests(tests)
else()
    message(STATUS "GTest not found")
endif()
