#pragma once

#include <any>
#include <string>
#include <string_view>
extern int
    __attribute__((visibility("default"))) (*bootstrapProgram)(int argc,
                                                               char **argv);

void initRepl();
void repl();
bool extExecRepl(std::string_view cmd);

std::any getResultRepl(std::string cmd);
