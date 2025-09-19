# Instruções contratuais para o GitHub Copilot — domínio **CPPREPL** (C++20/23)

**Objetivo**: Copilot deve gerar/editar código **C++ de alta qualidade**, com foco em segurança, legibilidade e testabilidade. Priorize as **C++ Core Guidelines** e as **NASA Power of Ten (P10)**. No domínio do REPL, **evite macros**, exceto as explicitamente necessárias para integrações (como captura de `SIGSEGV` em *segv-catch*).

> **Se houver conflito entre histórico do repositório e estas instruções, siga este documento.**

---

## 0) Padrões de compilação e ferramentas
- Código-alvo: **C++20/23**.
- Extensões GNU são permitidas, mas **não são mandatórias**; use alternativas padrão sempre que possível.
- O código deve compilar limpo com `-Wall -Wextra -Wpedantic` (ou `/W4`).
- Compatível com `clang-tidy`, especialmente os perfis `cppcoreguidelines-*`, `bugprone-*`, `performance-*`, `readability-*`.
- Utilize preferencialmente cabeçalhos da STL.

---

## 1) Regras essenciais (C++ Core Guidelines)
1. **RAII e ownership claro**
   - Use `std::unique_ptr` para propriedade exclusiva; `std::shared_ptr` apenas quando estritamente necessário.
   - Evite `new/delete` diretos em código de alto nível. Use `std::make_unique()` ou `std::make_shared()`.

2. **Evite arrays brutos ou `(ptr, len)`**
   - Prefira `std::span<T>`/`std::string_view` para *views* e `std::vector<T>` / `std::array<T,N>` para armazenamento.

3. **Const-correção rigorosa**
   - Marque como `const` tudo que não deve mutar, incluindo métodos que não alteram estado.

4. **Inicialização uniforme e segura**
   - Utilize inicialização uniforme (`T x{...}`) e inicializadores de membro (DMIs).

5. **Interfaces coesas e funções curtas**
   - Limite funções a ~80 linhas; cada função deve ter responsabilidade clara.

6. **Erros, contratos e guard clauses**
   - Use `[[nodiscard]]` em retornos significativos.
   - Prefira `std::expected<T,E>` (ou equivalente) para erros recuperáveis; exceções apenas para casos excepcionais.
   - **Use guard clauses / early return**: verifique condições inválidas logo no início e retorne cedo. Evite nesting e `else` desnecessário.

7. **Use `noexcept` onde aplicável**
   - Marque funções como `noexcept` se realmente não lançam exceções.

8. **Use algoritmos e ranges da STL**
   - Evite `for(i...)`; prefira `for (auto& e : range)` ou algoritmos `std::ranges`.

9. **Evite estado global**
   - Se indispensável, encapsule-o em classes sincronizadas com interfaces limitadas.

---

## 2) Regras NASA Power of Ten (P10) adaptadas
1. Evite `goto` e recursão profunda.
2. Loops devem ter limites claros e verificáveis.
3. Não use alocação dinâmica após inicialização do subsistema crítico.
4. Funções devem caber em uma página (~60 linhas).
5. Use assertivas frequentes para validar invariantes.
6. Declare variáveis no escopo mínimo possível.
7. Cheque todos os valores de retorno; nunca ignore sem justificativa.
8. Limite o uso do pré-processador; evite macros complexas.
9. Restrinja o uso de ponteiros e indireção; evite múltiplos níveis.
10. Compile sempre com o máximo de warnings habilitados e trate-os como erros.

---

## 3) Especificidades do domínio **CPPREPL**
- **Macros restritos** — Use apenas os necessários para integração com *segv-catch*. Todos os outros devem ser evitados; justificativa clara obrigatória.
- **Trampolines por arquitetura** — Separe os wrappers `naked` por arquitetura (e.g., `x86_64/`, `aarch64/`) com testes de interface ABI; evite *inline asm* em código de alto nível.
- **Evite alocação no loop de REPL** — Prefira `std::string_view`, `std::span`, `reserve`, buffers estáticos.
- **Limites explícitos** — Defina número máximo de includes ou comandos por avaliação; timeouts claros por operação do REPL.

---

## 4) Estilo de código
- **Nomes**:
  - `snake_case` para funções e variáveis locais
  - `PascalCase` para tipos
  - `SCREAMING_SNAKE_CASE` para constantes ou macros inevitáveis
- **Headers**: Use `#pragma once`; evite dependências cíclicas; um cabeçalho por unidade.
- **Namespaces**: Evite poluir o global; use `namespace repl { … }`.
- **Includes**: Ordem: local → projeto → terceiros → STL; um por linha.
- **Formatação**: Compatível com *clang-format* (LLVM style, largura 100, `{` na mesma linha, espaços após vírgula).

### Blocos de controle — sempre usar chaves
Por questão de legibilidade e para evitar bugs sutis em manutenções futuras, sempre emita chaves mesmo para blocos de controle com uma única instrução. Exemplo preferido:

```cpp
if (a) {
   foo();
}
```
Evite a forma sem chaves:

```cpp
if (a)
   foo();
```
Esta regra reduz erros quando linhas são adicionadas posteriormente e melhora a consistência do código.

---

## 5) Erros e logging (modelo)
- Use `std::expected<T, Error>` ou equivalente para tratar erros.
- Exceções apenas quando indispensável.
- Estruture `Error` com código e mensagem curta.
- Logging estruturado com identificadores de contexto.

```cpp
struct Error { int code; std::string_view msg; };
using Result = std::expected<Payload, Error>;

[[nodiscard]] Result run_snippet(std::string_view code) noexcept;

if (auto r = run_snippet(src); !r) {
  log_warn("run_snippet failed: {}", r.error().msg);
  return make_fail(r.error());
}
return consume(*r);
```

---

## 6) Concorrência, desempenho e segurança

* Evite *data races*: use `std::mutex`, `std::atomic`.
* Minimize cópias; passe por `const&` ou `span`, mova quando possível.
* Garanta *strong exception safety* em operações críticas.
* Não introduza comportamento indefinido; mantenha sanitizers ativos.

---

## 7) Testes

* Testes unitários obrigatórios para código novo ou alterado.
* Testes de integração para comandos do REPL, sinais e trampolines.
* Testes determinísticos; evite dependência de rede ou relógio.
* Execute testes sempre com sanitizers ativos.

---

## 8) Pré-processador e extensões GNU

* Evite macros; quando necessárias, documente com `// GNU EXTENSION: rationale` e forneça alternativa em C++.
* Prefira `__VA_OPT__` a hacks com `##__VA_ARGS__`.
* Não use `typeof`; prefira `decltype`.
* Não use *statement-expr* fora de código de compatibilidade; use lambdas IIFE.

---

## 9) O que NÃO gerar

* Código que ignora valores de retorno ou erros.
* Funções longas ou complexas sem refatoração.
* Alocação dinâmica em caminhos críticos do REPL.
* Macros para controle de fluxo.

---

## 10) TL;DR para o Copilot

* **Prefira**: RAII, `span`/`string_view`, `[[nodiscard]]`, `noexcept`, early returns, guard clauses, logs estruturados, sempre emita chaves em blocos de controle.
* **Evite**: macros (exceto integração de sinais), alocação em caminhos críticos, ponteiros crus profundos, `goto` ou recursão desnecessária.
* **Exija**: warnings zero, clang-tidy verde, testes com sanitizers.

