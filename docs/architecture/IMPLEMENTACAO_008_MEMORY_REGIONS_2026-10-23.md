# Rework — incremento 008: regiões de memória e aliasing conservador

**Data:** 2026-10-23
**Estado:** código escrito, NÃO COMPILADO nem TESTADO, SEM COMMIT (solicitação expressa do usuário).
**Branch:** feat/reworkbench, no worktree .worktrees/implementation.
**Progresso global estimado:** 8/100 (provisório, não verificado por comparação Ghidra).

## Entrega implementada no código

- `native/workbench/include/reworkbench/memory_regions.hpp`: `MemoryAddressFact`, `MemoryAliasPair`, `MemoryRegionObservation`.
- `native/workbench/src/memory_region_analysis.hpp` e `.cpp`: coletor limitado de Load/Store da IR e resolvedor de expressão simbólica 64-bit.
- Resolvedor considera `RSP_entrada + deslocamento`, constantes 64-bit, soma/subtração com no máximo um fator de RSP e `Phi` com proveniência idêntica. Ciclos, instruções opacas, ponteiros desconhecidos e larguras incompatíveis permanecem UNKNOWN.
- Endereços absolutos constantes são classificados como candidatos; quando um intervalo completo cai numa região mapeada informada pelo loader, recebe `mapped_image_candidate` e nome da região. NÃO prova variável global: a origem da constante e relocações exigem análise adicional.
- Os demais ponteiros ficam `unknown`. **Nenhum endereço é rotulado HEAP** sem rastreamento real de alocador/escape/lifetime. Ponteiros desconhecidos podem apontar para stack, heap, mmap e globais.
- Comparação de intervalos na MESMA base simbólica e larguras conhecidas: `must_overlap_address_ranges` e `proven_disjoint`; bases diferentes ou desconhecidas mantêm `may_alias`. Não afirmar que stack e endereço absoluto são sempre disjuntos.
- Limites: 384 acessos observados, até 1024 pares não triviais detalhados e 128 níveis de expressão. Contadores agregados permanecem disponíveis, flags `limited` e `display_truncated` expostas.
- Efeitos `Opaque`/`CallOpaque` continuam desconhecidos/barreiras. O passe é **somente observacional**; não transforma, otimiza, elimina ou reordena Load/Store.
- Integração: `function_abi.hpp` carrega `MemoryRegionObservation`; `function_abi.cpp` executa o passe sobre a mesma `Program` já levantada; `cli_main.cpp` serializa `observation.memory_regions` em `abi-observe` com proveniência de cada instrução e evidências por par; `native/workbench/CMakeLists.txt` inclui `memory_region_analysis.cpp`.

## Limites e riscos remanescentes

1. NÃO é alias analysis completo: ponteiros derivados de parâmetros, alocadores, heap, transferências indiretas, casts, FFI, escape, memória atômica e exceções não têm modelo preciso.
2. `Phi` consistente só quando a origem afim é literalmente igual nos caminhos; joins não triviais caem em UNKNOWN.
3. Mesma localização não implica mesmo CONTEÚDO de memória (interferência por Store/Call); apenas relação de endereço.
4. Passes anteriores 002–007 já estão pendentes de compilação/regressão; a combinação agora é 002–008, **não validada como conjunto**. A etapa 001 foi a única compilada/testada.
5. Conclusões de intervalos absolutos só se aplicam a ponteiros constantes modelados e à semântica de endereço daquela IR; não autorizam conclusão de imagem/heap/stack real sem prova.
6. Há uso pré-existente de ordem de criação dos nós em análises de Store/Load: revisar ordem temporal e `Phi` em CFG em uma futura auditoria antes de qualquer otimização.
7. As recentes alterações ao CLI podem exigir revisão de compilação/serialização no batch de validação.

## Etapa 009 sugerida

- Contratos de efeitos de CALL por função e análise interprocedural **conservadora**, separados em `verified`, `inferred`, `unknown`; nunca assumir pureza por ausência de `Store` observado.
- Subtipagem de localização de memória, classes reais e escape analysis onde houver evidência.
- Preparar `TypeEvidence` e `HIR` somente depois de estabilizar a IR atual.
- Quando usuário autorizar (não agora): build CLI/GUI, regressões de CFG+ABI+pilha+memória/alias/PHI, corpus Win64/SysV e comparação Ghidra.

Este é relatório de código ESCRITO, não de funcionalidades validadas.
