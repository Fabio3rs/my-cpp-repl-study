#pragma once

#include <filesystem>
#include <gtest/gtest.h>
#include <random>
#include <string>

namespace test_helpers {

/**
 * @brief RAII fixture for temporary directory management in tests
 *
 * Creates unique temporary directories that are automatically cleaned up
 * after tests complete. Thread-safe and exception-safe.
 */
class TempDirectoryFixture : public ::testing::Test {
  protected:
    void SetUp() override {
        // Generate unique temporary directory name
        auto random_suffix = generateRandomSuffix();
        temp_dir_ = std::filesystem::temp_directory_path() /
                    ("cpprepl_test_" + random_suffix);

        // Create directory and save old path
        old_path_ = std::filesystem::current_path();
        std::filesystem::create_directories(temp_dir_);
        std::filesystem::current_path(temp_dir_);
    }

    void TearDown() override {
        // Restore original directory and cleanup
        if (std::filesystem::exists(old_path_)) {
            std::filesystem::current_path(old_path_);
        }

        if (std::filesystem::exists(temp_dir_)) {
            std::error_code ec;
            std::filesystem::remove_all(temp_dir_, ec);
            // Ignore errors during cleanup in tests
        }
    }

    /**
     * @brief Get the temporary directory path
     * @return Path to temporary test directory
     */
    const std::filesystem::path &getTempDir() const { return temp_dir_; }

    /**
     * @brief Create a file in the temporary directory with content
     * @param filename Name of the file to create
     * @param content Content to write to the file
     * @return Full path to the created file
     */
    std::filesystem::path createFile(const std::string &filename,
                                     const std::string &content) {
        auto file_path = temp_dir_ / filename;
        std::ofstream file(file_path);
        file << content;
        file.close();
        return file_path;
    }

  private:
    std::filesystem::path temp_dir_;
    std::filesystem::path old_path_;

    std::string generateRandomSuffix() {
        const std::string chars = "abcdefghijklmnopqrstuvwxyz0123456789";
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, chars.size() - 1);

        std::string result;
        for (int i = 0; i < 8; ++i) {
            result += chars[dis(gen)];
        }
        return result;
    }
};

} // namespace test_helpers
