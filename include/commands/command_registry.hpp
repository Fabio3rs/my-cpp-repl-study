#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace commands {

struct CommandContextBase {
    virtual ~CommandContextBase() = default;
};

using CommandHandler = std::function<bool(std::string_view, CommandContextBase &)>;

struct CommandEntry {
    std::string prefix;
    std::string description;
    CommandHandler handler;
};

class CommandRegistry {
public:
    void registerPrefix(std::string prefix, std::string description, CommandHandler handler) {
        entries_.push_back(CommandEntry{std::move(prefix), std::move(description), std::move(handler)});
    }

    bool tryHandle(std::string_view line, CommandContextBase &ctx) const {
        for (const auto &entry : entries_) {
            if (line.rfind(entry.prefix, 0) == 0) {
                auto rest = line.substr(entry.prefix.size());
                return entry.handler(rest, ctx);
            }
        }
        return false;
    }

    const std::vector<CommandEntry> &entries() const { return entries_; }

private:
    std::vector<CommandEntry> entries_;
};

inline CommandRegistry &registry() {
    static CommandRegistry instance;
    return instance;
}

template <typename Context>
struct BasicContext : public CommandContextBase {
    Context data;
    explicit BasicContext(Context d) : data(std::move(d)) {}
};

template <typename Context>
inline bool handleCommand(std::string_view line, Context &context) {
    BasicContext<Context> ctx(context);
    return registry().tryHandle(line, ctx);
}

} // namespace commands


