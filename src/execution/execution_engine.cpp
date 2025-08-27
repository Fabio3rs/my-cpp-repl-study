#include "../include/execution/execution_engine.hpp"
#include <dlfcn.h>
#include <format>
#include <iostream>

namespace execution {

// Implementação dos métodos helper da GlobalExecutionState
void GlobalExecutionState::setLastLibrary(const std::string &library) {
    std::unique_lock lock(stateMutex);
    lastLibrary = library;
}

std::string GlobalExecutionState::getLastLibrary() const {
    std::shared_lock lock(stateMutex);
    return lastLibrary;
}

void GlobalExecutionState::clearSymbolsToResolve() {
    std::unique_lock lock(stateMutex);
    symbolsToResolve.clear();
}

void GlobalExecutionState::addSymbolToResolve(const std::string &symbol,
                                              uintptr_t address) {
    std::unique_lock lock(stateMutex);
    symbolsToResolve[symbol] = address;
}

bool GlobalExecutionState::hasFnName(const std::string &mangledName) const {
    std::shared_lock lock(stateMutex);
    return fnNames.contains(mangledName);
}

wrapperFn &GlobalExecutionState::getFnName(const std::string &mangledName) {
    std::unique_lock lock(stateMutex);
    return fnNames[mangledName];
}

void GlobalExecutionState::setFnName(const std::string &mangledName,
                                     const wrapperFn &fn) {
    std::unique_lock lock(stateMutex);
    fnNames[mangledName] = fn;
}

// Singleton para estado global
GlobalExecutionState &getGlobalExecutionState() {
    static GlobalExecutionState instance;
    return instance;
}

// FUNÇÃO GLOBAL OBRIGATÓRIA para compatibilidade com assembly inline
// NOTA: A implementação real está em repl.cpp como redirecionamento
// Esta declaração é apenas para consistência do header
// extern "C" void loadfnToPtr(void **ptr, const char *name);

void resolveSymbolOffsetsFromLibraryFile(
    const std::unordered_map<std::string, std::string> &functions) {
    if (functions.empty()) {
        return;
    }

    // TODO: Implementar resolução de símbolos
    // Por enquanto, stub para compilação
    (void)functions; // Evitar warning
}

void evalEverything() {
    // TODO: Mover implementação de replState.lazyEvalFns
    // Por enquanto, placeholder
}

// TODO: Implementar outras funções movidas do repl.cpp
// prepareWrapperAndLoadCodeLib, fillWrapperPtrs, loadPrebuilt
// Essas serão implementadas nas próximas etapas

} // namespace execution
