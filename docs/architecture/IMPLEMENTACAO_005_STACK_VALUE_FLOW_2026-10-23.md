# Rework — incremento 005: fluxo de valores na pilha (reaching writes e versões)

**Data:** 2026-10-23
**Branch:** `feat/reworkbench` (worktree `.worktrees/implementation`)
**Estado:** CÓDIGO ESCRITO E SALVO. SEM COMPILAÇÃO NEM TESTES NESTA RODADA, POR SOLICITAÇÃO EXPRESSA DO USUÁRIO.

## O que esta implementação faz

- Acrescenta identidade de nó IR e índice de bloco às evidências de `StackFrameReference`.
- Implementa análise de **possíveis escritas que alcançam uma leitura** (*may-reaching definitions*) sobre o CFG da IR x86-64; não confundir com recuperação de variáveis-fonte.
- Mantém uma versão de armazenamento para cada `Op::Store` conhecido com offset/intervalo e comprimento exato. Uma leitura pode ter várias escritas candidatas devido a bifurcações ou laços.
- Analisa entradas e saídas de blocos com iteração até ponto fixo, reunindo estados de predecessores alcançáveis.
- Lê/escreve slots exatos de pilha, mantendo offsets e larguras separadamente; eventos de escrita em intervalos parcialmente sobrepostos geram estado incerto, sem fusão de variáveis.
- Uma chamada `CallOpaque`, instrução `Opaque` ou escrita em endereço não modelado invalida conservadoramente as origens disponíveis até nova escrita exata comprovada; nunca assume preservação de memória.
- Leituras reportam `may_read_entry_memory`, `unknown_memory_effect_may_intervene`, `ambiguous_overlapping_write`, escritas candidatas e respectivos IDs de IR.
- Cada escrita conhecida pode listar leituras que **podem** alcançá-la e se alguma relação atravessa blocos básicos. Isso descreve **gerações observadas de armazenamento**, não tempo de vida real de variáveis C/C++.
- Excesso de definições vira **UNKNOWN**, não uma conclusão falsa.
- Resultados expostos em `abi-observe -> observation.stack_value_flow`. Emissor de pseudocódigo existente **não é modificado**.

## Novos arquivos

- `native/workbench/include/reworkbench/stack_value_flow.hpp`
- `native/workbench/src/stack_value_flow_analysis.hpp`
- `native/workbench/src/stack_value_flow_analysis.cpp`

## Integrações/alterações

- `native/workbench/include/reworkbench/stack_frame.hpp`: `ir_node_index`, `basic_block_index` para evidências.
- `native/workbench/src/stack_abi_observation.cpp`: vincula evidências Load/Store ao IR; corrige overflow assinado ao resolver offsets afins.
- `native/workbench/include/reworkbench/function_abi.hpp`: `StackValueFlowObservation`.
- `native/workbench/src/function_abi.cpp`: executa passe após mapear frame, reutilizando `Program` existente.
- `native/workbench/src/cli_main.cpp`: serializa `stack_value_flow` incluindo `read_relations`, `write_generations` e incertezas.
- `native/workbench/CMakeLists.txt`: registra nova unidade de compilação.

## Limites deliberados

- Só x86-64, acessos 8/16/32/64 bits e endereços modelados como `RSP_entrada + offset`.
- Máximos: 192 blocos, 128 slots, 8192 eventos, 512 leituras reportadas, 512 gerações, 32 origens por leitura, 64 definições simultâneas por estado, 256 iterações.
- Escrita com endereço desconhecido vira clobber global conservador; acessos desconhecidos não são transformados em slots fictícios.
- Operações opacas, regiões sobrepostas, ABI incerta e análise parcial tornam resultados inconclusivos.
- `Store` anterior não prova origem de variável de fonte; ausência de leitura não prova código morto.
- Ainda não é MemorySSA completa, liveness de variáveis C/C++, inferência de tipos ou HIR.
- Os marcadores `limited`, `has_unknown_aliases`, `dataflow_converged` e `verified_source_variables: false` permanecem explícitos.

## Validação em lote futura, quando autorizada

1. Compilar CLI e GUI com incrementos 002–005 e corrigir eventuais incompatibilidades.
2. Testar diamond CFG, loops, phi, Store/Load, subword writes, calls opacos, pilha não afim, erros e limites.
3. Executar regressão com corpus Win64 e SysV, comparando saídas de funções com Ghidra.
4. Mensurar CPU/memória com binários grandes e dados hostis.

## Próxima engenharia

Modelar aliasing com MemorySSA, contratos interprocedurais, liveness real (incluindo escapes/reutilização de armazenamento), `TypeEvidence`, IR de alto nível (HIR) e emissores multilíngues.

**Nenhuma compilação, teste ou benchmark foi realizado. Nenhum commit foi criado.**
