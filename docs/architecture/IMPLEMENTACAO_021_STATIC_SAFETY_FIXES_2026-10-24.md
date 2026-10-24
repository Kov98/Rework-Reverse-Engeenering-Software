# Rework — etapa 021: correções estáticas de segurança da IR e CFG

**Data:** 2026-10-24
**Repositório:** pasta principal `Rework Reverse Engeenering Software`, branch `main`.
**Estado:** correções ESCRITAS por inspeção. **NÃO compiladas, NÃO testadas, SEM commit/push.**
**Progresso estimado:** 21/100 EDITORIAL e PROVISÓRIO; sem equivalência demonstrada ao Ghidra.

## Problemas identificados pela leitura do código

### A. Cobertura de arestas não implica terminador válido
Em `native/workbench/src/control_safety_analysis.cpp` a flag
`all_observed_edges_accounted_for` anteriormente podia permanecer
verdadeira apesar de o terminador do bloco possuir destinos inconsistentes
ou um salto condicional não comprovado. A variável `all_matched` agora
fica falsa caso `observed_successor_set_consistent` seja falso.

`partial` também considera a existência de contratos de efeito ausentes.
**Não implica que um CFG consistente reproduza todas as saídas reais.**

### B. Planejador não pode aceitar semântica de aresta pendente
Em `native/workbench/src/structuring_plan.cpp` a condição de
`structure_reliable` foi reforçada: agora também requer
`unresolved_control_terminators == 0` e `edge_effects_unresolved == 0`.
Os candidatos de laço e condicional carregam explicitamente blockers:
`unresolved_control_terminators` e `unresolved_edge_effect_semantics`.
`rewrite_authorized` permanece falso, sem transformar HIR/CFG.

### C. Auditoria de memória podia não perceber definição omitida
Em `native/workbench/src/ir_semantic_audit.cpp` a auditoria já percorria
`Block.definitions` e a ordem de `Store/Load/CallOpaque`, mas sem conferir
que TODOS os nós IR pertencentes ao bloco estavam nessa sequência.
Foi adicionada verificação inversa, com:
- `unlisted_block_phi`: nó Phi não listado nos inputs daquele bloco.
- `unlisted_block_definition`: nó não-Phi pertencente a bloco alcançável
  ausente das definições ordenadas.
- Qualquer nó não-Phi omitido invalida também
  `memory_state_order_consistent`, pois pode ocultar efeitos de memória.

A nova verificação usa conjuntos locais limitados ao número de blocos/nós
já abrangidos pela auditoria, consulta cancelamento e não modifica o grafo.

## O que não foi feito

- Nenhuma compilação, teste unitário, execução de fixture, benchmark ou
  comparação diferencial com Ghidra foi realizada.
- Não foram criados commits, push, PR ou mudanças na interface gráfica.
- A correção semântica final ainda exige build, testes de casos malformados
  e execução diferencial. O passe continua conservador; não autoriza AST.

## Próxima etapa 022 sugerida

**Prioridade 1:** obter permissão para compilar e testar todo o núcleo desde
002 e corrigir erros reais da toolchain/Qt; especialmente regressões de
`ir-audit`, `control-safety` e `structuring-plan`.
**Se execução ainda não estiver autorizada:** revisar os demais contratos,
sem aumentar artificialmente a alegação de funcionalidade.
