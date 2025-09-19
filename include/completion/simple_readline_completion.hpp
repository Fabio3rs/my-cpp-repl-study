#pragma once

#include <readline/history.h>
#include <readline/readline.h>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

// Forward declarations
struct VarDecl;
struct ReplState;

namespace completion {

/**
 * @brief Sistema de completion simples usando dados do REPL
 */
class SimpleReadlineCompletion {
  public:
    SimpleReadlineCompletion() noexcept = default;
    ~SimpleReadlineCompletion() noexcept = default;

    // Non-copyable, movable
    SimpleReadlineCompletion(const SimpleReadlineCompletion &) = delete;
    SimpleReadlineCompletion &
    operator=(const SimpleReadlineCompletion &) = delete;
    SimpleReadlineCompletion(SimpleReadlineCompletion &&) noexcept = default;
    SimpleReadlineCompletion &
    operator=(SimpleReadlineCompletion &&) noexcept = default;

    // Core interface
    void initialize() noexcept;
    void shutdown() noexcept;

    // Context management from REPL state
    void updateFromReplState(const ReplState &state) noexcept;

    // Query interface
    [[nodiscard]] std::vector<std::string>
    getCompletions(std::string_view prefix) const noexcept;

  private:
    std::unordered_set<std::string> variables_;
    std::unordered_set<std::string> functions_;
    std::unordered_set<std::string> keywords_;
    std::unordered_set<std::string> replCommands_;

    void addBuiltinKeywords() noexcept;
    void addReplCommands() noexcept;

    // Static callbacks for readline
    static char **completionFunction(const char *text, int start,
                                     int end) noexcept;
    // Generator used by rl_completion_matches
    static char *completion_generator(const char *text, int state) noexcept;

    // Static state for callbacks
    static SimpleReadlineCompletion *activeInstance_;
    static std::vector<std::string> currentMatches_;
};

/**
 * @brief RAII wrapper para gerenciar lifetime do completion
 */
class SimpleCompletionScope {
  public:
    explicit SimpleCompletionScope(const ReplState &replState) noexcept;
    ~SimpleCompletionScope() noexcept;

    // Non-copyable, movable
    SimpleCompletionScope(const SimpleCompletionScope &) = delete;
    SimpleCompletionScope &operator=(const SimpleCompletionScope &) = delete;
    SimpleCompletionScope(SimpleCompletionScope &&) noexcept = default;
    SimpleCompletionScope &
    operator=(SimpleCompletionScope &&) noexcept = default;

    SimpleReadlineCompletion &get() noexcept { return completion_; }
    void updateContext(const ReplState &state) noexcept;

  private:
    SimpleReadlineCompletion completion_;
};

} // namespace completion
