# Rework — etapa 022: auditoria estática de operandos e identidade de Phi

**Data:** 2026-10-24.
**Branch:** `main`, pasta principal `Rework Reverse Engeenering Software`.
**Status:** correções C++ ESCRITAS; **sem compilação, testes, benchmarks, commit ou push**.
**Progresso editorial:** 22/100 provisórios, NÃO equivalente a testes aprovados ou paridade Ghidra.

## Correção 1 — contratos tipados da IR

Arquivo: `native/workbench/src/ir_semantic_audit.cpp`.

Além da cobertura SSA/dominância/ordem de memória das etapas 015/021, a auditoria agora verifica:
- `Input` sem dono de bloco, operandos nem Phi; `Constant` com aridade zero, largura de 1–64 bits e bits altos coerentes.
- `Add/Subtract/Multiply/And/Or/Xor`: duas entradas bitvector, largura igual à do resultado.
- `Equal/UnsignedLess/SignedLess`: dois operandos bitvector de mesma largura e resultado booleano 1 bit.
- `Not`, `BooleanNot`: operandos/larguras coerentes com resultado.
- `Extract`: origem bitvector e fatia inteira dentro da largura da origem; offset não negativo.
- `Insert`: origem principal com largura igual ao resultado, valor inserido válido e offset no intervalo.
- `ZeroExtend/SignExtend`: um operando de largura menor que o resultado e ambos bitvector.
- `Select`: condição de 1 bit, dois valores e resultado da mesma largura.
- `Parity`: um operando de 8 bits e resultado 1 bit.
- Toda violação emite `bitvector_operator_contract`, `invalid_bitvector_width` ou `input_value_contract`, reduzindo `structural_checks_passed`. Este conjunto de invariantes **não prova semântica das instruções x86**.

## Correção 2 — entrada Phi malformada e HIR parcial

Arquivo: `native/workbench/src/phi_transfers.cpp`.

- Cada `HirValue` do tipo `phi` tem de preservar `id == posição no vetor`, bloco proprietário válido e alcançável.
- Phi sem proprietário, de bloco inalcançável ou de ID adulterado recebe `graph_partial=true` e impede `all_assignments_well_formed`.
- O estado `graph_partial` agora impede um relatório de atribuições supostamente bem-formadas, evitando que outras etapas tratem mapeamentos parciais como completos.
- Nenhuma transferência física, reescrita HIR/CFG ou geração C/C++ é executada.

## Dívida e validação pendente

O trabalho é apenas correção de fontes por leitura. **Nenhuma etapa 002–022 foi compilada/testada em conjunto.** O código pode conter erros C++20/Qt/integração ainda não detectados.
Quando autorizado, testar IR inválida para cada operação, Phi com bloco inválido/ID diferente, loops e Phi de entrada, limites de memória e chamadas opacas. Comparar Rework×Ghidra em corpus confiável depois de build funcional.

## Próxima missão 023

Prioridade máxima técnica: **build/regressão controlada de 002–022 quando o usuário autorizar**, corrigindo erros reais e revisitando crédito de progresso. Enquanto a restrição de não testar persistir, limitar-se a correções estáticas e à melhoria dos critérios de aceite.

## Git

Último commit compartilhado: `main` em `5795874`. Edições 015–022 permanecem locais, sem commit/push.
