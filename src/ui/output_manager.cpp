#include "ui/output_manager.hpp"
#include <algorithm>
#include <iostream>
#include <ncurses.h>
#include <readline/history.h>
#include <readline/readline.h>

namespace repl {
namespace ui {

// ============================================================================
// OutputManager Implementation
// ============================================================================

OutputManager::~OutputManager() {
    if (ncurses_active_) {
        disable_ncurses_mode();
    }
}

void OutputManager::enable_ncurses_mode() {
    if (!ncurses_active_) {
        // Salvar referências originais
        original_cout_ = std::cout.rdbuf();
        original_cerr_ = std::cerr.rdbuf();

        // Redirecionar para nossos buffers
        std::cout.rdbuf(cout_buffer_.rdbuf());
        std::cerr.rdbuf(cerr_buffer_.rdbuf());

        ncurses_active_ = true;
    }
}

void OutputManager::disable_ncurses_mode() {
    if (ncurses_active_) {
        restore_streams();
        flush_to_terminal();
        ncurses_active_ = false;
    }
}

std::string OutputManager::get_cout_content() {
    std::string content = cout_buffer_.str();
    cout_buffer_.str(""); // Clear buffer
    cout_buffer_.clear(); // Clear state flags
    return content;
}

std::string OutputManager::get_cerr_content() {
    std::string content = cerr_buffer_.str();
    cerr_buffer_.str("");
    cerr_buffer_.clear();
    return content;
}

void OutputManager::clear_buffers() {
    cout_buffer_.str("");
    cout_buffer_.clear();
    cerr_buffer_.str("");
    cerr_buffer_.clear();
}

void OutputManager::flush_to_terminal() {
    if (original_cout_ && original_cerr_) {
        std::string cout_content = cout_buffer_.str();
        std::string cerr_content = cerr_buffer_.str();

        if (!cout_content.empty()) {
            std::cout.rdbuf(original_cout_);
            std::cout << cout_content;
            std::cout.rdbuf(cout_buffer_.rdbuf());
        }

        if (!cerr_content.empty()) {
            std::cerr.rdbuf(original_cerr_);
            std::cerr << cerr_content;
            std::cerr.rdbuf(cerr_buffer_.rdbuf());
        }

        clear_buffers();
    }
}

void OutputManager::restore_streams() {
    if (original_cout_) {
        std::cout.rdbuf(original_cout_);
        original_cout_ = nullptr;
    }
    if (original_cerr_) {
        std::cerr.rdbuf(original_cerr_);
        original_cerr_ = nullptr;
    }
}

// ============================================================================
// InterfaceManager Implementation
// ============================================================================

InterfaceManager::InterfaceManager()
    : output_mgr_(std::make_unique<OutputManager>()) {}

InterfaceManager::~InterfaceManager() {
    if (current_mode_ != InterfaceMode::READLINE_ONLY) {
        teardown_ncurses_interface();
    }
}

void InterfaceManager::set_mode(InterfaceMode mode) {
    if (mode == current_mode_) {
        return; // Já no modo desejado
    }

    // Teardown modo anterior
    switch (current_mode_) {
    case InterfaceMode::NCURSES_BASIC:
    case InterfaceMode::NCURSES_ADVANCED:
        teardown_ncurses_interface();
        break;
    case InterfaceMode::READLINE_ONLY:
        // Nada a fazer
        break;
    }

    // Setup novo modo
    switch (mode) {
    case InterfaceMode::READLINE_ONLY:
        output_mgr_->disable_ncurses_mode();
        break;
    case InterfaceMode::NCURSES_BASIC:
    case InterfaceMode::NCURSES_ADVANCED:
        setup_ncurses_interface();
        break;
    }

    current_mode_ = mode;
}

std::string InterfaceManager::get_user_input(const std::string &prompt) {
    switch (current_mode_) {
    case InterfaceMode::READLINE_ONLY:
        return get_readline_input(prompt);
    case InterfaceMode::NCURSES_BASIC:
    case InterfaceMode::NCURSES_ADVANCED:
        return get_ncurses_input(prompt);
    }
    return "";
}

void InterfaceManager::display_output(const std::string &text, bool is_error) {
    switch (current_mode_) {
    case InterfaceMode::READLINE_ONLY:
        display_readline_output(text, is_error);
        break;
    case InterfaceMode::NCURSES_BASIC:
    case InterfaceMode::NCURSES_ADVANCED:
        display_ncurses_output(text, is_error);
        break;
    }
}

void InterfaceManager::show_diagnostics(
    const std::vector<std::string> &diagnostics) {
    // Implementação básica - pode ser expandida
    for (const auto &diag : diagnostics) {
        display_output("⚠️  " + diag, true);
    }
}

// ============================================================================
// Private Methods
// ============================================================================

void InterfaceManager::setup_ncurses_interface() {
    output_mgr_->enable_ncurses_mode();

    // Inicializar ncurses
    initscr();
    noecho();
    cbreak();
    keypad(stdscr, TRUE);

    // Setup cores se suportadas
    if (has_colors()) {
        start_color();
        init_pair(1, COLOR_RED, COLOR_BLACK);    // Erro
        init_pair(2, COLOR_GREEN, COLOR_BLACK);  // Sucesso
        init_pair(3, COLOR_YELLOW, COLOR_BLACK); // Warning
    }
}

void InterfaceManager::teardown_ncurses_interface() {
    if (current_mode_ != InterfaceMode::READLINE_ONLY) {
        endwin();
        output_mgr_->disable_ncurses_mode();
    }
}

std::string InterfaceManager::get_readline_input(const std::string &prompt) {
    char *input = readline(prompt.c_str());
    if (input == nullptr) {
        return "";
    }

    std::string result(input);

    // Adicionar ao histórico se não vazio
    if (!result.empty() &&
        !std::all_of(result.begin(), result.end(), ::isspace)) {
        add_history(input);
    }

    free(input);
    return result;
}

std::string InterfaceManager::get_ncurses_input(const std::string &prompt) {
    // Implementação básica ncurses - pode ser expandida
    mvprintw(LINES - 1, 0, "%s", prompt.c_str());
    refresh();

    char buffer[1024] = {0};
    getnstr(buffer, sizeof(buffer) - 1);

    return std::string(buffer);
}

void InterfaceManager::display_readline_output(const std::string &text,
                                               bool is_error) {
    if (is_error) {
        std::cerr << text;
    } else {
        std::cout << text;
    }
}

void InterfaceManager::display_ncurses_output(const std::string &text,
                                              bool is_error) {
    if (is_error && has_colors()) {
        attron(COLOR_PAIR(1)); // Vermelho para erro
    }

    printw("%s", text.c_str());

    if (is_error && has_colors()) {
        attroff(COLOR_PAIR(1));
    }

    refresh();
}

} // namespace ui
} // namespace repl
