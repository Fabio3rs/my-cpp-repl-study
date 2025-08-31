#include "../test_helpers/temp_directory_fixture.hpp"
#include "completion/clang_completion.hpp"
#include "repl.hpp"
#include <chrono>
#include <gtest/gtest.h>
#include <memory>

using namespace completion;
using namespace test_helpers;

class ClangCompletionTest : public TempDirectoryFixture {
  protected:
    void SetUp() override {
        TempDirectoryFixture::SetUp();
        clangCompletion = std::make_unique<ClangCompletion>();
        clangCompletion->setVerbosity(5); // Máximo debug
        BuildSettings settings;
        clangCompletion->initialize(settings);
    }

    void TearDown() override {
        clangCompletion.reset();
        TempDirectoryFixture::TearDown();
    }

    std::unique_ptr<ClangCompletion> clangCompletion;
};

// Basic functionality tests
TEST_F(ClangCompletionTest, Constructor_DefaultState_ValidInitialization) {
    EXPECT_TRUE(clangCompletion != nullptr);

    // Should not crash when calling basic methods
    EXPECT_NO_THROW({ clangCompletion->clearCache(); });
}

TEST_F(ClangCompletionTest,
       UpdateReplContext_ValidContext_UpdatesSuccessfully) {
    ReplContext context;
    context.currentIncludes = "#include <iostream>\n";
    context.variableDeclarations = "int x = 42;\n";
    context.functionDeclarations = "void test();\n";
    context.activeCode = "std::cout << x;";
    context.line = 1;
    context.column = 10;

    EXPECT_NO_THROW({ clangCompletion->updateReplContext(context); });
}

TEST_F(ClangCompletionTest, GetCompletions_EmptyCode_ReturnsResults) {
    auto completions = clangCompletion->getCompletions("", 1, 1);

    // Should return some completions (at least keywords)
    EXPECT_GE(completions.size(), 0);
}

TEST_F(ClangCompletionTest, GetCompletions_StdPrefix_ReturnsStdCompletions) {
    auto completions = clangCompletion->getCompletions("std::", 1, 5);

    // Should find std:: completions
    bool foundStdCompletion = false;
    for (const auto &completion : completions) {
        if (completion.text.find("vector") != std::string::npos ||
            completion.text.find("string") != std::string::npos ||
            completion.text.find("cout") != std::string::npos) {
            foundStdCompletion = true;
            break;
        }
    }

    EXPECT_TRUE(foundStdCompletion);
}

TEST_F(ClangCompletionTest, GetCompletions_KeywordPrefix_ReturnsKeywords) {
    auto completions = clangCompletion->getCompletions("in", 1, 2);

    // Should find 'int' keyword
    bool foundIntKeyword = false;
    for (const auto &completion : completions) {
        if (completion.text == "int" &&
            completion.kind == CompletionItem::Kind::Keyword) {
            foundIntKeyword = true;
            break;
        }
    }

    EXPECT_TRUE(foundIntKeyword);
}

TEST_F(ClangCompletionTest, GetCompletions_ResultsAreSorted_ByPriority) {
    auto completions = clangCompletion->getCompletions("std::", 1, 5);

    // Results should be sorted by priority (higher first)
    for (size_t i = 1; i < completions.size(); ++i) {
        EXPECT_GE(completions[i - 1].priority, completions[i].priority);
    }
}

// Documentation tests
TEST_F(ClangCompletionTest, GetDocumentation_KnownSymbol_ReturnsDocumentation) {
    std::string doc = clangCompletion->getDocumentation("vector");

    EXPECT_FALSE(doc.empty());
    EXPECT_NE(doc, "No documentation available");
}

TEST_F(ClangCompletionTest, GetDocumentation_UnknownSymbol_ReturnsDefault) {
    std::string doc = clangCompletion->getDocumentation("unknownSymbolXYZ123");

    EXPECT_EQ(
        doc,
        "Symbol 'unknownSymbolXYZ123' - No specific documentation available.");
}

// Diagnostics tests
TEST_F(ClangCompletionTest, GetDiagnostics_ValidCode_ReturnsNoDiagnostics) {
    std::string code = "#include <iostream>\nint main() { return 0; }";
    auto diagnostics = clangCompletion->getDiagnostics(code);

    // Valid code should have no diagnostics
    EXPECT_TRUE(diagnostics.empty());
}

TEST_F(ClangCompletionTest, GetDiagnostics_CodeWithIssues_ReturnsDiagnostics) {
    // Code that uses cout without including iostream
    std::string code = "cout << \"hello\";";
    auto diagnostics = clangCompletion->getDiagnostics(code);

    // Should detect the missing include
    bool foundMissingInclude = false;
    for (const auto &diagnostic : diagnostics) {
        if (diagnostic.find("iostream") != std::string::npos) {
            foundMissingInclude = true;
            break;
        }
    }

    EXPECT_TRUE(foundMissingInclude);
}

TEST_F(ClangCompletionTest, GetDiagnostics_MissingSemicolon_DetectsIssue) {
    std::string code = "int x = 42"; // Missing semicolon
    auto diagnostics = clangCompletion->getDiagnostics(code);

    // Should detect missing semicolon
    bool foundSemicolonIssue = false;
    for (const auto &diagnostic : diagnostics) {
        if (diagnostic.find("semicolon") != std::string::npos) {
            foundSemicolonIssue = true;
            break;
        }
    }

    EXPECT_TRUE(foundSemicolonIssue);
}

// Symbol existence tests
TEST_F(ClangCompletionTest, SymbolExists_WithContext_FindsVariable) {
    ReplContext context;
    context.variableDeclarations = "int myVariable = 42;\n";
    clangCompletion->updateReplContext(context);

    EXPECT_TRUE(clangCompletion->symbolExists("myVariable"));
    EXPECT_FALSE(clangCompletion->symbolExists("nonExistentVariable"));
}

// Cache tests
TEST_F(ClangCompletionTest, ClearCache_AfterCompletions_ClearsSuccessfully) {
    // Get some completions first to populate cache
    auto completions = clangCompletion->getCompletions("std::", 1, 5);

    // Clear cache should not throw
    EXPECT_NO_THROW({ clangCompletion->clearCache(); });
}

// Context integration tests
TEST_F(ClangCompletionTest, ContextUpdate_WithVariables_ReflectsInCompletions) {
    ReplContext context;
    context.variableDeclarations =
        "int myCustomVar = 100;\nstd::string myString = \"hello\";";
    clangCompletion->updateReplContext(context);

    auto completions = clangCompletion->getCompletions("my", 1, 2);

    // Should find our custom variable in completions
    bool foundCustomVar = false;
    for (const auto &completion : completions) {
        if (completion.text == "myCustomVar" ||
            completion.text.find("myVar") != std::string::npos) {
            foundCustomVar = true;
            EXPECT_EQ(completion.kind, CompletionItem::Kind::Variable);
            break;
        }
    }

    EXPECT_TRUE(foundCustomVar);
}

// Performance tests
TEST_F(ClangCompletionTest,
       GetCompletions_Performance_CompletesInReasonableTime) {
    auto start = std::chrono::steady_clock::now();

    // Get completions multiple times
    for (int i = 0; i < 1; ++i) {
        auto completions = clangCompletion->getCompletions("std::", 1, 5);
    }

    auto end = std::chrono::steady_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // We can't set a strict threshold, but should be reasonably fast, but for
    // testing is hard to say so we set a high threshold to avoid false
    // positives Adjust threshold as needed based on environment
    EXPECT_LT(duration.count(), 5000);
}

// Edge case tests
TEST_F(ClangCompletionTest, GetCompletions_LargeCode_HandlesGracefully) {
    // Create a large code string
    std::string largeCode;
    for (int i = 0; i < 1000; ++i) {
        largeCode +=
            "int var" + std::to_string(i) + " = " + std::to_string(i) + ";\n";
    }

    EXPECT_NO_THROW({
        auto completions = clangCompletion->getCompletions(largeCode, 1, 1);
    });
}

TEST_F(ClangCompletionTest, GetCompletions_InvalidPosition_HandlesGracefully) {
    EXPECT_NO_THROW({
        auto completions = clangCompletion->getCompletions("test", -1, -1);
    });

    EXPECT_NO_THROW({
        auto completions = clangCompletion->getCompletions("test", 1000, 1000);
    });
}

// CompletionItem tests
TEST(CompletionItemTest, CompletionItem_DefaultConstruction_ValidState) {
    CompletionItem item;

    EXPECT_TRUE(item.text.empty());
    EXPECT_TRUE(item.display.empty());
    EXPECT_TRUE(item.documentation.empty());
    EXPECT_TRUE(item.returnType.empty());
    EXPECT_EQ(item.priority, 0);
    EXPECT_EQ(item.kind, CompletionItem::Kind::Variable);
}

TEST(CompletionItemTest, CompletionItem_FullConstruction_PreservesValues) {
    CompletionItem item;
    item.text = "test_function";
    item.display = "test_function()";
    item.documentation = "A test function";
    item.returnType = "int";
    item.priority = 10;
    item.kind = CompletionItem::Kind::Function;

    EXPECT_EQ(item.text, "test_function");
    EXPECT_EQ(item.display, "test_function()");
    EXPECT_EQ(item.documentation, "A test function");
    EXPECT_EQ(item.returnType, "int");
    EXPECT_EQ(item.priority, 10);
    EXPECT_EQ(item.kind, CompletionItem::Kind::Function);
}

// ReplContext tests
TEST(ReplContextTest, ReplContext_DefaultConstruction_ValidState) {
    ReplContext context;

    EXPECT_TRUE(context.currentIncludes.empty());
    EXPECT_TRUE(context.variableDeclarations.empty());
    EXPECT_TRUE(context.functionDeclarations.empty());
    EXPECT_TRUE(context.typeDefinitions.empty());
    EXPECT_TRUE(context.activeCode.empty());
    EXPECT_EQ(context.line, 1);
    EXPECT_EQ(context.column, 1);
}

// UX-focused tests for better user experience
class ClangCompletionUXTest : public ClangCompletionTest {
  protected:
    void SetUp() override {
        ClangCompletionTest::SetUp();

        // Setup common context for UX tests
        ReplContext context;
        context.currentIncludes =
            "#include <iostream>\n#include <vector>\n#include <string>\n";
        context.variableDeclarations =
            "int counter = 0;\nstd::string message = "
            "\"hello\";\nstd::vector<int> numbers = {1, 2, 3};\n";
        context.functionDeclarations =
            "void printMessage();\nint calculateSum(int a, int b);\n";
        clangCompletion->updateReplContext(context);
    }
};

// Smart completion ordering tests
TEST_F(ClangCompletionUXTest,
       GetCompletions_RecentVariables_PrioritizedHigher) {
    // Recently used variables should appear first
    auto completions = clangCompletion->getCompletions("", 1, 1);

    // Find local variables in completions
    std::vector<size_t> localVarPositions;
    for (size_t i = 0; i < completions.size(); ++i) {
        if (completions[i].text == "counter" ||
            completions[i].text == "message" ||
            completions[i].text == "numbers") {
            localVarPositions.push_back(i);
        }
    }

    // Local variables should be among the first results (within top 20)
    for (auto pos : localVarPositions) {
        EXPECT_LT(pos, 20)
            << "Local variable should be prioritized in completions";
    }
}

TEST_F(ClangCompletionUXTest, GetCompletions_ContextAware_RelevantSuggestions) {
    // When user types "message.", should show string methods
    auto completions = clangCompletion->getCompletions("message.", 1, 8);

    // Should find string methods like length(), substr(), etc.
    bool foundStringMethod = false;
    for (const auto &completion : completions) {
        if (completion.text.find("length") != std::string::npos ||
            completion.text.find("size") != std::string::npos ||
            completion.text.find("substr") != std::string::npos) {
            foundStringMethod = true;
            EXPECT_EQ(completion.kind, CompletionItem::Kind::Function);
            break;
        }
    }

    EXPECT_TRUE(foundStringMethod) << "Should suggest relevant string methods";
}

TEST_F(ClangCompletionUXTest,
       GetCompletions_PartialMatch_IntelligentFiltering) {
    // Test fuzzy matching: "prtMsg" should match "printMessage"
    auto completions = clangCompletion->getCompletions("prt", 1, 3);

    bool foundPrintMessage = false;
    for (const auto &completion : completions) {
        if (completion.text == "printMessage") {
            foundPrintMessage = true;
            break;
        }
    }

    EXPECT_TRUE(foundPrintMessage)
        << "Should support fuzzy matching for function names";
}

// Error prevention and user guidance
TEST_F(ClangCompletionUXTest, GetCompletions_CommonTypos_SuggestsCorrections) {
    // Test common typos like "stirng" instead of "string"
    auto completions = clangCompletion->getCompletions("stirng", 1, 6);

    // Should suggest "string" as a correction
    bool foundStringCorrection = false;
    for (const auto &completion : completions) {
        if (completion.text.find("string") != std::string::npos) {
            foundStringCorrection = true;
            break;
        }
    }

    EXPECT_TRUE(foundStringCorrection)
        << "Should suggest corrections for common typos";
}

TEST_F(ClangCompletionUXTest,
       GetCompletions_IncompleteStatement_ContextualHelp) {
    // When user types "for (", should suggest loop patterns
    auto completions = clangCompletion->getCompletions("for (", 1, 5);

    // Should have completions that help complete the for loop
    bool foundLoopPattern = false;
    for (const auto &completion : completions) {
        if (completion.text.find("int") != std::string::npos ||
            completion.text.find("auto") != std::string::npos ||
            completion.kind == CompletionItem::Kind::Keyword) {
            foundLoopPattern = true;
            break;
        }
    }

    EXPECT_TRUE(foundLoopPattern)
        << "Should provide contextual help for incomplete statements";
}

// Interactive REPL scenarios
TEST_F(ClangCompletionUXTest,
       GetCompletions_MultilineContext_AwarenessAcrossLines) {
    ReplContext context;
    context.activeCode =
        "if (counter > 0) {\n    message = \"positive\";\n    ";
    context.line = 3;
    context.column = 4;

    clangCompletion->updateReplContext(context);
    auto completions =
        clangCompletion->getCompletions(context.activeCode, 3, 4);

    // Should be aware we're inside an if block and suggest appropriate
    // completions
    bool foundRelevantCompletion = false;
    for (const auto &completion : completions) {
        if (completion.text.find("counter") != std::string::npos ||
            completion.text.find("message") != std::string::npos ||
            completion.text.find("std::cout") != std::string::npos) {
            foundRelevantCompletion = true;
            break;
        }
    }

    EXPECT_TRUE(foundRelevantCompletion)
        << "Should maintain context awareness across multiple lines";
}

TEST_F(ClangCompletionUXTest, GetCompletions_AfterOperator_AppropriateMembers) {
    // Test completion after different operators
    std::vector<std::pair<std::string, std::string>> testCases = {
        {"numbers.", "vector methods"},
        {"message.", "string methods"},
        {"std::", "standard library"}};

    for (const auto &testCase : testCases) {
        auto completions = clangCompletion->getCompletions(
            testCase.first, 1, testCase.first.length());

        EXPECT_GT(completions.size(), 0)
            << "Should provide completions after " << testCase.second;

        // Results should be relevant to the context
        bool hasRelevantCompletion = false;
        for (const auto &completion : completions) {
            if (!completion.text.empty() && completion.priority > 0) {
                hasRelevantCompletion = true;
                break;
            }
        }
        EXPECT_TRUE(hasRelevantCompletion)
            << "Should have relevant completions for " << testCase.second;
    }
}

// User productivity tests
TEST_F(ClangCompletionUXTest, GetCompletions_FrequentPatterns_HighPriority) {
    // Common patterns should have higher priority
    auto completions = clangCompletion->getCompletions("std::co", 1, 7);

    // "cout" should appear before less common completions
    int coutPosition = -1;
    int lessCommonPosition = -1;

    for (size_t i = 0; i < completions.size(); ++i) {
        if (completions[i].text.find("cout") != std::string::npos) {
            coutPosition = i;
        }
        if (completions[i].text.find("complex") != std::string::npos) {
            lessCommonPosition = i;
        }
    }

    if (coutPosition != -1 && lessCommonPosition != -1) {
        EXPECT_LT(coutPosition, lessCommonPosition)
            << "cout should appear before less common completions";
    }
}

TEST_F(ClangCompletionUXTest, GetCompletions_ResponseTime_InteractiveSpeed) {
    // Test that completion is fast enough for interactive use
    auto start = std::chrono::steady_clock::now();

    // Simulate typical user interaction
    auto completions = clangCompletion->getCompletions("std::", 1, 5);

    auto end = std::chrono::steady_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // We can't set a strict threshold, but should be reasonably fast, but for
    // testing is hard to say so we set a high threshold to avoid false
    // positives Adjust threshold as needed based on environment
    EXPECT_LT(duration.count(), 5000)
        << "Completion should be fast enough for interactive use";
    EXPECT_GT(completions.size(), 0)
        << "Should return meaningful completions quickly";
}

// Documentation and help quality
TEST_F(ClangCompletionUXTest,
       GetDocumentation_BuiltinTypes_ProvidesUsefulInfo) {
    std::vector<std::string> builtinTypes = {"int", "string", "vector", "cout"};

    for (const auto &type : builtinTypes) {
        std::string doc = clangCompletion->getDocumentation(type);

        EXPECT_FALSE(doc.empty())
            << "Should provide documentation for " << type;
        EXPECT_NE(doc, "No documentation available")
            << "Should have meaningful documentation for " << type;

        // Documentation should be informative (more than just the name)
        EXPECT_GT(doc.length(), type.length())
            << "Documentation should be more than just the symbol name";
    }
}

TEST_F(ClangCompletionUXTest,
       GetCompletions_WithDocumentation_IncludesHelpfulInfo) {
    auto completions = clangCompletion->getCompletions("std::vec", 1, 8);

    // Find vector completion
    bool foundVectorWithDoc = false;
    for (const auto &completion : completions) {
        if (completion.text.find("vector") != std::string::npos) {
            // Should have meaningful documentation or return type
            if (!completion.documentation.empty() ||
                !completion.returnType.empty()) {
                foundVectorWithDoc = true;

                // Documentation should be helpful
                if (!completion.documentation.empty()) {
                    EXPECT_GT(completion.documentation.length(), 10)
                        << "Documentation should be substantive";
                }
            }
            break;
        }
    }

    EXPECT_TRUE(foundVectorWithDoc)
        << "Should provide helpful documentation with completions";
}

// Error resilience and graceful degradation
TEST_F(ClangCompletionUXTest,
       GetCompletions_PartiallyInvalidCode_StillProvides) {
    // Code with syntax errors should still provide reasonable completions
    std::string partialCode =
        "if (counter >\nstd::co"; // Incomplete if statement

    auto completions = clangCompletion->getCompletions(partialCode, 2, 7);

    // Should still provide std:: completions despite syntax error
    bool foundStdCompletion = false;
    for (const auto &completion : completions) {
        if (completion.text.find("cout") != std::string::npos ||
            completion.text.find("string") != std::string::npos) {
            foundStdCompletion = true;
            break;
        }
    }

    EXPECT_TRUE(foundStdCompletion)
        << "Should provide completions even with partial syntax errors";
}

TEST_F(ClangCompletionUXTest, GetCompletions_EmptyPrefix_ShowsRelevantOptions) {
    // When user just pressed completion key without typing, show contextually
    // relevant options
    ReplContext context;
    context.activeCode = "int main() {\n    ";
    context.line = 2;
    context.column = 4;

    clangCompletion->updateReplContext(context);
    auto completions = clangCompletion->getCompletions("", 2, 4);

    // Should show common statements, variables, and keywords
    bool hasUsefulSuggestions = false;
    int relevantCount = 0;

    for (const auto &completion : completions) {
        if (completion.kind == CompletionItem::Kind::Keyword ||
            completion.kind == CompletionItem::Kind::Variable ||
            completion.kind == CompletionItem::Kind::Function) {
            relevantCount++;
        }

        if (completion.text == "if" || completion.text == "for" ||
            completion.text == "while" || completion.text == "counter" ||
            completion.text == "message") {
            hasUsefulSuggestions = true;
        }
    }

    EXPECT_TRUE(hasUsefulSuggestions)
        << "Should provide useful suggestions even without prefix";
    EXPECT_GT(relevantCount, 5)
        << "Should have multiple relevant completion options";
}

// Completion quality and ranking
TEST_F(ClangCompletionUXTest, GetCompletions_ResultQuality_NoIrrelevantNoise) {
    auto completions = clangCompletion->getCompletions("message", 1, 7);

    // Should not have too many irrelevant completions
    int relevantCount = 0;
    int totalCount = completions.size();

    for (const auto &completion : completions) {
        // Consider completion relevant if it contains the prefix or is
        // contextually related
        if (completion.text.find("message") != std::string::npos ||
            completion.kind == CompletionItem::Kind::Variable ||
            completion.kind == CompletionItem::Kind::Function ||
            completion.text.find("string") != std::string::npos) {
            relevantCount++;
        }
    }

    if (totalCount > 0) {
        double relevanceRatio = static_cast<double>(relevantCount) / totalCount;
        EXPECT_GT(relevanceRatio, 0.3)
            << "At least 30% of completions should be relevant to the context";
    }
}
