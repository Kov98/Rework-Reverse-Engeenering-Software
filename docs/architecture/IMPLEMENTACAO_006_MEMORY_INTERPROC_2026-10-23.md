# Rework — incremento 006: dependências de memória e descoberta interprocedural

Data: 2026-10-23
Estado: CÓDIGO ESCRITO, NÃO COMPILADO, NÃO TESTADO, SEM COMMIT, conforme instrução expressa do usuário.
Worktree: `.worktrees/implementation`, branch `feat/reworkbench`.

## Resultados no código

1. `native/workbench/include/reworkbench/memory_dependence.hpp` e `src/memory_dependence.cpp`
   - Consolidação de dependências entre leituras de memória relativas à pilha e possíveis escritas que as atingem, reaproveitando `stack_value_flow`.
   - Registra para cada `Load` modelado: conjunto de `Store` candidatos por ID IR, possibilidade de memória prévia da entrada, interferência por operação desconhecida, escrita parcialmente sobreposta e truncamento.
   - Sinaliza `memory_phi_candidate` quando existem múltiplas fontes possíveis. **Não constrói ainda SSA global para a memória**, nem injeta PHI verdadeiro na IR.
   - Classifica intervalos `(offset, largura)` em pares de memória explicitamente disjuntos ou parcialmente sobrepostos; classificação restrita a offsets de RSP que o resolvedor afim modelou.
   - O campo `complete` é conservador e refere-se somente a esse subconjunto de memória, nunca a todos os acessos possíveis no programa.

2. `native/workbench/include/reworkbench/interprocedural.hpp` e `src/interprocedural.cpp`
   - Grafo de chamadas diretas entre funções conhecidas do snapshot, com classificação `internal_direct`, `external_or_undiscovered`, `indirect`.
   - Índices de chamador, instrução `CALL`, destino quando disponível e índice da função recuperada em casos diretos não ambíguos.
   - Resumo por função com contadores, corpo parcial, recursão direta observada e propriedade MAY de alcançar chamadas não resolvidas através de chamadas diretas.
   - Propagação de possíveis chamadas não resolvidas por lista reversa de arestas até ponto fixo.
   - Efeitos de memória e registradores permanecem desconhecidos para TODAS as funções. Isso é descoberta de grafo, **não sumarização semântica entre funções nem prova de pureza**.
   - Limites (4096 funções, 32768 chamadas) e marcador de incompletude; entradas de função duplicadas não geram alvos internos falsamente verificados.

3. Integração:
   - `native/workbench/include/reworkbench/function_abi.hpp`: adiciona `MemoryDependenceObservation`.
   - `native/workbench/src/function_abi.cpp`: resume dependências de memória após `stack_value_flow`, sem segundo lift nem alterar pseudocódigo.
   - `native/workbench/src/cli_main.cpp`: acrescenta `observation.memory_dependence` ao `abi-observe` e comando separado `call-graph`, com exportação de nós, arestas, avisos e condições de incerteza.
   - `native/workbench/CMakeLists.txt`: incorpora `memory_dependence.cpp` e `interprocedural.cpp` ao núcleo nativo.

## Exemplos previstos, ainda NÃO executados

```powershell
reworkbench-cli abi-observe --input exemplo.exe
reworkbench-cli call-graph --input exemplo.exe
```

O primeiro deve incluir `observation.memory_dependence`; o segundo, `interprocedural.functions` e `interprocedural.calls`. O código CLI antigo continua disponível; o executável existente não foi substituído.

## Limitações essenciais

- MemorySSA plena exige modelagem rigorosa de tokens de memória, aliasing entre espaços, posição temporal real das operações, memória parcialmente sobreposta, efeitos de chamadas e exceções. Este incremento **não entrega MemorySSA global**.
- Chamadas dinâmicas, thunks e tail calls podem não aparecer em `FlowKind::Call`; desconhecer um destino não garante que seja externo.
- Um grafo de chamadas direto não prova ausência de efeitos de memória, retorno garantido ou convenção de chamada.
- Instruções desconhecidas e snapshots parciais impedem conclusões definitivas. `complete` dos relatórios nunca significa correção de fonte recuperada.
- Memória com endereços globais, heap, alocações e ponteiros não modelados ainda fica fora dessa análise.

## Fila de próximas implementações

- MemorySSA de primeira classe na IR, com PHI de memória por join, aliases por regiões, invariantes e efeitos explícitos.
- Summaries por função com distinção de efeitos demonstrados/inferidos/desconhecidos; propagação entre funções somente em contratos verificados.
- Construção de HIR semântica, recuperação de assinaturas e tipos, seguido de emitter C/C++.
- Suite de regressão e comparações diferenciais Rework×Ghidra em corpus próprio quando autorizado testar em lote.

## Rastreabilidade

Este documento é relatório de **implementação escrita**, não resultado de build, QA, benchmark ou equivalência com o Ghidra.
