#include "refactored_interfaces.hpp"
#include <dlfcn.h>
#include <fstream>
#include <sstream>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <filesystem>
#include <chrono>
#include <cstdlib>
#include <memory>

namespace repl::implementation {

// RAII wrapper for dynamic libraries
class ScopedLibrary {
private:
    void* handle_;
    std::filesystem::path path_;

public:
    explicit ScopedLibrary(const std::filesystem::path& path)
        : handle_(nullptr), path_(path) {
        handle_ = dlopen(path.c_str(), RTLD_LAZY | RTLD_GLOBAL);
        if (!handle_) {
            throw std::runtime_error("Failed to load library: " + std::string(dlerror()));
        }
    }

    ~ScopedLibrary() {
        if (handle_) {
            dlclose(handle_);
        }
    }

    // Non-copyable but movable
    ScopedLibrary(const ScopedLibrary&) = delete;
    ScopedLibrary& operator=(const ScopedLibrary&) = delete;

    ScopedLibrary(ScopedLibrary&& other) noexcept
        : handle_(other.handle_), path_(std::move(other.path_)) {
        other.handle_ = nullptr;
    }

    ScopedLibrary& operator=(ScopedLibrary&& other) noexcept {
        if (this != &other) {
            if (handle_) {
                dlclose(handle_);
            }
            handle_ = other.handle_;
            path_ = std::move(other.path_);
            other.handle_ = nullptr;
        }
        return *this;
    }

    template<typename T>
    std::expected<T*, ExecutionError> getSymbol(const std::string& name) {
        if (!handle_) {
            return std::unexpected(ExecutionError::InvalidOperation);
        }

        // Clear any existing error
        dlerror();
      
        void* sym = dlsym(handle_, name.c_str());
        const char* error = dlerror();
      
        if (error) {
            return std::unexpected(ExecutionError::SymbolNotFound);
        }
      
        return reinterpret_cast<T*>(sym);
    }

    bool isValid() const { return handle_ != nullptr; }
    const std::filesystem::path& getPath() const { return path_; }
};

// Thread-safe REPL context implementation
class ReplContext : public interfaces::IReplContext {
private:
    mutable std::shared_mutex mutex_;
    interfaces::CompilerConfig config_;
    std::string session_id_;
  
public:
    explicit ReplContext(const std::string& session_id = "default")
        : session_id_(session_id) {
        // Initialize default configuration
        config_.compiler_path = "clang++";
        config_.std_version = "gnu++20";
        config_.enable_debug_info = true;
    }

    void setCompilerConfig(const interfaces::CompilerConfig& config) override {
        std::unique_lock lock(mutex_);
        config_ = config;
    }

    interfaces::CompilerConfig getCompilerConfig() const override {
        std::shared_lock lock(mutex_);
        return config_;
    }

    void addIncludeDirectory(const std::filesystem::path& path) override {
        std::unique_lock lock(mutex_);
        auto it = std::find(config_.include_directories.begin(),
                           config_.include_directories.end(), path);
        if (it == config_.include_directories.end()) {
            config_.include_directories.push_back(path);
        }
    }

    void removeIncludeDirectory(const std::filesystem::path& path) override {
        std::unique_lock lock(mutex_);
        config_.include_directories.erase(
            std::remove(config_.include_directories.begin(),
                       config_.include_directories.end(), path),
            config_.include_directories.end()
        );
    }

    std::vector<std::filesystem::path> getIncludeDirectories() const override {
        std::shared_lock lock(mutex_);
        return config_.include_directories;
    }

    void addLinkLibrary(const std::string& library) override {
        std::unique_lock lock(mutex_);
        auto it = std::find(config_.link_libraries.begin(),
                           config_.link_libraries.end(), library);
        if (it == config_.link_libraries.end()) {
            config_.link_libraries.push_back(library);
        }
    }

    void removeLinkLibrary(const std::string& library) override {
        std::unique_lock lock(mutex_);
        config_.link_libraries.erase(
            std::remove(config_.link_libraries.begin(),
                       config_.link_libraries.end(), library),
            config_.link_libraries.end()
        );
    }

    std::vector<std::string> getLinkLibraries() const override {
        std::shared_lock lock(mutex_);
        return config_.link_libraries;
    }

    void addPreprocessorDefinition(const std::string& definition) override {
        std::unique_lock lock(mutex_);
        auto it = std::find(config_.preprocessor_definitions.begin(),
                           config_.preprocessor_definitions.end(), definition);
        if (it == config_.preprocessor_definitions.end()) {
            config_.preprocessor_definitions.push_back(definition);
        }
    }

    void removePreprocessorDefinition(const std::string& definition) override {
        std::unique_lock lock(mutex_);
        config_.preprocessor_definitions.erase(
            std::remove(config_.preprocessor_definitions.begin(),
                       config_.preprocessor_definitions.end(), definition),
            config_.preprocessor_definitions.end()
        );
    }

    std::vector<std::string> getPreprocessorDefinitions() const override {
        std::shared_lock lock(mutex_);
        return config_.preprocessor_definitions;
    }

    void reset() override {
        std::unique_lock lock(mutex_);
        config_ = interfaces::CompilerConfig{};
        config_.compiler_path = "clang++";
        config_.std_version = "gnu++20";
        config_.enable_debug_info = true;
    }

    std::string getSessionId() const override {
        std::shared_lock lock(mutex_);
        return session_id_;
    }
};

// Clang compiler implementation
class ClangCompiler : public interfaces::ICompiler {
private:
    std::filesystem::path temp_dir_;
    std::atomic<int> compilation_counter_{0};

    std::string buildCompileCommand(const interfaces::CompilerConfig& config,
                                   const std::filesystem::path& source_file,
                                   const std::filesystem::path& output_file) {
        std::ostringstream cmd;
      
        cmd << config.compiler_path;
        cmd << " -shared -fPIC";
        cmd << " -std=" << config.std_version;
      
        if (config.enable_debug_info) {
            cmd << " -g";
        }
      
        if (config.enable_optimization) {
            cmd << " -O2";
        }
      
        // Add include directories
        for (const auto& inc_dir : config.include_directories) {
            cmd << " -I" << inc_dir;
        }
      
        // Add preprocessor definitions
        for (const auto& def : config.preprocessor_definitions) {
            cmd << " -D" << def;
        }
      
        // Add link libraries
        for (const auto& lib : config.link_libraries) {
            cmd << " -l" << lib;
        }
      
        // Add custom flags
        for (const auto& flag : config.compiler_flags) {
            cmd << " " << flag;
        }
      
        cmd << " " << source_file;
        cmd << " -o " << output_file;
        cmd << " 2>&1"; // Capture stderr
      
        return cmd.str();
    }

    std::expected<std::string, interfaces::CompilerError>
    runCommand(const std::string& command, std::chrono::milliseconds timeout) {
        // For simplicity, using system() - in production, use proper process management
        std::string temp_output = temp_dir_ / ("compile_output_" + std::to_string(compilation_counter_++) + ".txt");
        std::string full_cmd = command + " > " + temp_output + " 2>&1";
      
        auto start_time = std::chrono::steady_clock::now();
        int result = std::system(full_cmd.c_str());
        auto end_time = std::chrono::steady_clock::now();
      
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        if (elapsed > timeout) {
            return std::unexpected(interfaces::CompilerError::InternalError);
        }
      
        // Read output
        std::ifstream output_file(temp_output);
        std::string output((std::istreambuf_iterator<char>(output_file)),
                          std::istreambuf_iterator<char>());
      
        // Clean up temp file
        std::filesystem::remove(temp_output);
      
        if (result != 0) {
            // Determine error type based on output
            if (output.find("error:") != std::string::npos) {
                return std::unexpected(interfaces::CompilerError::SyntaxError);
            }
            return std::unexpected(interfaces::CompilerError::InternalError);
        }
      
        return output;
    }

public:
    ClangCompiler() {
        // Create temporary directory for compilation artifacts
        temp_dir_ = std::filesystem::temp_directory_path() / "cpprepl_compile";
        std::filesystem::create_directories(temp_dir_);
    }

    ~ClangCompiler() {
        // Clean up temporary directory
        std::error_code ec;
        std::filesystem::remove_all(temp_dir_, ec);
    }

    std::expected<interfaces::CompilationResult, interfaces::CompilerError>
    compile(const std::string& source_code, const interfaces::CompilerConfig& config) override {
        try {
            auto start_time = std::chrono::steady_clock::now();
          
            // Create unique source file
            int counter = compilation_counter_++;
            auto source_file = temp_dir_ / ("source_" + std::to_string(counter) + ".cpp");
            auto output_file = temp_dir_ / ("lib_" + std::to_string(counter) + ".so");
          
            // Write source code to file
            {
                std::ofstream file(source_file);
                if (!file) {
                    return std::unexpected(interfaces::CompilerError::FileNotFound);
                }
                file << source_code;
            }
          
            // Build and run compile command
            std::string compile_cmd = buildCompileCommand(config, source_file, output_file);
            auto compile_result = runCommand(compile_cmd, config.timeout);
          
            if (!compile_result.has_value()) {
                return std::unexpected(compile_result.error());
            }
          
            auto end_time = std::chrono::steady_clock::now();
            auto compilation_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
          
            // Check if output file was created
            if (!std::filesystem::exists(output_file)) {
                return std::unexpected(interfaces::CompilerError::InternalError);
            }
          
            interfaces::CompilationResult result;
            result.library_path = output_file;
            result.compilation_time = compilation_time;
            result.compiler_output = *compile_result;
            result.exit_code = 0;
          
            // TODO: Extract exported symbols using nm or similar
            result.exported_symbols = {"exec"};  // Simplified for example
          
            return result;
          
        } catch (const std::exception&) {
            return std::unexpected(interfaces::CompilerError::InternalError);
        }
    }

    bool isAvailable() const override {
        // Check if clang++ is available
        int result = std::system("clang++ --version > /dev/null 2>&1");
        return result == 0;
    }

    std::expected<std::string, interfaces::CompilerError> getVersion() const override {
        auto result = runCommand("clang++ --version", std::chrono::seconds(5));
        if (!result.has_value()) {
            return std::unexpected(result.error());
        }
        return *result;
    }

    std::expected<bool, interfaces::CompilerError>
    validateSyntax(const std::string& source_code) const override {
        // Use clang to check syntax only
        auto temp_source = temp_dir_ / ("syntax_check_" + std::to_string(compilation_counter_++) + ".cpp");
      
        {
            std::ofstream file(temp_source);
            if (!file) {
                return std::unexpected(interfaces::CompilerError::FileNotFound);
            }
            file << source_code;
        }
      
        std::string check_cmd = "clang++ -fsyntax-only " + temp_source.string() + " 2>&1";
        auto result = runCommand(check_cmd, std::chrono::seconds(10));
      
        // Clean up
        std::filesystem::remove(temp_source);
      
        if (!result.has_value()) {
            return std::unexpected(result.error());
        }
      
        return result->empty() || result->find("error:") == std::string::npos;
    }
};

// Dynamic library executor implementation
class DynamicExecutor : public interfaces::IExecutor {
private:
    std::unique_ptr<ScopedLibrary> current_library_;
    std::unordered_map<std::string, std::any> variables_;
    mutable std::shared_mutex variables_mutex_;

public:
    std::expected<interfaces::ExecutionResult, interfaces::ExecutionError>
    execute(const interfaces::CompilationResult& compilation_result) override {
        try {
            // Load the compiled library
            auto library = std::make_unique<ScopedLibrary>(compilation_result.library_path);
          
            if (!library->isValid()) {
                return std::unexpected(interfaces::ExecutionError::InvalidOperation);
            }
          
            // Look for exec function
            auto exec_fn = library->getSymbol<void()>("_Z4execv");
            if (!exec_fn.has_value()) {
                return std::unexpected(interfaces::ExecutionError::SymbolNotFound);
            }
          
            // Execute the function with timing
            auto start_time = std::chrono::high_resolution_clock::now();
          
            try {
                (*exec_fn.value())();
            } catch (const std::exception&) {
                return std::unexpected(interfaces::ExecutionError::RuntimeException);
            }
          
            auto end_time = std::chrono::high_resolution_clock::now();
            auto execution_time = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
          
            // Store the library for future symbol lookups
            current_library_ = std::move(library);
          
            interfaces::ExecutionResult result;
            result.execution_time = execution_time;
            result.success = true;
          
            return result;
          
        } catch (const std::exception&) {
            return std::unexpected(interfaces::ExecutionError::InternalError);
        }
    }

    std::expected<std::any, interfaces::ExecutionError>
    getVariable(const std::string& variable_name) override {
        std::shared_lock lock(variables_mutex_);
      
        auto it = variables_.find(variable_name);
        if (it != variables_.end()) {
            return it->second;
        }
      
        return std::unexpected(interfaces::ExecutionError::SymbolNotFound);
    }

    std::expected<void, interfaces::ExecutionError>
    setVariable(const std::string& variable_name, const std::any& value) override {
        std::unique_lock lock(variables_mutex_);
        variables_[variable_name] = value;
        return {};
    }

    std::vector<std::string> getAvailableVariables() const override {
        std::shared_lock lock(variables_mutex_);
        std::vector<std::string> names;
        names.reserve(variables_.size());
      
        for (const auto& [name, _] : variables_) {
            names.push_back(name);
        }
      
        return names;
    }

    void cleanup() override {
        std::unique_lock lock(variables_mutex_);
        current_library_.reset();
        variables_.clear();
    }
};

// Factory function implementations
std::unique_ptr<interfaces::ICompiler> createClangCompiler() {
    return std::make_unique<ClangCompiler>();
}

std::unique_ptr<interfaces::IExecutor> createDynamicExecutor() {
    return std::make_unique<DynamicExecutor>();
}

std::unique_ptr<interfaces::IReplContext> createReplContext() {
    return std::make_unique<ReplContext>();
}

} // namespace repl::implementation

namespace repl::interfaces {

// Factory function implementations
std::unique_ptr<ICompiler> createClangCompiler() {
    return implementation::createClangCompiler();
}

std::unique_ptr<IExecutor> createDynamicExecutor() {
    return implementation::createDynamicExecutor();
}

std::unique_ptr<IReplContext> createReplContext() {
    return implementation::createReplContext();
}

std::unique_ptr<IReplEngine> createReplEngine(
    std::unique_ptr<ICompiler> compiler,
    std::unique_ptr<IExecutor> executor,
    std::unique_ptr<IReplContext> context
) {
    // This would be implemented in the full refactoring
    // For now, returning nullptr to demonstrate the interface
    return nullptr;
}

} // namespace repl::interfaces