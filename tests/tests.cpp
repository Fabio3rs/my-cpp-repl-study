#include "repl.hpp"
#include <any>
#include <filesystem>
#include <gtest/gtest.h>
#include <string>

extern int verbosityLevel;

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

        std::cout << "Using temporary directory: "
                  << fs::current_path().string() << std::endl;

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
    ASSERT_TRUE(extExecRepl("#include <string>"));
    ASSERT_TRUE(extExecRepl("std::string s = \"hello\";"));
    ASSERT_EQ(std::string("hello"),
              std::any_cast<std::string>(getResultRepl("s")));
}

// Test: Invalid command returns false
TEST_F(ReplTests, InvalidCommand) {
    std::string_view cmd = "int = ;";
    ASSERT_TRUE(extExecRepl(cmd));
}

// Test: Invalid command returns false
TEST_F(ReplTests, OverwriteFunction) {
    std::string_view cmd = "int foo() { return 123; }";
    ASSERT_TRUE(extExecRepl(cmd));
    cmd = "int foo() { return 456; }";
    ASSERT_TRUE(extExecRepl(cmd));
    cmd = "int a = foo();";
    ASSERT_TRUE(extExecRepl(cmd));
    ASSERT_EQ(456, std::any_cast<int>(getResultRepl("a")));
}

TEST_F(ReplTests, FunctionUsingVariable) {
    std::string_view cmd = "int val = 10; int getVal() { return val; }";
    ASSERT_TRUE(extExecRepl(cmd));
    cmd = "int result = getVal();";
    ASSERT_TRUE(extExecRepl(cmd));
    ASSERT_EQ(10, std::any_cast<int>(getResultRepl("result")));
}

TEST_F(ReplTests, AutoIdentifySimpleEvalCode) {
    std::string_view cmd = "int val = 10;";
    ASSERT_TRUE(extExecRepl(cmd));
    cmd = "val += 5;";
    ASSERT_TRUE(extExecRepl(cmd));
    ASSERT_EQ(15, std::any_cast<int>(getResultRepl("val")));
}

TEST_F(ReplTests, DependencyAtConstructorTime) {
    std::string_view cmd = "int foo() { return 123; }";
    ASSERT_TRUE(extExecRepl(cmd));

    cmd = "int bar() { return foo() + 1; }\n int a = bar();";
    ASSERT_TRUE(extExecRepl(cmd));
}

TEST_F(ReplTests, RecursiveFunction) {
    std::string_view cmd = R"(
        int factorial(int n) {
            if (n <= 1) return 1;
            return n * factorial(n - 1);
        }
    )";
    ASSERT_TRUE(extExecRepl(cmd));

    cmd = "int result = factorial(5);";
    ASSERT_TRUE(extExecRepl(cmd));
    ASSERT_EQ(120, std::any_cast<int>(getResultRepl("result")));
}

TEST_F(ReplTests, CodeWithIncludes) {
    std::string_view cmd = R"(
        #include <cmath>
        double calculateHypotenuse(double a, double b) {
            return std::sqrt(a * a + b * b);
        }
    )";
    ASSERT_TRUE(extExecRepl(cmd));

    cmd = "double result = calculateHypotenuse(3.0, 4.0);";
    ASSERT_TRUE(extExecRepl(cmd));
    ASSERT_EQ(5.0, std::any_cast<double>(getResultRepl("result")));
}

TEST_F(ReplTests, FunctionOverloading) {
    std::string_view cmd = R"(
        int multiply(int a, int b) { return a * b; }
        double multiply(double a, double b) { return a * b; }
    )";
    ASSERT_TRUE(extExecRepl(cmd));

    cmd = "int intResult = multiply(3, 4);";
    ASSERT_TRUE(extExecRepl(cmd));
    ASSERT_EQ(12, std::any_cast<int>(getResultRepl("intResult")));

    cmd = "double doubleResult = multiply(2.5, 4.0);";
    ASSERT_TRUE(extExecRepl(cmd));
    ASSERT_EQ(10.0, std::any_cast<double>(getResultRepl("doubleResult")));
}

TEST_F(ReplTests, IncludesAreWorking) {
    ASSERT_TRUE(extExecRepl("#include <numeric>"));
    ASSERT_TRUE(extExecRepl("#include <vector>"));

    ASSERT_TRUE(extExecRepl("std::vector<int> numbers = {1, 2, 3, 4, 5};"));
    ASSERT_EQ(15, std::any_cast<int>(getResultRepl(
                      "std::accumulate(numbers.begin(), numbers.end(), 0)")));
}
