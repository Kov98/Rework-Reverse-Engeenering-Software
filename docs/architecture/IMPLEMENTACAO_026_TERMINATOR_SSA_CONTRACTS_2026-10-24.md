# REwork — etapa 026: contratos de terminadores e usos SSA implícitos

**Data:** 24/10/2026
**Branch:** `main` do repositório principal `Rework Reverse Engeenering Software`.
**Estado:** implementação de auditoria escrita; **sem compilação, testes, benchmarks, commit ou push**.
**Progresso do plano:** 26/100 **editoriais e provisórios**; não indica paridade com Ghidra.

## Problema revisado

A auditoria de SSA já verificava os operandos explícitos de cada nó e os
vetores de saída por bloco. Porém `Block.condition` e
`Block.return_target` são **usos implícitos de SSA no terminador** e
não estão contidos em `Node::operands`. Era possível aceitar uma condição
de desvio mal tipada, um endereço de retorno inválido ou um valor não
dominante sem emitir o erro específico correspondente.

## Implementação em `native/workbench/src/ir_semantic_audit.cpp`

- Adicionada checagem read-only `available_at_exit`: um valor usado no
  terminador tem de ser um `Op::Input` global legítimo, pertencer ao
  próprio bloco, ou ter definição que domine o bloco.
- `Terminator::Conditional`: exige `condition` válido com tipo
  `Bits(1)`, uso SSA disponível na saída e no máximo 2 sucessores
  modelados. Códigos: `conditional_terminator_type`,
  `conditional_terminator_dominance`,
  `conditional_too_many_successors`.
- `Terminator::Return`: exige `return_target` válido e largura igual
  à largura do registrador da arquitetura, dominância SSA e ausência de
  sucessores CFG. Códigos: `return_target_type`,
  `return_target_dominance`, `return_with_cfg_successors`.
- `Terminator::Goto`: não pode ter mais que uma aresta normal;
  `goto_multiple_successors` em caso de incompatibilidade.
- Bloco alcançável sem terminador: `missing_reachable_terminator`.
- Falhas alimentam as flags `cfg_consistent`,
  `operand_types_consistent`, `ssa_uses_dominated` e desativam
  `structural_checks_passed`. Não foi acrescentado emitter e não há
  reescrita da IR ou CFG.

## Conservadorismo e riscos

Não foi exigido que `Goto` tenha exatamente um sucessor nem que
`Conditional` tenha dois: alvos externos, indiretos e CFG parcialmente
recuperado não devem ser inventados. Nenhum efeito excepcional ou
comportamento `noreturn` é inferido.

Esta etapa foi concluída **somente como alteração de código-fonte**.
Os incrementos 002–026 ainda não tiveram build/regressão conjuntos.
Quando autorizado, testar predicados inválidos, predicados de outro ramo,
retorno mal tipado, retorno com arestas, saltos desconhecidos e CFG válido
de funções reais; corrigir falsos positivos antes de permitir reescrita.

## Etapa 027

Priorizar a compilação e os testes de integração com autorização explícita.
Enquanto proibidos, manter revisão estática do núcleo e análise de falhas
concretas sem afirmar que os comandos estão funcionais.
