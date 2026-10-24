# Rework — Implementação 017: planejamento transacional de cópias paralelas de Phi

**Data:** 24/10/2026  
**Branch/pasta:** `main`, `C:\Users\pedri\OneDrive\Documents\PROJETO\Rework Reverse Engeenering Software`.  
**Status:** código ESCRITO e conectado a CMake/CLI/planejador. **Não compilado, não testado, não executado, não commitado e não enviado ao GitHub**.
**Progresso global editorial:** 17/100 = 17% PROVISÓRIOS; não é paridade Ghidra ou correção verificada.

## Objetivo

A etapa 016 criou um pacote de transferências `Phi` para cada aresta do CFG e detectou dependências/ciclos. A etapa 017 cria uma **receita transacional de lowering** desses pacotes, preservando a simultaneidade, sem jamais mover instruções ou editar a IR.

### Arquivos novos
- `native/workbench/include/reworkbench/parallel_copy_plan.hpp`: contratos `HirParallelCopyStep`, `HirParallelCopyEdgePlan`, `HirParallelCopyPlan` e API `plan_parallel_phi_copies(transfers, stop)`.
- `native/workbench/src/parallel_copy_plan.cpp`: implementação de snapshots virtuais e plano de commit.

### Algoritmo

Para cada aresta observada `pred -> succ`:
1. Herda IDs SSA de origem/destino, representação, largura, condição de entrada sintética `-1` e todos os bloqueadores do analisador de Phi.
2. Na **fase snapshot**, captura uma referência simbólica a cada valor escalar de origem distinto num temporário virtual `temp_id`, com IDs determinísticos e sem executar instruções. Reutiliza o mesmo temporário quando vários destinos leem a mesma fonte.
3. Só depois da captura de TODAS as fontes registra a **fase commit**, em que cada destino escalar aponta para o seu temporário; uma transferência identidade é `identity_noop`. Assim, cadeias e ciclos como `A<-B, B<-A` não perdem fontes por sobrescrita sequencial.
4. Transferências com representação de `memory_token`, `architecture_state` ou `opaque_state` ficam em `symbolic_state` com operação `preserve_state_dependency`, sem serem falsamente convertidas em variáveis do código-fonte.
5. Fonte opaca/indefinida, token de máquina, ciclo de dependência e auditoria SSA incompleta são bloqueadores de lowering físico, mesmo quando o **plano simbólico** está bem formado.
6. Verifica unicidade dos destinos e temporários, referência capturada antes do uso, correspondência contagem `assignments/commits` e que nenhuma captura aparece após o primeiro commit. Se qualquer restrição falhar, sinaliza incompletude e preserva dados originais.
7. Guarda contagens de arestas, temporários, escritas planejadas, tokens simbólicos, ciclos e limites. Máximo de 3072 arestas e 100000 ações simbólicas; limites não são tratados como prova de ausência de transfers.

**Invariantes inegociáveis:** `source_snapshot_before_any_write=true`, `machine_ir_unchanged=true`, `lowering_executed=false`, `rewrite_authorized=false`. Estes campos descrevem desenho do passe; NÃO certificam uma transformação real.

### Integrações
- `native/workbench/CMakeLists.txt`: inclui `src/parallel_copy_plan.cpp`.
- `include/reworkbench/structuring_plan.hpp` + `src/structuring_plan.cpp`: novo status `phi_copy_schedule_structurally_consistent`, quantidade de temporários virtuais e transferências simbólicas; bloqueia candidatas sem receita íntegra, acrescenta proof obligation `parallel_copy_plan_incomplete` quando necessária. Mesmo com plano completo, a reescrita não foi implementada nem autorizada.
- `src/cli_main.cpp`: `parallel_copy_plan_json()`; comando `phi-copy-plan`, disponibilidade em `capabilities`/ajuda e campo `hir-observe.phi_copy_plan`. Saída JSON limita visualização a 128 arestas e 128 ações por aresta e sinaliza truncamento sem perder contadores.

### Comandos PREVISTOS — NÃO EXECUTADOS

```powershell
reworkbench-cli phi-copy-plan --input caminho\programa.exe
reworkbench-cli hir-observe --input caminho\programa.exe
```

O executável atualmente instalado pode não conter tais comandos — não houve build.

## Limites e pendências

- As ações são **referências a valores SSA já levantados**, não avaliação de expressões nem operações de máquina.
- Snapshot transacional por aresta é útil mesmo com múltiplos destinos, mas **não prova** que instruções excepcionais/externas, saltos indiretos, aliasing e tempo de vida estejam totalmente modelados.
- `structurally_consistent` não significa que o plano pode ser compilado em C/C++.
- A auditoria estática SSA da etapa 015 e o contrato `Phi` da etapa 016 continuam **sem teste**, assim como todos os incrementos 002–017.
- Testar quando autorizado: `A<-B,B<-A`, cadeias `A<-B,B<-C`, origem compartilhada `A<-C,B<-C`, transferências identidade, mistura scalar/mem/arch, backedge, Phi ausente, corpos opacos e truncamento. Validar que nenhum efeito de memória é movido e que o fallback CFG é preservado.

## Próxima etapa 018

Dar prioridade ao **suporte semântico a controle de exceções, edge effects e lowering formal dos snapshots**. Separar explicitamente `predecessor-exit capture` de `successor-entry transfer`, e planejar um emissor de IR intermediária que preserve valores/efeitos sem ainda afirmar fonte C equivalente. Fortemente recomendável solicitar autorização para compilar/testar em lote antes de ampliar mais módulos, para evitar dívida de integração.

**Checkpoints 015–017 permanecem locais na `main`, sem commit/push desde `5795874`.**
