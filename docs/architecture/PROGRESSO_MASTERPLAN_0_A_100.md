# REWORK — PROGRESSO DO PLANO MESTRE (0–100%)

**Checkpoint:** 014 — 2026-10-23
**Progresso global estimado: 14/100 = 14% (PROVISÓRIO).** Anterior 13%.
Esta porcentagem é avaliação editorial do escopo escrito, não medição de paridade Ghidra. Etapas 002–014 SEM compilação, testes ou benchmarks conjuntos; créditos podem regredir na auditoria futura.

## Rubrica 100 pontos

| Área | Peso | Crédito editorial |
|---|---:|---:|
| Ingestão binária, ferramentas e análise | 8 | 4 |
| Lifter ISA e semântica MIR correta | 16 | 1 |
| CFG/SSA, pilha, memória e aliasing | 14 | 2,5 |
| ABI, chamadas, assinatura, interprocedural | 12 | 2,5 |
| HIR, estruturação e emissão C | 17 | 3 |
| Tipos, estruturas, objetos C++ | 11 | 1 |
| Demais ISAs e ABIs | 7 | 0 |
| Frontends JVM/CIL/.NET/JS | 7 | 0 |
| Emissores multilíngues | 4 | 0 |
| Segurança, testes, benchmark e paridade demonstrada | 4 | 0 |
| **TOTAL** | **100** | **14** |

O crédito de 3/17 em HIR refere-se a esqueleto estrutural, fatos de dominância e planejamento de regiões, todos **não validados**. Não há emissor C nem AST C fiel.

## Marcos de evidência

- Etapa 001: ABI Win64/SysV compilada anteriormente, 25 testes CLI aprovados e 2 pulados.
- Etapas 002–011: observadores de ABI, stack, memória e tipos, código escrito, **NÃO compilado**.
- Etapa 012: HIR v0 estrutural read-only, `hir-observe`, **NÃO compilada**.
- Etapa 013: dominadores, pós-dominadores, laços naturais, candidatos de junção/irredutibilidade, `cfg-structure`, **NÃO compilada**.
- **Etapa 014:** `structuring_plan.cpp/.hpp` com candidatos de regiões para laço/diamante, bloqueadores, fallback original de todos os blocos e contadores de cobertura e conflito. CLI `structuring-plan`, campo adicional em `hir-observe`, alteração CMake. **CÓDIGO SALVO E NÃO COMPILADO/NEM TESTADO.**

## Principais limitações

Sem equivalência demonstrada com Ghidra, sem semântica correta e completa de ISAs, chamadas/ABI incompletas, aliasing e MemorySSA em grande parte observacionais, sem tipos-fonte recuperados, sem HIR reestruturada, sem saída C/C++ validada, sem JVM/.NET/JS e outras ISAs/frontend/emissores confiáveis. Propostas `structurally_eligible` não autorizam reescrever CFG; `rewrite_authorized=false` em todos os casos.

## Próximo marco 015

Priorizar engenharia semântica do núcleo (ordem SSA, PHI, Store/Load, efeitos de chamadas) e desenvolver contrato explícito de proof obligations para transformação segura de HIR, preservando fallback. Não inventar conclusão de pureza ou estrutura-fonte.

## Continuidade

Ler `REWORK_CHECKPOINT.md` e `docs/architecture/IMPLEMENTACAO_014_STRUCTURING_PLAN_2026-10-23.md` no worktree `feat/reworkbench`.
**Ordem do usuário: NÃO compilar, rodar testes, benchmarkar ou commitar até ele liberar.**
