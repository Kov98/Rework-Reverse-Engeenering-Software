# Rework — incremento 007: grafo explícito de versões de memória na IR

Data: 2026-10-23
Estado: ESCRITO, ainda NÃO compilado/nem testado conforme instrução do usuário.
Worktree: `.worktrees/implementation`, branch `feat/reworkbench`, HEAD base b2a682f.
Progresso estimado global: 7/100, PROVISÓRIO.

## Objetivo do incremento

A IR antiga já representa memória como um slot de estado de tipo `TypeKind::Memory`, com operações `Input`, `Store`, `Phi` e `StateField` após `Opaque/CallOpaque`. Não replicar sem necessidade essa arquitetura; torná-la inspecionável e preparada para análises MemorySSA avançadas.

## Código criado

- `native/workbench/include/reworkbench/memory_ssa.hpp`: `MemoryVersion`, `MemoryReadUse` e `MemorySSAObservation` com proveniência, IDs estáveis por observação, predecessor, origens de PHI, instrução, bloco e marcadores de efeitos desconhecidos.
- `native/workbench/src/memory_ssa_analysis.hpp`: função interna `memory_ssa_analysis::observe(Program, stop_token)`.
- `native/workbench/src/memory_ssa_analysis.cpp`: dois passes sobre IR: indexa os nós de tipo memória e resolve depois `Store.prev`, `Phi.incoming` e `StateField` com indicação da memória anterior ao efeito opaco; `Load` é ligado ao token explícito que consome.
- Propaga conservadoramente `may_contain_unknown_effects` ao longo dos predecessores de versões e `Phi`, por lista reversa de arestas e fila (sem assumir que o grafo é acíclico).
- Falta de referências e limites de recurso colocam `complete=false`; não apaga operações nem muta `Program`.
- `native/workbench/include/reworkbench/function_abi.hpp` inclui `MemorySSAObservation memory_ssa`.
- `native/workbench/src/function_abi.cpp` coleta observação sobre a mesma IR usada em ABI/stack.
- `native/workbench/src/cli_main.cpp` publica `observation.memory_ssa` no comando `abi-observe`, com contagens totais e visualização limitada a 1.024 versões/leituras para não emitir JSON gigantesco.
- `native/workbench/CMakeLists.txt` adiciona `src/memory_ssa_analysis.cpp` à biblioteca.

## O que isto NÃO é

- NÃO é implementação completa de MemorySSA otimizada de Ghidra/LLVM.
- Memória continua num único espaço de estado lógico; NÃO existem alias classes verificadas por heap/stack/globais, escape, semântica de memória atômica ou exceções.
- Nenhum Store é substituído ou eliminado; nenhum Load vira constante; não altera gerador de pseudocódigo.
- `Phi` aqui representa fluxo de versões de memória já presente na IR, sem prova de exclusividade de caminhos ou tipos de origem.
- Efeitos de `CallOpaque`/operação desconhecida continuam impedindo conclusões sobre valores.
- O sinalizador `complete` representa apenas integridade estrutural das versões de memória no trecho modelado, não correção global da decompilação.

## Testes em lote PENDENTES (não executar sem autorização)

- Build Release CLI+GUI com todos os incrementos 002–007; correção de compilação/linkedição e warnings.
- Corpus mínimo: Load inicial sem Store, Store->Load, Store->Store->Load, diamond Phi, backedge Phi, CallOpaque->Load, Store em endereço desconhecido, dado sobreposto e loops.
- Verificar que os IDs de versão referenciam nós reais; checar memória pré-efeito opaco e propagação do aviso de efeito desconhecido.
- Benchmarks e comparação Ghidra apenas depois da estabilização.

## Próxima etapa (008)

Modelar regiões de memória (stack, global, heap, unknown), alias conservador e efeitos de chamadas por função, sem inventar pureza ou ABI. Separar inferência de assinatura/tipo da IR. Depois HIR, reestruturação do CFG, decompilação C/C++ e, após equivalência por domínio, multilíngue.

Nenhum commit, build, teste ou benchmark foi executado na entrega 007.
