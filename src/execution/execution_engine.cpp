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

SymbolResolver::WrapperConfig &GlobalExecutionState::getWrapperConfig() {
    std::shared_lock lock(stateMutex);
    return wrapperConfig;
}

void GlobalExecutionState::initializeWrapperConfig() {
    std::unique_lock lock(stateMutex);
    wrapperConfig.libraryPath = lastLibrary;
    wrapperConfig.symbolOffsets = symbolsToResolve;

    // Configurar como configuração global
    SymbolResolver::setGlobalWrapperConfig(&wrapperConfig);
}

// Singleton para estado global
GlobalExecutionState &getGlobalExecutionState() {
    static GlobalExecutionState instance;
    return instance;
}

} // namespace execution
