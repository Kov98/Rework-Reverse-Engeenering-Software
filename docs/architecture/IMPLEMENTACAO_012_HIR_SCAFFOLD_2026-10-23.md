# Rework — implementação 012: HIR scaffold v0

**Data:** 2026-10-23
**Estado:** CÓDIGO ESCRITO/INTEGRADO AOS ARQUIVOS; **NÃO COMPILADO, NÃO TESTADO, NÃO COMMITADO**, conforme solicitação do usuário.
**Branch:** `feat/reworkbench` no worktree `.worktrees/implementation`.
**Progresso editorial geral:** 12/100 (12%, provisório); anterior 11%. NÃO representa paridade medida com Ghidra.

## Missão realizada

Criar um esqueleto de representação intermediária de alto nível para evoluir no futuro, mas **não produzir um AST C/C++ ou estrutura de controle fictícia**. A primeira versão é uma adaptação estrutural, 1-para-1, da IR real existente, preservando IDs, relações de operandos, `Phi`, efeitos e CFG.

### Novos arquivos

- `native/workbench/include/reworkbench/hir.hpp`:
  - `HirValue`: identidade do nó IR original, operação tipada por bitvector/memória/estado, largura, status do tipo fonte, operandos, entradas `Phi` com predecessor, endereço da instrução, constante, proveniência do nó/bloco, classe de efeito e pistas `TypeEvidence`.
  - `HirBlock`: índice, entrada, definição de nós, predecessores/sucessores, terminador, possíveis endereços e blocos-alvo, condição e alvo de retorno.
  - `HirObservation`: versão `hir_scaffold_v0`, entrada CFG, nós sintéticos, contagem de referências inválidas, barreiras opacas, estado `partial/limited/report_complete`. `source_types_verified=false`, `structured_control_flow_verified=false`.
  - API `observe_function_hir(snapshot, function_index, stop)` sem exigir ABI.
- `native/workbench/src/hir_analysis.hpp`: contrato do adaptador read-only `hir_analysis::observe(program, type_evidence, stop)`.
- `native/workbench/src/hir_analysis.cpp`:
  - Traduz os enums reais de `Op`, `TypeKind`, `Terminator` para nomes estáveis no esquema HIR, sem converter em operações C.
  - Preserva os números originais de nós e operandos, fontes `Phi`, resultados de comparação, memória `Load/Store`, `Opaque/CallOpaque` e projeções `StateField`.
  - Marca `memory_read_may_fault`, `memory_write_may_fault`, `unknown_machine_state_barrier`, `unknown_call_state_barrier`, `opaque_state_projection` e `unknown_value`; nenhuma operação de memória é apagada ou reordenada.
  - Preserva estrutura do CFG bruto, sem declarar equivalência com `if/while/switch` da linguagem original. Destinos externos/não resolvidos continuam explícitos.
  - Usa `TypeEvidence` produzido a partir da MESMA IR, sem reutilizar tipos-fonte fictícios. `clues_incomplete=true` quando não há observação ou houve limite de pistas.
  - Cap de 8192 valores/768 blocos; se ultrapassar, não publica um grafo mutilado como íntegro. Cancelamento; captura de erro de orçamento da IR.
  - Verifica existência de referências a valores e índices de blocos durante construção (não é execução de testes nem validação semântica), assinalando incompletude.
  - Não altera `Program`, renderer, SSA original ou interface gráfica.

### Integração

- `native/workbench/CMakeLists.txt`: adiciona `src/hir_analysis.cpp` a `reworkbench_native`.
- `native/workbench/src/cli_main.cpp`: inclui API, `hir_json()` e comando `hir-observe`; aparece em `capabilities`, lista de comandos e ajuda.
- API do CLI: `hir-observe -> hir.version/semantic_status/values/blocks/entry_block/partial/limited/report_complete/warnings`.
- Visualização JSON limitada a 256 valores e 128 blocos, preservando contagens totais e `display_truncated`. Não significa perda silenciosa do resultado estrutural em memória.

### Uso previsto, ainda NÃO executado

```powershell
reworkbench-cli hir-observe --input exemplo.exe
```

A versão executável atualmente instalada não possui necessariamente este comando. **Não houve compilação, execução ou teste nesta rodada.**

## Limites explícitos

1. **Esta NÃO é ainda uma HIR semântica de fonte.** É a fundação tipada da transformação: uma representação auxiliar estrutural. `machine_ir_structural_only` explica essa diferença.
2. `TypeEvidence` é contextual; largura 64 não implica `int64_t` nem ponteiro-fonte. Não são inferidos `struct`, tipos de parâmetros nem classes C++.
3. `HirBlock.ordered_definitions` preserva a ordem listada pela IR; não demonstra ordem operacional completa dos efeitos sobre todos os caminhos.
4. `Phi` e CFG não são convertidos em laços, condições ou expressões de alto nível; isso exige dominadores, laços, irreducibilidade, tratamento de exceções e pass de reestruturação com provas.
5. Calls continuam opacas e podem alterar máquina/memória; não autoriza DSE, CSE, constant folding global nem preservação ABI sem validação.
6. Análise x86/x64 está limitada pelo lifter subjacente; ISAs e formatos futuros exigirão frontends próprios.
7. Incrementos **002–012** ainda não passaram por compilação/regressões conjuntas. O código salvo pode conter incompatibilidades C++/Qt, performance e falhas lógicas não descobertas.

## Próxima etapa 013 sugerida

Construir **passes de CFG de alto nível**: dominadores, pós-dominadores e regiões de controle, identificação conservadora de laços naturais e junções de `if`, mantendo fallback para blocos/labels e nunca inventando controle estruturado. Documentar as provas de região e não mudar o pseudocódigo principal antes de validação.

Quando o usuário autorizar testes em lote: compilar CLI+GUI, corrigir falhas acumuladas (002–013), testar nós/operandos/`Phi` e ciclo, jumps internos/externos, opacos, width/types, limites, comparar saídas com Ghidra por corpus.
