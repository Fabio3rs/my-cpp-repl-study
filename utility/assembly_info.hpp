#pragma once
/*
 * This code was generated with the assistance of an AI language model
 * (ChatGPT). Please review and test the code thoroughly before using it in a
 * production environment. No guarantees are provided regarding the correctness,
 * performance, or security of the code.
 */

#include "../stdafx.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace assembly_info {
inline std::string executeCommand(const std::string &command) {
    std::ostringstream result;
    FILE *pipe = popen(command.c_str(), "r");
    if (!pipe) {
        std::cerr << "Error running command: " << command << std::endl;
        return "";
    }
    char buffer[128];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result << buffer;
    }
    pclose(pipe);
    return result.str();
}

inline std::string printSourceLine(const std::string &filePath, int lineNumber,
                                   int before, int after) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        return "Error: Could not open file " + filePath;
    }

    std::string result;

    std::string line;
    int currentLine = 0;
    while (std::getline(file, line)) {
        ++currentLine;
        if (currentLine < (lineNumber - before)) {
            continue;
        }
        if (currentLine > (lineNumber + after)) {
            break;
        }
        bool colored = currentLine == lineNumber;

        if (colored) {
            result += "\033[1;31m";
        }

        result += std::to_string(currentLine) + ": " + line + "\n";

        if (colored) {
            result += "\033[0m";
        }
    }

    return result;
}

inline std::string analyzeAddress(const std::string &binaryPath,
                                  uintptr_t address) {
    std::ostringstream output;

    // Get the instruction using gdb
    std::ostringstream gdbCommand;
    gdbCommand << "gdb --batch -ex 'file " << binaryPath << "' "
               << "-ex 'x/i 0x" << std::hex << address << "' -ex 'quit'";
    output << "Instruction:\n" << executeCommand(gdbCommand.str()) << "\n";

    // Get the source line using addr2line
    std::ostringstream addr2lineCommand;
    addr2lineCommand << "addr2line -e " << binaryPath << " " << std::hex
                     << address;
    std::string sourceFile = executeCommand(addr2lineCommand.str());
    output << "Source:\n" << sourceFile << "\n";

    size_t colon = sourceFile.find(':');
    std::string file = sourceFile.substr(0, colon);
    int line = colon == std::string::npos
                   ? 0
                   : std::stoi(sourceFile.substr(colon + 1));

    output << printSourceLine(file, line, 5, 5);
    output << "\n";

    return output.str();
}

inline std::string getInstructionAndSource(pid_t pid, uintptr_t address) {
    std::ostringstream output;
    std::ostringstream mapsPath;
    mapsPath << "/proc/" << pid << "/maps";

    std::ifstream mapsFile(mapsPath.str());
    if (!mapsFile.is_open()) {
        std::cerr << "Failed to open maps file: " << mapsPath.str()
                  << std::endl;
        return "";
    }

    std::string line;
    uintptr_t baseAddress = 0;
    uintptr_t baseOffset = 0;
    std::string binaryPath;
    while (std::getline(mapsFile, line)) {
        std::istringstream iss(line);
        std::string range, perms, offset, dev, inode, path;
        iss >> range >> perms >> offset >> dev >> inode;
        std::getline(iss, path);

        if (path.find('/') != std::string::npos) {
            std::size_t dash = range.find('-');
            uintptr_t start = std::stoul(range.substr(0, dash), nullptr, 16);
            uintptr_t end = std::stoul(range.substr(dash + 1), nullptr, 16);

            if (address >= start && address < end) {
                baseAddress = start;
                binaryPath = path;
                baseOffset = std::stoul(offset, nullptr, 16);
                break;
            }
        }
    }
    mapsFile.close();

    if (binaryPath.empty()) {
        std::cerr << "Failed to locate the binary containing address."
                  << std::endl;
        return "";
    }

    uintptr_t offset = address - baseAddress;
    offset += baseOffset;
    std::cout << "Calculated offset: " << std::hex << offset << std::endl;

    output << analyzeAddress(binaryPath, offset) << "\n";

    return output.str();
}
} // namespace assembly_info
