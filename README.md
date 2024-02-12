# C++ REPL study

This is a simple C++ REPL (Read-Eval-Print Loop) that I made to study C++ and some other things related to library loading and symbol resolution.

## Building
```
mkdir -p build
cd build
cmake ..
ninja # or make
```

## Running the REPL
```
cd build
./cpprepl
```

## Running the editor
```
cd editor
../build/cpprepl -r ../booteditor.txt
```
