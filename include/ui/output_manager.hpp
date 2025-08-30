#pragma once

#include <iostream>
#include <memory>
#include <sstream>

namespace repl {
namespace ui {

/**
 * @brief Gerencia conflict entre stdout/stderr e ncurses
 *
 * Esta classe resolve o problema fundamental de incompatibilidade
 * entre output streams normais (cout/cerr) e ncurses, permitindo
 * que o REPL tenha modos de interface diferentes.
 */
class OutputManager {
  private:
    std::streambuf *original_cout_;
    std::streambuf *original_cerr_;
    std::ostringstream cout_buffer_;
    std::ostringstream cerr_buffer_;
    bool ncurses_active_{false};

  public:
    OutputManager() = default;
    ~OutputManager();

    // Desabilita cópia para evitar problemas com stream redirection
    OutputManager(const OutputManager &) = delete;
    OutputManager &operator=(const OutputManager &) = delete;

    /**
     * @brief Ativa modo ncurses - redireciona cout/cerr para buffers
     */
    void enable_ncurses_mode();

    /**
     * @brief Desativa modo ncurses - restaura cout/cerr originais
     */
    void disable_ncurses_mode();

    /**
     * @brief Verifica se está em modo ncurses
     */
    bool is_ncurses_active() const noexcept { return ncurses_active_; }

    /**
     * @brief Obtém conteúdo capturado do cout
     */
    std::string get_cout_content();

    /**
     * @brief Obtém conteúdo capturado do cerr
     */
    std::string get_cerr_content();

    /**
     * @brief Limpa buffers internos
     */
    void clear_buffers();

    /**
     * @brief Flush buffers para terminal (quando não em modo ncurses)
     */
    void flush_to_terminal();

  private:
    void restore_streams();
};

/**
 * @brief Enum para diferentes modos de interface do REPL
 */
enum class InterfaceMode {
    READLINE_ONLY,   // Modo atual - readline + stdout
    NCURSES_BASIC,   // Ncurses simples - diagnostics pós-entrada
    NCURSES_ADVANCED // Ncurses completo - real-time features
};

/**
 * @brief Gerenciador principal da interface do usuário
 *
 * Coordena entre diferentes modos de interface, permitindo
 * transição suave entre readline simples e ncurses avançado.
 */
class InterfaceManager {
  private:
    InterfaceMode current_mode_{InterfaceMode::READLINE_ONLY};
    std::unique_ptr<OutputManager> output_mgr_;

  public:
    InterfaceManager();
    ~InterfaceManager();

    /**
     * @brief Define o modo de interface atual
     */
    void set_mode(InterfaceMode mode);

    /**
     * @brief Obtém o modo atual
     */
    InterfaceMode get_mode() const noexcept { return current_mode_; }

    /**
     * @brief Obtém input do usuário (compatível com diferentes modos)
     */
    std::string get_user_input(const std::string &prompt);

    /**
     * @brief Mostra output de forma compatível com o modo atual
     */
    void display_output(const std::string &text, bool is_error = false);

    /**
     * @brief Mostra diagnostics de acordo com o modo
     */
    void show_diagnostics(const std::vector<std::string> &diagnostics);

  private:
    void setup_ncurses_interface();
    void teardown_ncurses_interface();

    // Implementações específicas por modo
    std::string get_readline_input(const std::string &prompt);
    std::string get_ncurses_input(const std::string &prompt);

    void display_readline_output(const std::string &text, bool is_error);
    void display_ncurses_output(const std::string &text, bool is_error);
};

} // namespace ui
} // namespace repl
