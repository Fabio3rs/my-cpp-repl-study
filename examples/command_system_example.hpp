#pragma once

#include "refactored_interfaces.hpp"
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace repl::commands {

using namespace interfaces;

/**
 * @brief Base command class with common functionality
 */
class BaseCommand : public ICommand {
  protected:
    std::string name_;
    std::string description_;
    std::string usage_;

  public:
    BaseCommand(std::string name, std::string description, std::string usage)
        : name_(std::move(name)), description_(std::move(description)),
          usage_(std::move(usage)) {}

    std::string getName() const override { return name_; }
    std::string getDescription() const override { return description_; }
    std::string getUsage() const override { return usage_; }

  protected:
    // Helper to validate argument count
    std::expected<void, ReplError>
    validateArgCount(const std::vector<std::string> &args, size_t expected_min,
                     size_t expected_max = SIZE_MAX) {
        if (args.size() < expected_min) {
            return std::unexpected(ReplError::InvalidCommand);
        }
        if (args.size() > expected_max) {
            return std::unexpected(ReplError::InvalidCommand);
        }
        return {};
    }
};

/**
 * @brief Include header command - adds system/user headers
 * Usage: include <header> or include "header"
 */
class IncludeCommand : public BaseCommand {
  public:
    IncludeCommand()
        : BaseCommand("include", "Add include directive for header file",
                      "include <header> or include \"header\"") {}

    std::expected<void, ReplError> execute(const std::vector<std::string> &args,
                                           IReplContext &context) override {
        auto validation = validateArgCount(args, 1, 1);
        if (!validation.has_value()) {
            return validation;
        }

        const std::string &header = args[0];

        // Determine if it's a system header or user header
        bool is_system_header =
            (header.front() == '<' && header.back() == '>') ||
            (header.front() != '"' && header.back() != '"');

        // Add to preprocessor definitions as include directive
        if (is_system_header) {
            context.addPreprocessorDefinition("INCLUDE_SYSTEM_" + header);
        } else {
            // Remove quotes and add as include directory
            std::string clean_header = header.substr(1, header.length() - 2);
            std::filesystem::path header_dir =
                std::filesystem::path(clean_header).parent_path();
            if (!header_dir.empty()) {
                context.addIncludeDirectory(header_dir);
            }
        }

        return {};
    }
};

/**
 * @brief Link library command - adds libraries for linking
 * Usage: link library_name
 */
class LinkCommand : public BaseCommand {
  public:
    LinkCommand()
        : BaseCommand("link", "Add library for linking",
                      "link <library_name>") {}

    std::expected<void, ReplError> execute(const std::vector<std::string> &args,
                                           IReplContext &context) override {
        auto validation = validateArgCount(args, 1, 1);
        if (!validation.has_value()) {
            return validation;
        }

        const std::string &library = args[0];
        context.addLinkLibrary(library);
        return {};
    }
};

/**
 * @brief Define preprocessor macro command
 * Usage: define MACRO_NAME [value]
 */
class DefineCommand : public BaseCommand {
  public:
    DefineCommand()
        : BaseCommand("define", "Define preprocessor macro",
                      "define <MACRO_NAME> [value]") {}

    std::expected<void, ReplError> execute(const std::vector<std::string> &args,
                                           IReplContext &context) override {
        auto validation = validateArgCount(args, 1, 2);
        if (!validation.has_value()) {
            return validation;
        }

        std::string definition = args[0];
        if (args.size() > 1) {
            definition += "=" + args[1];
        }

        context.addPreprocessorDefinition(definition);
        return {};
    }
};

/**
 * @brief Help command - shows available commands and usage
 * Usage: help [command_name]
 */
class HelpCommand : public BaseCommand {
  private:
    std::reference_wrapper<
        const std::unordered_map<std::string, std::unique_ptr<ICommand>>>
        commands_ref_;

  public:
    explicit HelpCommand(
        const std::unordered_map<std::string, std::unique_ptr<ICommand>>
            &commands)
        : BaseCommand("help", "Show help information", "help [command_name]"),
          commands_ref_(commands) {}

    std::expected<void, ReplError> execute(const std::vector<std::string> &args,
                                           IReplContext &context) override {
        if (args.empty()) {
            // Show all commands
            std::cout << "Available commands:\n";
            for (const auto &[name, command] : commands_ref_.get()) {
                std::cout << "  " << name << " - " << command->getDescription()
                          << "\n";
            }
            std::cout
                << "\nUse 'help <command>' for detailed usage information.\n";
        } else {
            // Show specific command help
            const std::string &command_name = args[0];
            auto it = commands_ref_.get().find(command_name);
            if (it != commands_ref_.get().end()) {
                const auto &command = it->second;
                std::cout << "Command: " << command->getName() << "\n";
                std::cout << "Description: " << command->getDescription()
                          << "\n";
                std::cout << "Usage: " << command->getUsage() << "\n";
            } else {
                std::cout << "Unknown command: " << command_name << "\n";
                return std::unexpected(ReplError::InvalidCommand);
            }
        }
        return {};
    }
};

/**
 * @brief Reset command - clears REPL state
 * Usage: reset
 */
class ResetCommand : public BaseCommand {
  public:
    ResetCommand()
        : BaseCommand("reset", "Reset REPL context to initial state", "reset") {
    }

    std::expected<void, ReplError> execute(const std::vector<std::string> &args,
                                           IReplContext &context) override {
        auto validation = validateArgCount(args, 0, 0);
        if (!validation.has_value()) {
            return validation;
        }

        context.reset();
        std::cout << "REPL context has been reset.\n";
        return {};
    }
};

/**
 * @brief Status command - shows current REPL configuration
 * Usage: status
 */
class StatusCommand : public BaseCommand {
  public:
    StatusCommand()
        : BaseCommand("status", "Show current REPL configuration", "status") {}

    std::expected<void, ReplError> execute(const std::vector<std::string> &args,
                                           IReplContext &context) override {
        auto validation = validateArgCount(args, 0, 0);
        if (!validation.has_value()) {
            return validation;
        }

        auto config = context.getCompilerConfig();

        std::cout << "REPL Status:\n";
        std::cout << "  Session ID: " << context.getSessionId() << "\n";
        std::cout << "  Compiler: " << config.compiler_path << "\n";
        std::cout << "  C++ Standard: " << config.std_version << "\n";
        std::cout << "  Debug Info: "
                  << (config.enable_debug_info ? "enabled" : "disabled")
                  << "\n";
        std::cout << "  Optimization: "
                  << (config.enable_optimization ? "enabled" : "disabled")
                  << "\n";

        auto includes = context.getIncludeDirectories();
        std::cout << "  Include Directories (" << includes.size() << "):\n";
        for (const auto &dir : includes) {
            std::cout << "    " << dir << "\n";
        }

        auto libraries = context.getLinkLibraries();
        std::cout << "  Link Libraries (" << libraries.size() << "):\n";
        for (const auto &lib : libraries) {
            std::cout << "    " << lib << "\n";
        }

        auto definitions = context.getPreprocessorDefinitions();
        std::cout << "  Preprocessor Definitions (" << definitions.size()
                  << "):\n";
        for (const auto &def : definitions) {
            std::cout << "    " << def << "\n";
        }

        return {};
    }
};

/**
 * @brief Quit command - exits the REPL
 * Usage: quit, exit, or q
 */
class QuitCommand : public BaseCommand {
  private:
    bool &should_exit_;

  public:
    explicit QuitCommand(bool &should_exit)
        : BaseCommand("quit", "Exit the REPL", "quit, exit, or q"),
          should_exit_(should_exit) {}

    std::expected<void, ReplError> execute(const std::vector<std::string> &args,
                                           IReplContext &context) override {
        auto validation = validateArgCount(args, 0, 0);
        if (!validation.has_value()) {
            return validation;
        }

        should_exit_ = true;
        std::cout << "Goodbye!\n";
        return {};
    }
};

/**
 * @brief Command manager for parsing and executing commands
 */
class CommandManager {
  private:
    std::unordered_map<std::string, std::unique_ptr<ICommand>> commands_;
    std::unique_ptr<HelpCommand> help_command_;
    bool should_exit_ = false;

  public:
    CommandManager() {
        // Register built-in commands
        registerCommand(std::make_unique<IncludeCommand>());
        registerCommand(std::make_unique<LinkCommand>());
        registerCommand(std::make_unique<DefineCommand>());
        registerCommand(std::make_unique<ResetCommand>());
        registerCommand(std::make_unique<StatusCommand>());
        registerCommand(std::make_unique<QuitCommand>(should_exit_));

        // Create help command with reference to commands map
        help_command_ = std::make_unique<HelpCommand>(commands_);
        commands_["help"] = std::unique_ptr<ICommand>(help_command_.get());

        // Add aliases
        commands_["exit"] = std::unique_ptr<ICommand>(commands_["quit"].get());
        commands_["q"] = std::unique_ptr<ICommand>(commands_["quit"].get());
    }

    void registerCommand(std::unique_ptr<ICommand> command) {
        std::string name = command->getName();
        commands_[name] = std::move(command);
    }

    std::expected<void, ReplError>
    executeCommand(const std::string &command_line, IReplContext &context) {
        if (command_line.empty()) {
            return std::unexpected(ReplError::InvalidCommand);
        }

        // Parse command line into command and arguments
        std::istringstream iss(command_line);
        std::string command_name;
        iss >> command_name;

        // Check if command exists
        auto it = commands_.find(command_name);
        if (it == commands_.end()) {
            return std::unexpected(ReplError::InvalidCommand);
        }

        // Parse arguments
        std::vector<std::string> args;
        std::string arg;
        while (iss >> arg) {
            args.push_back(arg);
        }

        // Execute command
        return it->second->execute(args, context);
    }

    bool shouldExit() const { return should_exit_; }

    std::vector<std::string> getAvailableCommands() const {
        std::vector<std::string> names;
        names.reserve(commands_.size());

        for (const auto &[name, _] : commands_) {
            names.push_back(name);
        }

        return names;
    }
};

/**
 * @brief Command line parser utilities
 */
class CommandParser {
  public:
    /**
     * @brief Check if a line is a REPL command (starts with colon)
     * @param line Input line to check
     * @return True if it's a command, false if it's C++ code
     */
    static bool isCommand(const std::string &line) {
        auto trimmed = trim(line);
        return !trimmed.empty() && trimmed[0] == ':';
    }

    /**
     * @brief Extract command from command line (remove leading colon)
     * @param command_line Full command line with leading colon
     * @return Command without colon
     */
    static std::string extractCommand(const std::string &command_line) {
        auto trimmed = trim(command_line);
        if (trimmed.empty() || trimmed[0] != ':') {
            return trimmed;
        }
        return trim(trimmed.substr(1));
    }

  private:
    static std::string trim(const std::string &str) {
        auto start = str.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) {
            return "";
        }
        auto end = str.find_last_not_of(" \t\r\n");
        return str.substr(start, end - start + 1);
    }
};

} // namespace repl::commands