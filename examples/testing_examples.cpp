#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "refactored_interfaces.hpp"
#include "command_system_example.hpp"
#include <memory>
#include <filesystem>
#include <fstream>

using namespace repl::interfaces;
using namespace repl::commands;

// Mock implementations for testing

class MockCompiler : public ICompiler {
public:
    MOCK_METHOD(std::expected<CompilationResult, CompilerError>,
                compile, (const std::string& source_code, const CompilerConfig& config), (override));
    MOCK_METHOD(bool, isAvailable, (), (const, override));
    MOCK_METHOD(std::expected<std::string, CompilerError>, getVersion, (), (const, override));
    MOCK_METHOD(std::expected<bool, CompilerError>,
                validateSyntax, (const std::string& source_code), (const, override));
};

class MockExecutor : public IExecutor {
public:
    MOCK_METHOD(std::expected<ExecutionResult, ExecutionError>,
                execute, (const CompilationResult& compilation_result), (override));
    MOCK_METHOD(std::expected<std::any, ExecutionError>,
                getVariable, (const std::string& variable_name), (override));
    MOCK_METHOD(std::expected<void, ExecutionError>,
                setVariable, (const std::string& variable_name, const std::any& value), (override));
    MOCK_METHOD(std::vector<std::string>, getAvailableVariables, (), (const, override));
    MOCK_METHOD(void, cleanup, (), (override));
};

class MockReplContext : public IReplContext {
private:
    CompilerConfig config_;
    std::vector<std::filesystem::path> include_dirs_;
    std::vector<std::string> link_libs_;
    std::vector<std::string> preprocessor_defs_;

public:
    MOCK_METHOD(void, setCompilerConfig, (const CompilerConfig& config), (override));
  
    CompilerConfig getCompilerConfig() const override {
        return config_;
    }

    void addIncludeDirectory(const std::filesystem::path& path) override {
        include_dirs_.push_back(path);
    }

    MOCK_METHOD(void, removeIncludeDirectory, (const std::filesystem::path& path), (override));
  
    std::vector<std::filesystem::path> getIncludeDirectories() const override {
        return include_dirs_;
    }

    void addLinkLibrary(const std::string& library) override {
        link_libs_.push_back(library);
    }

    MOCK_METHOD(void, removeLinkLibrary, (const std::string& library), (override));
  
    std::vector<std::string> getLinkLibraries() const override {
        return link_libs_;
    }

    void addPreprocessorDefinition(const std::string& definition) override {
        preprocessor_defs_.push_back(definition);
    }

    MOCK_METHOD(void, removePreprocessorDefinition, (const std::string& definition), (override));
  
    std::vector<std::string> getPreprocessorDefinitions() const override {
        return preprocessor_defs_;
    }

    MOCK_METHOD(void, reset, (), (override));
    MOCK_METHOD(std::string, getSessionId, (), (const, override));
};

// Test Fixtures

class CompilerTests : public ::testing::Test {
protected:
    void SetUp() override {
        compiler_ = std::make_unique<MockCompiler>();
    }

    std::unique_ptr<MockCompiler> compiler_;
};

class ExecutorTests : public ::testing::Test {
protected:
    void SetUp() override {
        executor_ = std::make_unique<MockExecutor>();
    }

    std::unique_ptr<MockExecutor> executor_;
};

class ReplContextTests : public ::testing::Test {
protected:
    void SetUp() override {
        context_ = std::make_unique<MockReplContext>();
    }

    std::unique_ptr<MockReplContext> context_;
};

class CommandSystemTests : public ::testing::Test {
protected:
    void SetUp() override {
        context_ = std::make_unique<MockReplContext>();
        command_manager_ = std::make_unique<CommandManager>();
    }

    std::unique_ptr<MockReplContext> context_;
    std::unique_ptr<CommandManager> command_manager_;
};

// Unit Tests

TEST_F(CompilerTests, CompileSuccessfulCode) {
    std::string source_code = "int main() { return 0; }";
    CompilerConfig config;
  
    CompilationResult expected_result;
    expected_result.library_path = "/tmp/test_lib.so";
    expected_result.compilation_time = std::chrono::milliseconds(100);
    expected_result.exit_code = 0;
    expected_result.success = true;
  
    EXPECT_CALL(*compiler_, compile(source_code, testing::_))
        .WillOnce(testing::Return(expected_result));
  
    auto result = compiler_->compile(source_code, config);
  
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->library_path, "/tmp/test_lib.so");
    EXPECT_EQ(result->exit_code, 0);
}

TEST_F(CompilerTests, CompileInvalidCode) {
    std::string invalid_code = "int invalid syntax here;";
    CompilerConfig config;
  
    EXPECT_CALL(*compiler_, compile(invalid_code, testing::_))
        .WillOnce(testing::Return(std::unexpected(CompilerError::SyntaxError)));
  
    auto result = compiler_->compile(invalid_code, config);
  
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), CompilerError::SyntaxError);
}

TEST_F(CompilerTests, ValidateSyntaxValid) {
    std::string valid_code = "int x = 42;";
  
    EXPECT_CALL(*compiler_, validateSyntax(valid_code))
        .WillOnce(testing::Return(true));
  
    auto result = compiler_->validateSyntax(valid_code);
  
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(*result);
}

TEST_F(CompilerTests, ValidateSyntaxInvalid) {
    std::string invalid_code = "int x = ;";
  
    EXPECT_CALL(*compiler_, validateSyntax(invalid_code))
        .WillOnce(testing::Return(false));
  
    auto result = compiler_->validateSyntax(invalid_code);
  
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(*result);
}

TEST_F(ExecutorTests, ExecuteSuccessfully) {
    CompilationResult compile_result;
    compile_result.library_path = "/tmp/test_lib.so";
  
    ExecutionResult expected_result;
    expected_result.success = true;
    expected_result.execution_time = std::chrono::microseconds(1000);
  
    EXPECT_CALL(*executor_, execute(testing::_))
        .WillOnce(testing::Return(expected_result));
  
    auto result = executor_->execute(compile_result);
  
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->success);
}

TEST_F(ExecutorTests, GetVariable) {
    std::string var_name = "test_var";
    int expected_value = 42;
  
    EXPECT_CALL(*executor_, getVariable(var_name))
        .WillOnce(testing::Return(std::any(expected_value)));
  
    auto result = executor_->getVariable(var_name);
  
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::any_cast<int>(*result), expected_value);
}

TEST_F(ExecutorTests, GetNonExistentVariable) {
    std::string var_name = "nonexistent";
  
    EXPECT_CALL(*executor_, getVariable(var_name))
        .WillOnce(testing::Return(std::unexpected(ExecutionError::SymbolNotFound)));
  
    auto result = executor_->getVariable(var_name);
  
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), ExecutionError::SymbolNotFound);
}

TEST_F(ReplContextTests, AddIncludeDirectory) {
    std::filesystem::path include_path = "/usr/include";
  
    context_->addIncludeDirectory(include_path);
  
    auto includes = context_->getIncludeDirectories();
    ASSERT_EQ(includes.size(), 1);
    EXPECT_EQ(includes[0], include_path);
}

TEST_F(ReplContextTests, AddLinkLibrary) {
    std::string library = "pthread";
  
    context_->addLinkLibrary(library);
  
    auto libraries = context_->getLinkLibraries();
    ASSERT_EQ(libraries.size(), 1);
    EXPECT_EQ(libraries[0], library);
}

TEST_F(ReplContextTests, AddPreprocessorDefinition) {
    std::string definition = "DEBUG=1";
  
    context_->addPreprocessorDefinition(definition);
  
    auto definitions = context_->getPreprocessorDefinitions();
    ASSERT_EQ(definitions.size(), 1);
    EXPECT_EQ(definitions[0], definition);
}

TEST_F(CommandSystemTests, ParseCommandLine) {
    EXPECT_TRUE(CommandParser::isCommand(":help"));
    EXPECT_TRUE(CommandParser::isCommand(":include <iostream>"));
    EXPECT_FALSE(CommandParser::isCommand("int x = 42;"));
    EXPECT_FALSE(CommandParser::isCommand(""));
  
    EXPECT_EQ(CommandParser::extractCommand(":help"), "help");
    EXPECT_EQ(CommandParser::extractCommand(":include <iostream>"), "include <iostream>");
}

TEST_F(CommandSystemTests, ExecuteIncludeCommand) {
    std::string command_line = "include <iostream>";
  
    auto result = command_manager_->executeCommand(command_line, *context_);
  
    ASSERT_TRUE(result.has_value());
  
    // Verify that preprocessor definition was added
    auto definitions = context_->getPreprocessorDefinitions();
    EXPECT_GT(definitions.size(), 0);
}

TEST_F(CommandSystemTests, ExecuteLinkCommand) {
    std::string command_line = "link pthread";
  
    auto result = command_manager_->executeCommand(command_line, *context_);
  
    ASSERT_TRUE(result.has_value());
  
    // Verify that library was added
    auto libraries = context_->getLinkLibraries();
    ASSERT_EQ(libraries.size(), 1);
    EXPECT_EQ(libraries[0], "pthread");
}

TEST_F(CommandSystemTests, ExecuteDefineCommand) {
    std::string command_line = "define DEBUG 1";
  
    auto result = command_manager_->executeCommand(command_line, *context_);
  
    ASSERT_TRUE(result.has_value());
  
    // Verify that preprocessor definition was added
    auto definitions = context_->getPreprocessorDefinitions();
    ASSERT_EQ(definitions.size(), 1);
    EXPECT_EQ(definitions[0], "DEBUG=1");
}

TEST_F(CommandSystemTests, ExecuteInvalidCommand) {
    std::string command_line = "nonexistent_command arg1 arg2";
  
    auto result = command_manager_->executeCommand(command_line, *context_);
  
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), ReplError::InvalidCommand);
}

// Integration Tests

class ReplIntegrationTests : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temporary directory for testing
        temp_dir_ = std::filesystem::temp_directory_path() / "repl_integration_test";
        std::filesystem::create_directories(temp_dir_);
      
        compiler_ = std::make_unique<MockCompiler>();
        executor_ = std::make_unique<MockExecutor>();
        context_ = createReplContext();  // Use real implementation
    }

    void TearDown() override {
        // Clean up temporary directory
        std::error_code ec;
        std::filesystem::remove_all(temp_dir_, ec);
    }

    std::filesystem::path temp_dir_;
    std::unique_ptr<MockCompiler> compiler_;
    std::unique_ptr<MockExecutor> executor_;
    std::unique_ptr<IReplContext> context_;
};

TEST_F(ReplIntegrationTests, EndToEndVariableDeclaration) {
    // Test the full pipeline: command parsing -> compilation -> execution -> result retrieval
  
    // 1. Set up context with necessary configuration
    context_->addIncludeDirectory("/usr/include");
    context_->addLinkLibrary("c");
  
    // 2. Simulate compilation
    std::string source_code = "int x = 42;";
    CompilerConfig config = context_->getCompilerConfig();
  
    CompilationResult compile_result;
    compile_result.library_path = temp_dir_ / "test_lib.so";
    compile_result.success = true;
  
    EXPECT_CALL(*compiler_, compile(testing::_, testing::_))
        .WillOnce(testing::Return(compile_result));
  
    auto compilation = compiler_->compile(source_code, config);
    ASSERT_TRUE(compilation.has_value());
  
    // 3. Simulate execution
    ExecutionResult exec_result;
    exec_result.success = true;
    exec_result.execution_time = std::chrono::microseconds(100);
  
    EXPECT_CALL(*executor_, execute(testing::_))
        .WillOnce(testing::Return(exec_result));
  
    auto execution = executor_->execute(*compilation);
    ASSERT_TRUE(execution.has_value());
    EXPECT_TRUE(execution->success);
  
    // 4. Simulate variable retrieval
    EXPECT_CALL(*executor_, getVariable("x"))
        .WillOnce(testing::Return(std::any(42)));
  
    auto variable = executor_->getVariable("x");
    ASSERT_TRUE(variable.has_value());
    EXPECT_EQ(std::any_cast<int>(*variable), 42);
}

TEST_F(ReplIntegrationTests, CommandSystemIntegration) {
    CommandManager command_manager;
  
    // Test multiple commands in sequence
    ASSERT_TRUE(command_manager.executeCommand("include <iostream>", *context_).has_value());
    ASSERT_TRUE(command_manager.executeCommand("link pthread", *context_).has_value());
    ASSERT_TRUE(command_manager.executeCommand("define DEBUG 1", *context_).has_value());
  
    // Verify state after all commands
    auto config = context_->getCompilerConfig();
    auto libraries = context_->getLinkLibraries();
    auto definitions = context_->getPreprocessorDefinitions();
  
    EXPECT_GT(libraries.size(), 0);
    EXPECT_GT(definitions.size(), 0);
  
    // Test help command
    testing::internal::CaptureStdout();
    ASSERT_TRUE(command_manager.executeCommand("help", *context_).has_value());
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_GT(output.length(), 0);
    EXPECT_NE(output.find("Available commands"), std::string::npos);
}

// Performance Tests

class ReplPerformanceTests : public ::testing::Test {
protected:
    void SetUp() override {
        context_ = createReplContext();
    }

    std::unique_ptr<IReplContext> context_;
};

TEST_F(ReplPerformanceTests, ContextOperationsPerformance) {
    const int num_operations = 1000;
  
    auto start_time = std::chrono::high_resolution_clock::now();
  
    // Add many include directories
    for (int i = 0; i < num_operations; ++i) {
        context_->addIncludeDirectory("/usr/include/" + std::to_string(i));
    }
  
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
  
    // Should be able to add 1000 directories in less than 100ms
    EXPECT_LT(duration.count(), 100);
  
    // Verify all directories were added
    auto directories = context_->getIncludeDirectories();
    EXPECT_EQ(directories.size(), num_operations);
}

TEST_F(ReplPerformanceTests, CommandParsingPerformance) {
    CommandManager command_manager;
    const int num_commands = 1000;
  
    auto start_time = std::chrono::high_resolution_clock::now();
  
    for (int i = 0; i < num_commands; ++i) {
        std::string command = "link lib" + std::to_string(i);
        command_manager.executeCommand(command, *context_);
    }
  
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
  
    // Should be able to execute 1000 commands in less than 500ms
    EXPECT_LT(duration.count(), 500);
  
    // Verify all libraries were added
    auto libraries = context_->getLinkLibraries();
    EXPECT_EQ(libraries.size(), num_commands);
}

// Stress Tests

TEST_F(ReplPerformanceTests, MemoryUsageStability) {
    // Test that repeated operations don't cause memory leaks
    const int num_cycles = 100;
  
    for (int cycle = 0; cycle < num_cycles; ++cycle) {
        // Add and remove many items
        for (int i = 0; i < 100; ++i) {
            context_->addIncludeDirectory("/tmp/" + std::to_string(i));
            context_->addLinkLibrary("lib" + std::to_string(i));
            context_->addPreprocessorDefinition("DEF" + std::to_string(i));
        }
      
        // Reset context (should clean up all allocations)
        context_->reset();
      
        // Verify reset worked
        EXPECT_EQ(context_->getIncludeDirectories().size(), 0);
        EXPECT_EQ(context_->getLinkLibraries().size(), 0);
        EXPECT_EQ(context_->getPreprocessorDefinitions().size(), 0);
    }
}

// Example usage test
TEST(ReplUsageExample, BasicWorkflow) {
    // This test demonstrates how the refactored system would be used
  
    // 1. Create components
    auto context = createReplContext();
    CommandManager command_manager;
  
    // 2. Configure the environment
    ASSERT_TRUE(command_manager.executeCommand("include <iostream>", *context).has_value());
    ASSERT_TRUE(command_manager.executeCommand("link c++", *context).has_value());
  
    // 3. Check configuration
    auto config = context->getCompilerConfig();
    EXPECT_FALSE(config.link_libraries.empty());
  
    // 4. The system is now ready for C++ code compilation and execution
    // (In the real implementation, we would proceed with compilation)
  
    std::cout << "REPL system configured successfully!" << std::endl;
}