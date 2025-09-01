#pragma once

#include "../repl.hpp"
#include "symbol_resolver.hpp"
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace execution {
// Forward declaration
struct wrapperFn {
    void *fnptr;
    void **wrap_ptrfn;
};

// Estado global centralizado - OBRIGATÓRIO para dlopen/dlsym e assembly inline
struct GlobalExecutionState {
    // Estado compartilhado necessário para execução dinâmica
    std::string lastLibrary;
    std::unordered_map<std::string, uintptr_t> symbolsToResolve;
    std::unordered_map<std::string, wrapperFn> fnNames;

    std::unordered_map<std::string, std::string> existingFunctions;

    // Configuração global para resolução de símbolos via trampolines
    SymbolResolver::WrapperConfig wrapperConfig;

    // Counters globais
    int64_t replCounter = 0;
    int ctrlcounter = 0;

    // Thread safety para acesso concorrente
    mutable std::shared_mutex stateMutex;

    // Métodos helper para acesso thread-safe
    void setLastLibrary(const std::string &library);
    std::string getLastLibrary() const;
    void clearSymbolsToResolve();
    void addSymbolToResolve(const std::string &symbol, uintptr_t address);
    bool hasFnName(const std::string &mangledName) const;
    wrapperFn &getFnName(const std::string &mangledName);
    void setFnName(const std::string &mangledName, const wrapperFn &fn);

    // Métodos para gerenciar configuração de wrappers
    SymbolResolver::WrapperConfig &getWrapperConfig();
    void initializeWrapperConfig();

    friend GlobalExecutionState &getGlobalExecutionState();

  private:
    GlobalExecutionState() = default;
    ~GlobalExecutionState() = default;
    GlobalExecutionState(const GlobalExecutionState &) = delete;
    GlobalExecutionState &operator=(const GlobalExecutionState &) = delete;
    GlobalExecutionState(GlobalExecutionState &&) = delete;
    GlobalExecutionState &operator=(GlobalExecutionState &&) = delete;
};

// Acesso controlado ao estado global singleton
GlobalExecutionState &getGlobalExecutionState();

// Funções de execução que serão movidas do repl.cpp
auto prepareWrapperAndLoadCodeLib(const CompilerCodeCfg &cfg,
                                  std::vector<VarDecl> &&vars) -> EvalResult;

void fillWrapperPtrs(
    const std::unordered_map<std::string, std::string> &functions,
    void *handlewp, void *handle);

void resolveSymbolOffsetsFromLibraryFile(
    const std::unordered_map<std::string, std::string> &functions);

bool loadPrebuilt(const std::string &path);
void evalEverything();

// NOTA: A função extern "C" void loadfnToPtr está em repl.cpp
// para compatibilidade com assembly inline
} // namespace execution
