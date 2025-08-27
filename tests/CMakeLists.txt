
if (GTest_FOUND)
    message(STATUS "GTest found")
    include(GoogleTest)
    
    # Integration tests (existing)
    add_executable(tests tests.cpp)
    target_include_directories(tests PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(tests cpprepl_lib GTest::GTest GTest::Main segvcatch)
    gtest_discover_tests(tests)
    
    # Unit tests for CompilerService
    add_executable(compiler_tests 
        compiler/test_compiler_service.cpp
        test_helpers/temp_directory_fixture.hpp
        test_helpers/mock_build_settings.hpp
    )
    target_include_directories(compiler_tests PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(compiler_tests cpprepl_lib GTest::GTest GTest::Main segvcatch)
    gtest_discover_tests(compiler_tests)
    
    # Unit tests for AstContext
    add_executable(analysis_tests 
        analysis/test_ast_context.cpp
        analysis/test_static_duration.cpp
        test_helpers/temp_directory_fixture.hpp
    )
    target_include_directories(analysis_tests PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(analysis_tests cpprepl_lib GTest::GTest GTest::Main segvcatch)
    gtest_discover_tests(analysis_tests)
    
    # Unit tests for Utility functions
    add_executable(utility_tests 
        utility/test_library_introspection.cpp
        test_helpers/temp_directory_fixture.hpp
    )
    target_include_directories(utility_tests PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(utility_tests cpprepl_lib GTest::GTest GTest::Main segvcatch)
    gtest_discover_tests(utility_tests)

else()
    message(STATUS "GTest not found")
endif()
