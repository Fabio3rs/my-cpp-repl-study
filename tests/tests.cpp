#include "repl.hpp"
#include <any>
#include <filesystem>
#include <gtest/gtest.h>
#include <string>

class ReplTests : public ::testing::Test {
  protected:
    void SetUp() override {
        // Initialize the REPL environment before each test
        // create temporary dir and change to it
        namespace fs = std::filesystem;
        fs::path temp_dir = fs::temp_directory_path();

        char tmpfilename[] = "/repl_test_XXXXXX";
        tmpnam(tmpfilename); // Ensure a unique temporary file name
        fs::path unique_dir =
            fs::temp_directory_path() / fs::path(std::string(tmpfilename));

        oldPath = fs::current_path();

        fs::create_directory(unique_dir);
        fs::current_path(unique_dir);

        // Initialize the REPL
        initRepl();
    }

    void TearDown() override {
        // Clean up after each test
        namespace fs = std::filesystem;
        fs::path temp_path = fs::current_path();

        fs::current_path(oldPath); // Restore old path
        fs::remove_all(temp_path); // Remove the temporary directory
    }

    std::filesystem::path oldPath;
};

// Test: Basic assignment and retrieval
TEST_F(ReplTests, BasicAssignment) {
    std::string_view cmd = "int x = 42;";
    ASSERT_TRUE(extExecRepl(cmd));
    ASSERT_EQ(42, std::any_cast<int>(getResultRepl("x")));
}

// Test: Multiple assignments and retrievals
TEST_F(ReplTests, MultipleAssignments) {
    ASSERT_TRUE(extExecRepl("int a = 1; int b = 2;"));
    ASSERT_EQ(1, std::any_cast<int>(getResultRepl("a")));
    ASSERT_EQ(2, std::any_cast<int>(getResultRepl("b")));
}

// Test: Assignment with expression
TEST_F(ReplTests, AssignmentWithExpression) {
    ASSERT_TRUE(extExecRepl("int y = 10 + 5;"));
    ASSERT_EQ(15, std::any_cast<int>(getResultRepl("y")));
}

// Test: Reassignment of variable
TEST_F(ReplTests, Reassignment) {
    ASSERT_TRUE(extExecRepl("int z = 7; void exec() { z = 9; }"));
    ASSERT_EQ(9, std::any_cast<int>(getResultRepl("z")));
}

// Test: String assignment
TEST_F(ReplTests, StringAssignment) {
    ASSERT_TRUE(extExecRepl("std::string s = \"hello\";"));
    ASSERT_EQ(std::string("hello"),
              std::any_cast<std::string>(getResultRepl("s")));
}

// Test: Invalid command returns false
TEST_F(ReplTests, InvalidCommand) {
    std::string_view cmd = "int = ;";
    ASSERT_TRUE(extExecRepl(cmd));
}
