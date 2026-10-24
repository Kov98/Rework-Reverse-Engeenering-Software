# Rework — implementação 019: segurança de terminadores e arestas CFG

**Data:** 2026-10-24
**Branch:** `main` na pasta principal `Rework Reverse Engeenering Software`.
**Estado:** arquivos C++ e integração textual criados; **NÃO COMPILADO, NÃO TESTADO, NÃO COMMITADO NEM ENVIADO AO GITHUB**, por solicitação do usuário.
**Progresso editorial estimado:** 19/100 (19%, provisório), NÃO porcentagem de paridade medida com Ghidra.

## Objetivo

Antes de baixar `Phi` para transferências por aresta e reestruturar regiões em `if`/laços, descrever **terminadores e arestas efetivamente observados** e impedir a inferência de destinos, exceções e chamadas `noreturn` sem evidência.

## Arquivos novos

- `native/workbench/include/reworkbench/control_safety.hpp`: `HirTerminatorEvidence`, `HirControlEdgeEvidence`, `HirControlSafetyObservation` e `observe_hir_control_safety(hir,effects,stop)`.
- `native/workbench/src/control_safety_analysis.cpp`: passe read-only sobre HIR 012 e contratos de efeito 018.

## Funcionalidade escrita

1. Normaliza terminadores **somente como candidatos observados**: `conditional` com dois braços distintos resolvidos, `goto` com um destino, `machine_state_return` sem sucessores, `opaque_exit` como saída de natureza desconhecida, e `none` como terminador desconhecido.
2. Verifica correspondência entre `successors`, `predecessors`, alcance, endereço de destino resolvido e valor condicional conhecido. Relata a ambiguidade, sem substituir CFG.
3. Para cada bloco com operação `opaque_call`, registra `call_noreturn_unverified`; isso indica que a possibilidade NÃO foi descartada, não que a função tenha o atributo `noreturn`. Não inventa targets indiretos ou saídas excepcionais.
4. Identifica **aresta crítica no CFG observado** quando a origem tem múltiplos sucessores e o destino tem múltiplos predecessores. Marca necessidade potencial de `edge splitting` se uma transferência `Phi` localizada for baixada por aquela aresta, mas NÃO realiza nenhuma divisão.
5. Relaciona cada aresta real ao contrato de efeito 018 (`pred,succ`) e registra ausência/efeitos desconhecidos e proof blockers, mantendo `exceptional_transfer_unverified=true`, `rewriting_safe=false`.
6. Tem limites conservadores: até 384 blocos e 3072 arestas; sinaliza truncamento, não inferindo ausência de dados fora do orçamento.

## Integração

- `native/workbench/CMakeLists.txt` inclui `src/control_safety_analysis.cpp`.
- `native/workbench/include/reworkbench/structuring_plan.hpp` e `src/structuring_plan.cpp` recebem métricas `control_edges_accounted_for`, `critical_edges_detected`, `possible_nonreturning_call_blocks`, `unresolved_control_terminators` e obrigações não provadas. Nunca autoriza rewrite.
- `native/workbench/src/cli_main.cpp` serializa `control_safety_json`, registra `control-safety` em comandos/capabilities e anexa `hir-observe.control_safety`. Limita exibição a 96 blocos e 96 arestas por seção, com contagens e `display_truncated`.

Comando PREVISTO, não executado:
```powershell
reworkbench-cli control-safety --input caminho\exemplo.exe
```

## Falta validar

- **Etapas 002–019 permanecem sem compilação/testes conjuntos.** Todo o código pode conter erros de API Qt/C++20 ou lógica que só testes detectariam.
- `CallOpaque`, exits, traps, indiretos e exceções não são totalmente modelados. A classificação de terminadores é baseada no CFG recuperado, não em comportamento comprovado do binário.
- Falta implementação real de divisão de arestas críticas, aplicação de cópias paralelas, lowerings de memory-state, AST fonte, C recompilável, testes de equivalência com Ghidra.
- `source_control_verified=false` e `rewriting_authorized=false` permanecem, mesmo em relatório estruturalmente completo.

## Próximo passo 020

**Prioridade operacional:** auditar integração estática e obter autorização para compilar/testar incrementalmente 002–019, pois o risco de código acumulado sem validação já é alto. Se ainda não for permitido executar, construir apenas contratos/corpus *sem executar* para futura verificação diferencial Rework vs Ghidra.

**Git:** `main` remota ainda em `5795874`. Implementações 015–019 locais e sem commit/push.
