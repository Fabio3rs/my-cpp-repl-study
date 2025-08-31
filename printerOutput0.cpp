#include "printerOutput.hpp"


#include "decl_amalgama.hpp"


extern "C" void printvar_large_vec() {
  printdata(large_vec, "large_vec", "std::vector<int>");
}
void printall() {
printdata(large_vec, "large_vec", "std::vector<int>");
}
