#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Forward declarations
struct VarDecl;

namespace execution {

/**
 * @brief Sistema de resolução dinâmica de símbolos usando trampolines
 *
 * Este módulo implementa um sistema sofisticado de lazy loading de símbolos
 * usando naked functions como trampolines. Na primeira chamada de uma função,
 * o trampoline resolve o símbolo real e atualiza o ponteiro, eliminando
 * overhead nas chamadas subsequentes.
 */
class SymbolResolver {
  public:
    /**
     * @brief Estrutura para manter informações de uma função wrapper
     */
    struct WrapperInfo {
        void *fnptr;
        void **wrap_ptrfn;
    };

    /**
     * @brief Configuração para geração de wrappers
     */
    struct WrapperConfig {
        std::string libraryPath;
        std::unordered_map<std::string, uintptr_t> symbolOffsets;
        std::unordered_map<std::string, WrapperInfo> functionWrappers;
    };

    /**
     * @brief Gera código C++ para um trampoline de função
     *
     * @param fnvars Declaração da função para gerar wrapper
     * @return String contendo código C++ do trampoline
     */
    static std::string generateFunctionWrapper(const VarDecl &fnvars);

    /**
     * @brief Prepara wrappers para um conjunto de funções
     *
     * @param name Nome base para o arquivo de wrapper
     * @param vars Lista de declarações de variáveis/funções
     * @param config Configuração de wrappers (será preenchida)
     * @param existingFunctions Funções já processadas (para evitar duplicação)
     * @return Mapa de nomes mangles para nomes de função
     */
    static std::unordered_map<std::string, std::string> prepareFunctionWrapper(
        const std::string &name, const std::vector<VarDecl> &vars,
        WrapperConfig &config,
        const std::unordered_set<std::string> &existingFunctions);

    /**
     * @brief Preenche ponteiros de wrapper após carregamento da biblioteca
     *
     * @param functions Mapa de nomes mangles para nomes de função
     * @param handlewp Handle da biblioteca wrapper
     * @param handle Handle da biblioteca principal
     * @param config Configuração de wrappers
     */
    static void fillWrapperPtrs(
        const std::unordered_map<std::string, std::string> &functions,
        void *handlewp, void *handle, WrapperConfig &config);

    /**
     * @brief Resolve offsets de símbolos a partir de arquivo de biblioteca
     *
     * @param functions Mapa de funções para resolver
     * @param libraryPath Caminho para a biblioteca
     * @return Mapa de símbolos para offsets
     */
    static std::unordered_map<std::string, uintptr_t>
    resolveSymbolOffsetsFromLibraryFile(
        const std::unordered_map<std::string, std::string> &functions,
        const std::string &libraryPath);

    /**
     * @brief Callback para resolução dinâmica de símbolos
     *
     * Esta função é chamada pelos trampolines quando precisam resolver
     * um símbolo na primeira execução.
     *
     * @param ptr Ponteiro para o ponteiro da função
     * @param name Nome do símbolo a resolver
     * @param config Configuração de wrappers
     */
    static void loadSymbolToPtr(void **ptr, const char *name,
                                const WrapperConfig &config);

    /**
     * @brief Define a configuração global para resolução de símbolos
     *
     * Esta função define a configuração global usada pelos trampolines.
     * Deve ser chamada antes de executar código que possa usar trampolines.
     *
     * @param config Ponteiro para a configuração (pode ser nullptr para limpar)
     */
    static void setGlobalWrapperConfig(WrapperConfig *config);
};

/**
 * @brief Callback C para resolução de símbolos (interface para assembly)
 *
 * Esta função serve como bridge entre o código assembly dos trampolines
 * e o sistema C++ de resolução de símbolos.
 */
extern "C" void loadfnToPtr(void **ptr, const char *name);

} // namespace execution
