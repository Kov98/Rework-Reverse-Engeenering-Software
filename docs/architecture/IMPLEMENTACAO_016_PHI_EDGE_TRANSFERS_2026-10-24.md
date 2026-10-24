# Rework — Implementação 016: transferências Phi por aresta do CFG

**Data:** 24/10/2026
**Branch/pasta:** `main` em `C:\Users\pedri\OneDrive\Documents\PROJETO\Rework Reverse Engeenering Software`.
**Estado:** CÓDIGO ESCRITO, integrado ao CMake/CLI em texto. **NÃO COMPILADO, NÃO TESTADO, SEM COMMIT E SEM PUSH.**
**Progresso estimado:** 16/100 (16%, PROVISÓRIO); NÃO mede paridade com Ghidra.

## Entrega de engenharia

Criado `native/workbench/include/reworkbench/phi_transfers.hpp`:
- `HirPhiEdgeAssignment`: destino `Phi` (ID SSA preservado), origem na aresta, largura, representação física, identidade, token de máquina e fonte opaca.
- `HirPhiEdgeTransfer`: `predecessor_block -> successor_block`, origem sintética `-1` na entrada, cópias SIMULTÂNEAS, dependências, ciclos, avisos, bloqueadores, `rewrite_authorized=false`.
- `HirPhiTransferObservation`: contagem de arestas esperadas/observadas, atribuições, dependências e ciclos, limites, incertezas, integridade SSA e ausência de autorização para reescrita.

Criado `native/workbench/src/phi_transfers.cpp`:
1. Consome a HIR v0 que já contém `HirValue::phi_inputs`, `HirBlock::predecessors/successors`, a auditoria da etapa 015 e identidades de valores. **NÃO gera segunda IR ou modifica a HIR.**
2. Para cada bloco alcançável, registra uma transferência por predecessor alcançável, e a entrada sintética `-1` para o bloco de entrada. Verifica identidade da aresta no CFG.
3. Exige exatamente uma origem para cada `Phi` de destino naquela aresta. Rejeita duplicatas, fontes fora dos limites, IDs HIR incompatíveis e tipos/larguras da IR divergentes. Mantém coleções e motivos explícitos.
4. Identifica dependências de cópia entre destinos e fontes da MESMA aresta e ciclos que exigiriam temporários; **não executa cópias sequenciais**.
5. Diferencia "atribuições SSA estruturalmente completas" de "prontas para reescrita": token de memória/arquitetura, chamadas opacas e dependências paralelas são rastreados como bloqueadores mesmo quando as entradas `Phi` estão completas.
6. Orçamentos: até 384 blocos, 3072 arestas, 40000 transferências SSA no diagnóstico; overflow é sinalizado sem alegar ausência dos dados faltantes.
7. `rewrite_authorized=false` incondicional. Sem emitter, otimizações ou conversão de memória/estado de máquina em variáveis-fonte.

### Integrações escritas

- `native/workbench/CMakeLists.txt`: adiciona `src/phi_transfers.cpp`.
- `native/workbench/include/reworkbench/structuring_plan.hpp`: adiciona `phi_edge_associations_complete`, número de arestas com dependência paralela e ciclos.
- `native/workbench/src/structuring_plan.cpp`: usa a análise `Phi` como requisito adicional para declarar um plano estruturalmente elegível, preserva `unproved_rewrite_obligations` e bloqueia propostas incompletas; o fallback de CFG e `rewrite_authorized=false` continuam intactos.
- `native/workbench/src/cli_main.cpp`: comando `phi-transfers`, estrutura `phi_transfers_json` por aresta, integração em `hir-observe.phi_transfers` e campos no relatório de `structuring-plan`. Limite visual de 128 arestas e 96 atribuições por aresta, com `display_truncated`.

### Uso PREVISTO; NÃO EXECUTADO

```powershell
reworkbench-cli phi-transfers --input caminho\programa.exe
reworkbench-cli hir-observe --input caminho\programa.exe
```

O executável instalado NÃO foi compilado com o novo módulo.

## Restrições e riscos

- O diagnóstico diferencia bem-formação da aresta de segurança da transformação, mas ainda não há algoritmo de resolução de cópias paralelas com temporários nem tradução de tokens memória/estado para expressões C.
- Uma fonte em bloco de loop, `Phi` de cabeçalho e operação opaca podem afetar fluxos; o algoritmo não corrige semântica do lifter.
- `all_edges_covered` é sobre o CFG que foi RECUPERADO, não o CFG real total do binário. `all_assignments_well_formed` depende da auditoria estática `IrSemanticAudit`, que também não foi compilada/testada.
- Os módulos 002–016 exigem compilação C++20/Qt, regressões e provas de semântica antes de alegar capacidade real. Este incremento só foi inspecionado como texto.
- Não houve commit ou push desta etapa nem da 015; último GitHub `main` permanece no merge `5795874`.

## Próxima missão 017

Implementar **planejamento de lowering de cópias paralelas com temporários virtuais** usando snapshots de fontes e destinos, preservando classes de estado/efeitos opacos. Não aplicar rewriting da IR ainda; construir uma representação transacional de transfers que possa ser verificada depois. Ao ser autorizado, priorizar build/testes em lote 002–017, com casos: `Phi` em diamante, ciclos `A<-B, B<-A`, `Phi` de backedge, `mem Phi` e chamadas opacas.

Status da etapa 016: **implementada em arquivos-fonte, não validada em execução**.
