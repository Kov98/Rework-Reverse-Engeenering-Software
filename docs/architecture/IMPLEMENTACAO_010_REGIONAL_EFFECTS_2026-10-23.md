# Rework — Implementação 010: efeitos de memória observados por região

Data: 2026-10-23
Estado: CÓDIGO ESCRITO, SEM COMPILAÇÃO, SEM TESTES E SEM COMMIT por solicitação do usuário.
Worktree: `.worktrees/implementation`, branch `feat/reworkbench`, base HEAD anteriormente `b2a682f`.
Progresso global ESTIMADO: 10/100, provisório; não é equivalência Ghidra comprovada.

## Objetivo técnico

Criar resumos por função dos acessos Load/Store que o lifter consegue modelar, separados pelo tipo de expressão de endereço e propagados por arestas diretas conhecidas do grafo de chamadas. Preservar incertezas e proveniência; nenhuma conclusão de pureza ou ausência de efeitos.

## Novos arquivos

- `native/workbench/include/reworkbench/region_effects.hpp`: contratos tipados `RegionEffectSite`, `RegionEffectSummary`, `LocalRegionEffects`, inicialização de quatro classes de armazenamento.
- `native/workbench/src/region_effect_analysis.hpp`: contrato interno sobre `decompiler_detail::Program` e `BinaryMetadata`.
- `native/workbench/src/region_effect_analysis.cpp`: análise somente leitura que reutiliza `memory_region_analysis::observe` para x86-64, associa cada Load/Store modelado e alcançável ao seu ID de nó IR e região. Quando a classificação é impossível ou excede budget, classifica como `unknown` **sem inventar heap, stack ou globais**. Em outras ISAs, mantém regiões `unknown`.

Quatro classes expostas:
1. `stack_relative` — expressão comprovadamente afim em relação a RSP de entrada, sob o modelo restrito.
2. `mapped_image_candidate` — constante absoluta que caiu dentro de seção de imagem; NÃO prova variável global original.
3. `absolute_candidate` — constante absoluta de endereço sem atribuição a uma seção conhecida.
4. `unknown` — ponteiro de origem não comprovada, região ausente devido a limite, ISA não suportada; pode ser stack, heap, global ou outro espaço.

## Integrações

- `include/reworkbench/effect_summary.hpp`: cada `FunctionEffectSummary` contém `regional_effects` e `regional_effects_partial`.
- `src/effect_summary.cpp`: usa o MESMO `Program` já produzido pelo lift, sem duplicar o lift; coleta contagens de Load/Store modelados por região. Inicializa regiões com todos os quatro nomes até para funções cujo corpo não foi analisado, mas deixa contagens em zero SEM alegar ausência de efeito real.
- No ponto fixo de propagação reversa do grafo direto, propaga observações POSITIVAS de leitura/escrita por região, testemunha de função de origem (`read_witness_function_index`, `write_witness_function_index`) e indicador de parcialidade; ciclos/recursão mantêm propagação monotônica.
- `src/cli_main.cpp`: `effects-observe -> interprocedural_effects.functions[].regional_effects`, contendo contagens de acessos diretamente observados, flags de transitive effects, testemunha de função, amostras `instruction/ir_node/write/confidence`, flag `samples_truncated` e `regional_effects_partial`.
- `native/workbench/CMakeLists.txt`: inclui `region_effect_analysis.cpp`.

## Limites e critérios de interpretação

- Até 16 amostras de proveniência por região por corpo, com `samples_truncated`. Contagens de operações modeladas são preservadas, mesmo quando amostras excedem o teto.
- Classificação de região subjacente limita-se a 384 acessos por função, 1024 pares relatados e expressões afins limitadas; após limite, acessos restantes ficam `unknown`. Não elimina positivos.
- Mantém limites existentes do `effects-observe`: até 128 corpos sob budget próprio; funções restantes permanecem com estado parcial/desconhecido; CLI relata total e eventuais truncamentos.
- `transitive_read_observed=true` significa **pode alcançar algum Load modelado** no grafo de chamadas diretas, NÃO que ocorre em todas as execuções.
- `mapped_image_candidate` não significa `global` verificada, e `absolute_candidate` não significa heap.
- Calls externas/indiretas, tail calls, instruções opacas, SSA parcial, caches e alocações fora de região modelada são fontes de incerteza.
- `may_read_unknown_memory`, `may_write_unknown_memory`, `register_effects_unknown`, `control_effects_unknown` continuam true; `purity_verified=false`, `signature_verified=false` para todas as funções.
- Não constrói MemorySSA por regiões completa, resumos de escrita transacionais, modelo de escape, prova de aliasing ou recuperação de tipos/variáveis C++.
- NÃO altera IR, pass de otimização, renderer, UI nem executável atual.

## Validação futura acumulada (NÃO rodar sem pedido)

1. Compilar todos incrementos 002–010 e corrigir possíveis falhas de C++/Qt/linkedição em bloco, incluindo o novo CLI e CMake.
2. Testar funções com Store/Load stack, absoluto na imagem, endereço desconhecido, chamada interna, externa/indireta, recursão, corpo parcial e orçamento excedido.
3. Verificar testemunhas transitivas, contagens de efeitos quando amostras são truncadas e propagação de parcialidade pelo CFG de chamadas.
4. Garantir que nenhuma função é marcada pura por falta de Load/Store; validar que o renderer não muda e o código antigo mantém comportamento.
5. Comparação Rework×Ghidra só após integração estável.

## Próxima missão: 011

Criar `TypeEvidence` com proveniência para larguras, tipos candidatos de armazenamento e tipos de ABI; combinar fatos confirmados de debug/símbolos com hipóteses de IR sem os confundir. Planejar HIR tipada por função, preservando unknowns e efeitos. Seguir sem compilação/testes/commit até autorização.
