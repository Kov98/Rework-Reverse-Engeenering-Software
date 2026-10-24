# Rework — Implementação 014: planejamento conservador de estruturação da HIR

**Data:** 2026-10-23  
**Branch:** `feat/reworkbench` — `.worktrees/implementation`, commit-base observado `b2a682f`.  
**Estado:** CÓDIGO ESCRITO E SALVO, NÃO COMPILADO, NÃO TESTADO, SEM COMMIT por solicitação explícita do usuário.  
**Progresso global estimado:** 14/100 (provisório), não equivalente a paridade com Ghidra.

## Objetivo

Converter observações de CFG/dominadores/laços da etapa 013 em **propostas de reestruturação com bloqueadores e fallback explícito**, ainda SEM reescrever HIR/SSA, alterar pseudocódigo, otimizar memória ou fabricar AST C/C++.

## Arquivos

- `native/workbench/include/reworkbench/structuring_plan.hpp` — novos contratos `RegionProposal`, `RawBlockFallback`, `HirStructuringPlan`, função `plan_hir_structuring(hir, control, stop)`.
- `native/workbench/src/structuring_plan.cpp` — algoritmo observacional com limites de recursos, propostas de laços/diamantes, verificação de arestas e fallback completo de blocos.
- `native/workbench/CMakeLists.txt` — registra `src/structuring_plan.cpp`.
- `native/workbench/src/cli_main.cpp` — import, `structuring_plan_json`, comando `structuring-plan`; `hir-observe` ganha também `structuring_plan`, reutilizando a mesma HIR/observação de controle.

## Semântica dos relatórios

Cada `RegionProposal` armazena: tipo (`natural_loop_candidate`, `while_header_candidate`, `if_else_diamond_candidate`), bloco cabeçalho, blocos membros, join/saída se conhecidos, blockers, `confidence`, `structurally_eligible` e `rewrite_authorized=false`.

- Laço: conserva conjunto observado de loop natural, backedge latch->header, exige entrada única, destino condicional e aresta de saída única para criar hipótese de `while`. Detecta interseção com regiões irredutíveis.
- Condicional: no v0 somente hipótese de **diamante simples** de duas arms, cada uma um único bloco que segue para join explícito. Condicionais complexas geram proposta `blocked` com razão, não AST inventada.
- Somente sob fechamento do CFG e convergência das análises as propostas podem ser rotuladas `candidate_in_observed_cfg`. Mesmo assim a transformação permanece **NÃO AUTORIZADA**, exigindo prova de equivalência semântica e testes posteriores.
- Se o fluxo é incompleto, alvo desconhecido, SCC irredutível ou há interseção de regiões, a proposta fica `blocked_requires_raw_cfg` e o CFG antigo segue intacto.
- `RawBlockFallback` cobre todos os blocos do HIR, inclusive alcançabilidade, endereço, terminador, condition value, destinos absolutos de saltos e sucessores de bloco; nunca apaga um bloco por ele participar de proposta.
- `uncovered_reachable_blocks` indica quais blocos não aparecem em qualquer proposta; `overlapping_candidate_blocks` aponta ambiguidade entre candidatas e `all_reachable_blocks_have_fallback` atesta cobertura do fallback dentro do limite.
- `HirStructuringPlan`: `emitted_source_code=false`, `rewrite_authorized=false`, `raw_cfg_preserved=true`. Isso descreve um plano e seus limites, não um emissor C ou uma HIR estruturalmente reescrita.

### Uso previsto (NÃO EXECUTADO)

```powershell
reworkbench-cli structuring-plan --input caminho\programa.exe
reworkbench-cli hir-observe --input caminho\programa.exe
```

A versão executável atual NÃO foi recompilada para conter o novo comando.

## Invariantes / bloqueadores

1. Mesmo uma proposta `structurally_eligible=true` NÃO autoriza mudar a IR; há efeitos opacos, dados SSA, `Phi`, aliasing, chamadas, exceções e side effects a validar.
2. Os candidatos são extraídos apenas de CFG observado, sem prova de que todo o binário foi descoberto.
3. O fallback original não perde blocos nem relações de endereço, e o plano não aplica qualquer transformação.
4. Regiões propostas se sobrepõem às vezes, o que demanda futura seleção global consistente; `overlapping_candidate_blocks` explícito.
5. Limites: até 384 blocos de HIR e 512 propostas; visualização CLI limita 128 propostas/blocos, mantendo contagens e `display_truncated`.
6. Nenhum código-fonte original (tipos, if, while, switch ou assinatura) é inferido definitivamente.
7. Etapas 002–014 ainda não passaram por compilação/regressão conjunta: o código pode conter erros sintáticos/de integração/lógicos. Não afirmar CLI funcionando ou paridade Ghidra.

## Próxima missão 015

Prioridade técnica recomendada: **núcleo de semântica de IR/CFG** em vez de continuar indefinidamente adicionando sidecars. Revisar estaticamente invariantes de `Phi`/ordem de estados/efeitos de CALL e adicionar um modelo explícito de condições de transformação segura (proof obligations) em torno do planejamento. Posteriormente, quando usuário autorizar, validação em lote de etapas 002–014, compilação e testes diferenciais Rework × Ghidra. Somente após as provas, iniciar um emitter C-like com fallback de blocos.

## Checkpoint

Atualizar `REWORK_CHECKPOINT.md`, `PROGRESSO_MASTERPLAN_0_A_100.md` ao fim. NÃO compilar/testar/commitar até autorização.
