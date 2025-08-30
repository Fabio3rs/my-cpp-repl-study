
# ==============================================================================
# CPPREPL Tests Configuration
# ==============================================================================

if(GTest_FOUND)
    message(STATUS "✓ Configuring comprehensive test suite with GTest")
    include(GoogleTest)
  
    # ==============================================================================
    # CORE TEST EXECUTABLES
    # ==============================================================================
  
    # Integration tests (main system functionality)
    add_executable(tests tests.cpp)
    target_include_directories(tests PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(tests PRIVATE cpprepl_lib GTest::GTest GTest::Main segvcatch)
    gtest_discover_tests(tests)
  
    # ==============================================================================
    # COMPONENT-SPECIFIC UNIT TESTS
    # ==============================================================================
  
    # Compiler Service unit tests
    add_executable(compiler_tests
        compiler/test_compiler_service.cpp
        test_helpers/temp_directory_fixture.hpp
        test_helpers/mock_build_settings.hpp
    )
    target_include_directories(compiler_tests PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(compiler_tests PRIVATE cpprepl_lib GTest::GTest GTest::Main segvcatch)
    gtest_discover_tests(compiler_tests)
  
    # AST Context unit tests
    add_executable(analysis_tests
        analysis/test_ast_context.cpp
        analysis/test_static_duration.cpp
        test_helpers/temp_directory_fixture.hpp
    )
    target_include_directories(analysis_tests PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(analysis_tests PRIVATE cpprepl_lib GTest::GTest GTest::Main segvcatch)
    gtest_discover_tests(analysis_tests)
  
    # Utility functions unit tests
    add_executable(utility_tests
        utility/test_library_introspection.cpp
        test_helpers/temp_directory_fixture.hpp
    )
    target_include_directories(utility_tests PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(utility_tests PRIVATE cpprepl_lib GTest::GTest GTest::Main segvcatch)
    gtest_discover_tests(utility_tests)

    # ==============================================================================
    # CONDITIONAL FEATURE TESTS
    # ==============================================================================
  
    # Completion system tests (only if libclang is available)
    if(Clang_FOUND)
        message(STATUS "  ✓ Adding completion tests (Clang available)")
      
        add_executable(completion_tests
            completion/test_clang_completion.cpp
            completion/test_readline_integration.cpp
            test_helpers/temp_directory_fixture.hpp
        )
        target_include_directories(completion_tests PRIVATE ${CMAKE_SOURCE_DIR})
        target_link_libraries(completion_tests PRIVATE cpprepl_lib GTest::GTest GTest::Main segvcatch)
        gtest_discover_tests(completion_tests)

        # Minimal Clang integration example/test
        add_executable(minimal_clang_example
            minimal_clang_example.cpp
        )
        target_link_libraries(minimal_clang_example PRIVATE cpprepl_lib)
    else()
        message(STATUS "  ✗ Skipping completion tests (libclang not available)")
    endif()
  
    # ==============================================================================
    # TEST SUMMARY
    # ==============================================================================
  
    message(STATUS "")
    message(STATUS "Test suite configuration:")
    message(STATUS "  - Integration tests: tests")
    message(STATUS "  - Compiler tests: compiler_tests")
    message(STATUS "  - Analysis tests: analysis_tests")
    message(STATUS "  - Utility tests: utility_tests")
    if(Clang_FOUND)
        message(STATUS "  - Completion tests: completion_tests")
        message(STATUS "  - Clang example: minimal_clang_example")
    endif()
    message(STATUS "")

else()
    message(STATUS "✗ GTest not found - testing disabled")
    message(STATUS "  Install GTest to enable comprehensive testing")
endif()
