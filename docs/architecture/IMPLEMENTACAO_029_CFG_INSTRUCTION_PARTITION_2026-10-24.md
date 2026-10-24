# REwork — etapa 029: integridade da partição de instruções no CFG

**Data:** 24/10/2026.
**Ramo:** `main`, pasta principal `Rework Reverse Engeenering Software`.
**Status:** C++ escrito, **NÃO COMPILADO, NÃO TESTADO, SEM COMMIT OU PUSH.**
**Progresso global editorial:** **29/100 (29%, provisório)**, não paridade com Ghidra.

## Evidência observada no código

`decompiler_lift.cpp` cria blocos `Program.blocks` agrupando os índices das instruções recuperadas da função. Os índices se referem à posição original em `Function.instructions`, e podem não estar numericamente ordenados como os endereços. Depois, `Builder::run` incrementa `Program.total` para cada instrução percorrida em bloco `reachable`.

A auditoria de CFG, embora verificasse índice de blocos, alcance e arestas, ainda não confirmava que uma referência de instrução pertence a um único bloco, que nenhum bloco é vazio e que `Program.total` coincide com o número de referências alcançáveis.

## Alteração realizada

`native/workbench/src/ir_semantic_audit.cpp`:

- Percorre as listas `Block.instructions` de **todos** os blocos do CFG, inclusive inalcançáveis.
- Rejeita bloco sem referência de instrução com `cfg_empty_instruction_block`.
- Rejeita índice de instrução negativo com `cfg_negative_instruction_index`.
- Guarda índices em `QSet<qsizetype>` e sinaliza reutilização entre blocos ou dentro do mesmo bloco com `cfg_duplicate_instruction_owner`.
- Conta as referências de instruções em blocos marcados `reachable` e confronta o total com `Program.total`, registrando `cfg_reachable_instruction_count`.
- Sinaliza `Program.modeled > Program.total` com `cfg_modeled_exceeds_total`.
- Cada incompatibilidade impede `cfg_consistent` e, por consequência, `structural_checks_passed`. Não modifica a IR, o CFG ou os dados originais e não autoriza nenhuma reescrita.

## Limitações e evidências pendentes

- O objeto `Program` não guarda o tamanho original de `Function.instructions`, então esta etapa **não** pode verificar o limite superior de todos os índices nem a cobertura completa de instruções do snapshot. O contrato se restringe à unicidade, índices negativos e contagem de referências alcançáveis.
- Índices NÃO são exigidos em ordem numérica porque a lista original pode não estar ordenada por endereço.
- Blocos e CFG são os **observados pelo lifter**; exceções, chamadas, indiretos e semântica ISA ainda não são comprovados.
- Os incrementos 002–029 seguem sem compilação/testes conjuntos; possíveis erros de integração e falsos positivos precisam de validação quando autorizada.

## Casos futuros (NÃO EXECUTADOS)

1. Duplicar um índice dentro do mesmo bloco: erro de duplicidade.
2. Referenciar a mesma instrução em dois blocos distintos: erro de duplicidade.
3. Bloco sem instruções: erro de bloco vazio.
4. Inserir índice negativo: erro de índice inválido.
5. Alterar `Program.total` ou aumentar `Program.modeled` além dele: erro de contagem.
6. CFG genuíno com índices não monotônicos: não gerar falso positivo.
7. Laço/referência repetida por EXECUÇÃO não duplica `Block.instructions`; validar esse comportamento.

## Próxima etapa 030

Prioridade técnica: liberação expressa de build/regressões para identificar erros concretos no conjunto de etapas; sem liberação, continuar revisão estática dos contratos e do lifter. Sem commit/push automático.
