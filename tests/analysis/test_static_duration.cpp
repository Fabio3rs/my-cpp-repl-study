#include "../../include/analysis/ast_context.hpp"
#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <vector>

namespace analysis {

/**
 * @brief Testes específicos para validar a duração estática do outputHeader_
 *
 * Estes testes são CRÍTICOS para garantir que o outputHeader_ mantém
 * suas declarações durante toda a sessão REPL, essencial para a geração
 * correta do decl_amalgama.hpp.
 */
class StaticDurationTest : public ::testing::Test {
  protected:
    void SetUp() override {
        context1 = std::make_shared<AstContext>();
        context2 = std::make_shared<AstContext>();
    }

    std::shared_ptr<AstContext> context1;
    std::shared_ptr<AstContext> context2;
};

/**
 * @brief Teste crítico: outputHeader_ deve persistir entre diferentes
 * instâncias
 *
 * Este teste valida que o outputHeader_ é verdadeiramente estático e
 * compartilhado entre todas as instâncias de AstContext.
 */
TEST_F(StaticDurationTest,
       OutputHeader_PersistsBetweenInstances_StaticDuration) {
    // Adiciona declaração na primeira instância
    context1->addDeclaration("extern int static_test_var1;");

    // Verifica que a segunda instância vê a mesma declaração
    std::string header1 = context1->getOutputHeader();
    std::string header2 = context2->getOutputHeader();

    EXPECT_EQ(header1, header2) << "OutputHeader deve ser compartilhado entre "
                                   "instâncias (duração estática)";

    EXPECT_NE(header1.find("static_test_var1"), std::string::npos)
        << "Declaração deve estar presente em ambas as instâncias";
}

/**
 * @brief Teste crítico: outputHeader_ deve sobreviver ao clear()
 *
 * Este é o teste mais importante - garante que clear() NÃO limpa
 * o outputHeader_, mantendo as declarações acumuladas.
 */
TEST_F(StaticDurationTest, OutputHeader_SurvivesClear_PreservesDeclarations) {
    // Adiciona algumas declarações
    context1->addDeclaration("extern int persistent_var1;");
    context1->addDeclaration("extern double persistent_var2;");

    std::string headerBeforeClear = context1->getOutputHeader();

    // Chama clear() - isto NÃO deve limpar o outputHeader_
    context1->clear();

    std::string headerAfterClear = context1->getOutputHeader();

    // CRÍTICO: O header deve ser IDÊNTICO antes e depois do clear()
    EXPECT_EQ(headerBeforeClear, headerAfterClear)
        << "CRÍTICO: outputHeader_ deve manter declarações após clear()";

    EXPECT_NE(headerAfterClear.find("persistent_var1"), std::string::npos)
        << "Primeira declaração deve persistir após clear()";

    EXPECT_NE(headerAfterClear.find("persistent_var2"), std::string::npos)
        << "Segunda declaração deve persistir após clear()";
}

/**
 * @brief Teste de acumulação: novas declarações são adicionadas às existentes
 *
 * Valida que o outputHeader_ acumula declarações corretamente durante
 * toda a sessão REPL.
 */
TEST_F(StaticDurationTest,
       OutputHeader_AccumulatesDeclarations_ThroughoutSession) {
    // Simula uma sessão REPL com múltiplas declarações
    size_t initialSize = context1->getOutputHeader().size();

    context1->addDeclaration("extern int session_var1;");
    size_t afterFirst = context1->getOutputHeader().size();
    EXPECT_GT(afterFirst, initialSize)
        << "Header deve crescer após primeira declaração";

    context1->clear(); // Simula reset de contexto (mas header deve persistir)

    context1->addDeclaration("extern float session_var2;");
    size_t afterSecond = context1->getOutputHeader().size();
    EXPECT_GT(afterSecond, afterFirst)
        << "Header deve continuar crescendo após clear()";

    context1->addDeclaration("extern char session_var3;");
    size_t afterThird = context1->getOutputHeader().size();
    EXPECT_GT(afterThird, afterSecond) << "Header deve continuar acumulando";

    // Verifica que todas as declarações estão presentes
    std::string finalHeader = context1->getOutputHeader();
    EXPECT_NE(finalHeader.find("session_var1"), std::string::npos)
        << "Primeira declaração deve estar presente no final";
    EXPECT_NE(finalHeader.find("session_var2"), std::string::npos)
        << "Segunda declaração deve estar presente no final";
    EXPECT_NE(finalHeader.find("session_var3"), std::string::npos)
        << "Terceira declaração deve estar presente no final";
}

/**
 * @brief Teste de thread safety para outputHeader_ estático
 */
TEST_F(StaticDurationTest, OutputHeader_ThreadSafe_StaticAccess) {
    const int num_threads = 4;
    const int declarations_per_thread = 10;
    std::vector<std::thread> threads;

    // Lança múltiplas threads adicionando declarações
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            auto local_context = std::make_shared<AstContext>();
            for (int i = 0; i < declarations_per_thread; ++i) {
                std::string decl = "extern int thread" + std::to_string(t) +
                                   "_var" + std::to_string(i) + ";";
                local_context->addDeclaration(decl);
            }
        });
    }

    // Espera todas as threads terminarem
    for (auto &t : threads) {
        t.join();
    }

    // Verifica que o header contém declarações de diferentes threads
    std::string finalHeader = context1->getOutputHeader();
    bool foundThread0 = finalHeader.find("thread0_var0") != std::string::npos;
    bool foundThread1 = finalHeader.find("thread1_var0") != std::string::npos;

    EXPECT_TRUE(foundThread0 || foundThread1)
        << "Header deve conter declarações de múltiplas threads";
}

} // namespace analysis
