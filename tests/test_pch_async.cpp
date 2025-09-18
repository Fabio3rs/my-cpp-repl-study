#include "repl.hpp"
#include <gtest/gtest.h>
#include <chrono>
#include <thread>

extern int verbosityLevel;

class PchAsyncTests : public ::testing::Test {
  protected:
    void SetUp() override {
        // ensure a clean environment
        verbosityLevel = 0;
        initRepl();
    }

    void TearDown() override {
        shutdownRepl();
    }
};

TEST_F(PchAsyncTests, SyncRebuildWhenDisabled) {
    // Disable async rebuilds and request an include that will mark PCH
    set_async_pch_rebuild(false);

    // Should process include and perform rebuild synchronously
    ASSERT_TRUE(extExecRepl(std::string_view("#include <vector>")));
}

TEST_F(PchAsyncTests, AsyncRebuildRequestedAndWaitable) {
    // Enable async rebuilds
    set_async_pch_rebuild(true);

    // Request include which should schedule an async rebuild
    ASSERT_TRUE(extExecRepl(std::string_view("#include <numeric>")));

    // Immediately call wait_for_pch_rebuild_if_running(); it should not
    // throw and should return after the async rebuild completes (or no-op
    // if the rebuild finished fast).
    wait_for_pch_rebuild_if_running();

    SUCCEED();
}
