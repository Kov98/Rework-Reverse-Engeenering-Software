# Rework — Implementação 018: contratos de efeitos nas arestas da HIR

**Data:** 24/10/2026  
**Pasta/branch:** `C:\Users\pedri\OneDrive\Documents\PROJETO\Rework Reverse Engeenering Software`, `main`.
**Estado:** CÓDIGO ESCRITO no PC e integrado textualmente ao projeto; **NÃO COMPILADO, NÃO TESTADO, NÃO COMMITADO/NEM ENVIADO**.
**Progresso global editorial:** 18/100 (18% PROVISÓRIO); não corresponde a paridade medida com Ghidra.

## Objetivo

A etapa 017 descreveu cópias `Phi` simultâneas por meio de snapshots simbólicos. A etapa 018 define **fronteiras explícitas** para aplicar tal receita no CFG **sem** misturar o corpo do predecessor, as consequências do terminador, as transferências de valores na aresta selecionada e os nós não-`Phi` do sucessor. Não executa nem move operações.

## Implementação

### Novos arquivos
- `native/workbench/include/reworkbench/edge_effects.hpp`: `HirEdgeBoundaryAction` (posição, espécie de ação, fonte, destino Phi, temporário e representação), `HirEdgeEffectContract` (pred/succ, fronteiras, terminador, efeitos observados, barreiras, bloqueadores, cobertura e flags de autorização), `HirEdgeEffectsObservation`.
- `native/workbench/src/edge_effect_analysis.cpp`: `observe_hir_edge_effects(hir, copies, stop)`. Consome a HIR REAL e o plano 017, sem segundo lift e sem mutar nós.

### Regras

1. Cada `HirParallelCopyEdgePlan` vira contrato `predecessor -> successor`, com caso especial de entrada sintética `-1 -> entry`.
2. Checa alcance dos dois blocos e presença mútua de `pred.successors` e `succ.predecessors`. Destino desconhecido, `opaque_exit`, `none` e `machine_state_return` com aresta de saída não são considerados seguros.
3. Preserva o TERMINADOR e percorre as `ordered_definitions` do predecessor, mantendo os IDs de operações com efeitos que a HIR modelou: `Load` potencialmente falho, `Store`, projeções opacas, `Opaque`, `CallOpaque` e valores indefinidos. Registra até 32 IDs como amostras; as contagens totais permanecem disponíveis quando há truncamento.
4. Marca em `predecessor_exit` as ações `capture_scalar_reference` PREVISTAS, apenas para valores SSA já existentes e **não** como avaliação executada antes/depois de efeitos reais. A aplicação prevista ocorre como `successor_entry_phi_merge` antes das definições não-Phi do sucessor.
5. Estado de arquitetura/memória/opaque usa apenas `successor_entry_symbolic_state` — **não** vira atribuição-fonte nem é reordenado com Loads, Stores ou calls.
6. Confere que temporários de snapshot têm identidade e que todo commit `write_from_snapshot` tem captura anterior; diagnósticos bloqueiam receitas truncadas, arestas inválidas, ciclos ainda não lowered, estados opacos, reads potencialmente excepcionais e falhas da auditoria IR.
7. `exceptional_control_unknown=true`, `effect_order_preservation_proven=false`, `lowering_executed=false`, `rewrite_authorized=false` sempre. A fronteira registrada é hipótese operacional, não uma prova da semântica de exceções/efeitos.
8. Limites: até 384 blocos, 3072 arestas e 2048 ações por aresta, amostras de 32 efeitos por predecessor. Indica `limited/partial` quando truncado.

### Integração ao Rework
- `native/workbench/CMakeLists.txt`: inclui `src/edge_effect_analysis.cpp`.
- `native/workbench/include/reworkbench/structuring_plan.hpp`, `src/structuring_plan.cpp`: informações `edge_effect_contracts_accounted_for`, `edge_effects_unresolved` e obrigações `edge_effect_contracts_incomplete`, `edge_effect_semantics_unresolved`, `exception_and_call_effects_not_verified`; planos `structurally_eligible` requerem contratos completos, MAS `rewrite_authorized=false` permanece.
- `native/workbench/src/cli_main.cpp`: `edge_effects_json()`, comando `edge-effects`, ajuda/capabilities, seção `hir-observe.edge_effects` e campos diagnósticos no `structuring-plan`. A saída visual limita 96 arestas e 96 ações por fase, com contagens e `display_truncated`.

### Comando previsto, NÃO executado

```powershell
reworkbench-cli edge-effects --input caminho\exemplo.exe
```

O executável atualmente instalado não foi recompilado.

## Limitações importantes

- O grafo de controle real pode ter arestas excepcionais, sinais, chamadas que não retornam e saltos indiretos não descobertos. O diagnóstico não modela esses caminhos completamente.
- `captures/applications` são posições e referências **simbólicas**, NÃO execução dos loads, instruções ou efeitos de máquina.
- O planejador não converte esse contrato num bloco `if/while`, não altera o renderer, não produz AST nem C/C++ recompilável.
- As etapas 002–018 continuam sem compilação/testes em conjunto. Pode haver erros C++/Qt e semânticos desconhecidos.
- A porcentagem 18% é provisória e admite revisão para baixo após validação.

## Próxima missão 019

Reforçar os **contratos de segurança entre fronteiras**: normalização de terminadores, proveniência de controle condicional, trajetórias de exceção/noreturn e detecção de edge-splitting necessário antes de baixar transferências. Antes de expandir demais a HIR, considerar prioritário AUTORIZAR build e testes em lote 002–019.

**GitHub `main` permanece em `5795874`; incrementos 015–018 estão somente no diretório local `main` e ainda sem commit/push.**
