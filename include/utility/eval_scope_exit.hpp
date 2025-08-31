#pragma once

#include <functional>

struct EvalOnScopeExit {
    explicit EvalOnScopeExit(std::function<void()> func)
        : func_(std::move(func)) {}
    ~EvalOnScopeExit() noexcept {
        try {
            if (func_) {
                func_();
            }
        } catch (...) {
            // Evitar propagação de exceções no destrutor
        }
    }

    // Non-copyable, non-movable
    EvalOnScopeExit(const EvalOnScopeExit &) = delete;
    EvalOnScopeExit &operator=(const EvalOnScopeExit &) = delete;
    EvalOnScopeExit(EvalOnScopeExit &&) = delete;
    EvalOnScopeExit &operator=(EvalOnScopeExit &&) = delete;

  private:
    std::function<void()> func_;
};
