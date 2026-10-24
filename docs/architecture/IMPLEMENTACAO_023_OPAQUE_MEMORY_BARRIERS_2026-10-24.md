# REwork — Etapa 023: memória e barreiras opacas na IR

**Data:** 2026-10-24.
**Pasta:** `C:\Users\pedri\OneDrive\Documents\PROJETO\Rework Reverse Engeenering Software`
**Branch:** `main`; HEAD-base `5795874`.
**Status:** implementação escrita, **NÃO compilada/testada**, sem benchmark, commit ou push.
**Progresso editorial estimado:** 23/100 provisórios, sem evidência de paridade com Ghidra.

## Problema confirmado por leitura de código

A auditoria de memória da etapa 015 conferia se `Opaque/CallOpaque`
consumia o token `mem` corrente. Porém a verificação não exigia que a
subsequente `StateField("mem")` pertencesse àquela mesma operação,
nem impedia outro `Load/Store` ou efeito opaco enquanto a projeção estava
pendente. Um grafo malformado poderia parecer estruturalmente íntegro
apesar de reutilizar o estado de memória anterior à chamada.

## Correção implementada

Arquivo: `native/workbench/src/ir_semantic_audit.cpp`.

- Um `pending_opaque` por bloco registra qual operação opaca ainda exige
  a projeção da próxima versão de memória.
- Um `Load`/`Store` entre a operação opaca e a projeção correspondente
  gera erro `memory_operation_crosses_opaque_barrier`.
- Uma nova operação opaca antes da projeção anterior gera
  `nested_unresolved_opaque_barrier`.
- `StateField("mem")` somente atualiza `current_memory` se tiver um
  operando que aponte precisamente para `pending_opaque`; projeções
  ausentes, extras ou ligadas a outro efeito geram
  `memory_projection_wrong_effect`.
- Uma barreira que chega ao fim do bloco sem a projeção correta gera
  `unresolved_opaque_memory_projection`.
- Todas as violações desativam `memory_state_order_consistent` e,
  consequentemente, `structural_checks_passed`.

O passe não altera o CFG, não gera código-fonte, não reduz efeitos e não
conclui que alguma chamada seja pura ou `noreturn`.

## Cenários de validação futura (NÃO EXECUTADOS)

1. `Opaque(old_mem) -> StateField(mem, Opaque) -> Load(new_mem)`: ordem
   estruturalmente coerente, embora a equivalência binária ainda não seja provada.
2. `Opaque(old_mem) -> Load(old_mem) -> StateField(mem, Opaque)`:
   exige erro de acesso antes da projeção.
3. `Opaque_A(old_mem) -> StateField(mem, Opaque_B)`: exige erro.
4. `Opaque_A(old_mem) -> Opaque_B(old_mem)`: exige erro de barreira sobreposta.
5. `Opaque_A(old_mem) -> fim do bloco`: exige erro de projeção ausente.
6. `StateField(mem, Opaque)` sem barreira pendente: exige erro.
7. Múltiplos efeitos válidos em sequência: não devem gerar falso bloqueio
   quando as projeções corretas aparecem na ordem.

**Limitação:** são contratos estáticos da IR modelada, não provas de
exceções, concorrência, aliasing, semântica ISA, ABI ou decompilação correta.
As etapas 002–023 não passaram por build/testes conjuntos.

## Próxima etapa 024

Priorizar compilação e regressões do núcleo acumulado **apenas mediante
autorização explícita do usuário**. Enquanto não houver autorização,
continuar com revisão estática de invariantes e preservar todo fallback.
