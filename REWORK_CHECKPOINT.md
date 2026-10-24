# REWORK — CHECKPOINT PRINCIPAL (LEIA PRIMEIRO AO RETOMAR)

**Checkpoint:** 014, 2026-10-23.  
**Próxima missão:** 015 — auditoria estática de invariantes semânticos da IR/SSA/CFG; condições de prova antes de reestruturação.  
**Progresso estimado:** **14/100 (14%, PROVISÓRIO)**; não é paridade comprovada com Ghidra.  
**Validação:** etapa 001 compilada e 25 testes CLI aprovados (2 skipped); **etapas 002–014 somente ESCRITAS, NÃO COMPILADAS/NEM TESTADAS.**

## Objetivo / regras do usuário

Desenvolver motor próprio do decompilador Rework, primeiro visando equivalência mensurável por domínio/corpus com Ghidra e depois melhorias demonstráveis. Metas futuras de C/C++, Java, C#, Rust e JS exigem frontends e emissores próprios; não afirmar reconstrução exata nem saída recompilável por padrão.
**Prioridade: núcleo do DECOMPILADOR, sem mexer na interface gráfica.**
Usuário pediu explicitamente: **NÃO compilar, NÃO testar, NÃO benchmarkar, NÃO fazer commit até nova ordem.** Escrever código, manter checkpoints persistentes e progresso 0–100. Não mascarar código escrito como recurso aprovado.

## Acesso e Git

Desktop Commander `DESKTOP-S1ATAV9` ID `e49cad8c-6ab7-40e3-99dc-877b7d0fb86e` (conferir online ao retomar).
Repositório: `C:\Users\pedri\OneDrive\Documents\PROJETO\Rework Reverse Engeenering Software`.
**Worktree único a editar:** `C:\Users\pedri\OneDrive\Documents\PROJETO\Rework Reverse Engeenering Software\.worktrees\implementation`.
Branch `feat/reworkbench`; último HEAD confirmado `b2a682f` no checkpoint 014 (reconfirmar).
Existem diversas modificações `M` e arquivos `??` PREEXISTENTES. Não fazer reset, clean, checkout destrutivo, commit ou sobrescrever outros trabalhos. Não confundir com Git no diretório pai.
Arquitetura mestre: `docs/architecture/REWORK_DECOMPILER_MASTERPLAN_V1.md`.
Progresso: `docs/architecture/PROGRESSO_MASTERPLAN_0_A_100.md`.
Relatório atual: `docs/architecture/IMPLEMENTACAO_014_STRUCTURING_PLAN_2026-10-23.md`.

## Histórico de implementações

- 000: auditoria Rework/Ghidra, arquitetura mestre.
- 001: `abi.hpp/.cpp`, candidatos de ABI Win64/SysV e CLI; ÚNICA rodada compilada/testada (25 passes, 2 skips).
- 002: `function_abi.*`, `CallOpaque`, CLI `abi-observe`. NÃO testado.
- 003: `stack_abi_observation.*`, `call_effects.*`, argumentos na pilha. NÃO testado.
- 004: `stack_frame_analysis.*`, `stack_frame.hpp`, slots e proveniência. NÃO testado.
- 005: `stack_value_flow_analysis.*`, `stack_value_flow.hpp`, reaching writes. NÃO testado.
- 006: `memory_dependence.*`, `interprocedural.*`, call graph `call-graph`. NÃO testado.
- 007: `memory_ssa.hpp`, `memory_ssa_analysis.*`, grafo estrutural dos tokens de memória. NÃO testado.
- 008: `memory_regions.hpp`, `memory_region_analysis.*`, pilha/absolutos/unknown e aliases. NÃO testado.
- 009: `effect_summary.hpp/.cpp`, evidências de efeitos interprocedurais, `effects-observe`. NÃO testado.
- 010: `region_effects.hpp`, `region_effect_analysis.*`, efeitos por região. NÃO testado.
- 011: `type_evidence.hpp`, `type_evidence_analysis.*`, tipo-fato da IR e pistas, `type-observe`. NÃO testado.
- 012: `hir.hpp`, `hir_analysis.*`, HIR v0 estrutural, `hir-observe`. NÃO testado.
- 013: `control_regions.hpp`, `control_region_analysis.cpp`, dominadores/pós-dominadores, laços naturais, diamantes candidatos e CFG irredutível, CLI `cfg-structure`. NÃO testado.
- **014 ATUAL:** `include/reworkbench/structuring_plan.hpp`, `src/structuring_plan.cpp`: propostas conservadoras `natural_loop_candidate`, `while_header_candidate`, `if_else_diamond_candidate`, razões explícitas para bloquear, cobertura e conflitos entre propostas. Preserva fallback dos blocos originais (IDs, entradas, sucessores, condição e destinos de saltos), sem tocar na HIR existente. `structurally_eligible` não significa transformação autorizada, `rewrite_authorized=false`, `emitted_source_code=false`. Novo comando `structuring-plan`; `hir-observe` ganha campo `structuring_plan`; `native/workbench/CMakeLists.txt` inclui cpp. **SÓ CÓDIGO ESCRITO, NÃO COMPILADO/NEM TESTADO.**

## Arquitetura e riscos

C++20/Qt6/Capstone. IR: `native/workbench/src/decompiler_internal.hpp`, lifter `decompiler_lift.cpp`, renderer `decompiler.cpp`, HIR `hir_analysis.cpp`, controles `control_region_analysis.cpp`, planejamento `structuring_plan.cpp`.
Todos os módulos 002–014 são sidecars majoritariamente read-only sobre a mesma IR/CFG; não há emitter C confiável nem reconstrução de tipos C/C++ verificada. `TypeEvidence.confirmed_ir` é apenas fato de largura/operação na IR; ABI/alias/chamadas opacas continuam hipótese ou efeitos desconhecidos. CFG dominadores/loops só valem para o CFG observado, que pode estar incompleto por chamadas, exceções, saltos indiretos ou instruções não modeladas.
O planejador 014 **não reescreve** `Phi`/CFG, memória ou branches, não transforma controle em `if/while` e não autoriza AST/otimização. Fallback raw CFG preservado. Candidatos podem se sobrepor e estar bloqueados.
**Validação das etapas 002–014 está adiada pelo usuário**. C++ pode ter erros de build/link/integração/lógica não descobertos. Nunca alegar uso end-to-end ou paridade Ghidra.

## Próximo incremento 015 — foco em semântica real

Em vez de aumentar apenas número de relatórios sidecar, revisar estaticamente o núcleo `decompiler_lift.cpp` e os invariantes `Load/Store/Phi` e `CallOpaque`: id de operandos, ordem temporal das operações dentro dos blocos, projeções de memória, dominância SSA, efeitos e fronteiras de confiança. Implementar mecanismo de **proof obligations/constraints** para futura transformação de HIR, sinalizando cada requisito não provado. Seguir sem compilar/testar até o usuário liberar, mas não presumir que os módulos acumulados estão corretos.

## Procedimento de retomada

1. Conectar Desktop Commander, **LER ESTE ARQUIVO PRIMEIRO**.
2. Ler rubrica, relatório 014 e C++ real antes de editar; conferir `git status --short --branch` e HEAD sem modificar o repositório.
3. Codar missão 015 sem compilação, testes, benchmarks ou commits.
4. Salvar `IMPLEMENTACAO_015_...` e atualizar este checkpoint e a rubrica, sem pontuar linhas de código como progresso.
5. Distinguir código escrito, integrado e validado.

Frase para próxima conversa: “Retome o Rework pelo Desktop Commander. Leia `REWORK_CHECKPOINT.md` no worktree `feat/reworkbench` e o relatório 014. Continue etapa 015, sem compilar, testar ou commitar. Atualize checkpoint e progresso.”


## 015 — INTERRUPÇÃO PARA COMMIT (WIP, 23/10/2026)

O usuário interrompeu a etapa 015 para sair do computador e pediu um commit do Rework.
**NÃO classificar a etapa 015 como concluída. Progresso global permanece 14% estimados até a conclusão técnica.**
Nesta etapa já foram ESCRITOS, mas NÃO COMPILADOS/NEM TESTADOS:
- Correção no cache de `Extract` em `native/workbench/src/decompiler_lift.cpp`: impedir reutilização entre blocos sem prova de dominância.
- `include/reworkbench/ir_semantic_audit.hpp`, `src/ir_semantic_audit.hpp/.cpp`: auditoria estática de CFG/SSA/Phi/Load/Store/CallOpaque/StateField, sem prova de equivalência semântica.
- A HIR carrega `IrSemanticAudit` e a coleta o executa; `native/workbench/CMakeLists.txt` inclui o CPP de auditoria.
- `structuring_plan.cpp/.hpp` inclui requisitos de prova ainda não satisfeitos, `rewrite_authorized=false`; revisar integração por possível edição parcial.
- `src/cli_main.cpp`: início da serialização `ir_semantic_audit` e integração do comando `ir-audit`; conferir se a ligação final está completa.
**Pendente na retomada:** auditoria estática detalhada do código, concluir integração de CLI e gating do planejador, relatório de implementação 015, rubrica/porcentagem e, somente quando autorizado, compilar/testar em lote.
O worktree `feat/reworkbench` não possui remoto Git configurado no momento do pedido: commit local não equivale a push GitHub.
