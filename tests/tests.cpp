#include <any>
#include <gtest/gtest.h>

#include "repl.hpp"

TEST(FirstTest, BasicGlobalAssignment) {
    initRepl();
    std::string_view cmd = "int a = 5;";
    ASSERT_TRUE(extExecRepl(cmd));
    ASSERT_EQ(5, std::any_cast<int>(getResultRepl("a")));
}
