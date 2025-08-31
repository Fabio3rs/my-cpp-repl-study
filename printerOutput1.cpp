#include "printerOutput.hpp"


#include "decl_amalgama.hpp"


extern "C" void printvar_test_str() {
  printdata(test_str, "test_str", "std::string");
}
void printall() {
printdata(test_str, "test_str", "std::string");
}
