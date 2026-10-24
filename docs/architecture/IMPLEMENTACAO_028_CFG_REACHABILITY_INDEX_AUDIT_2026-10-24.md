# REwork — implementação 028: alcance independente e integridade do índice de blocos CFG

**Data:** 2026-10-24  
**Branch:** `main` na pasta principal `Rework Reverse Engeenering Software`.  
**Status:** código C++ ESCRITO por revisão de fontes; **SEM build, testes, benchmarks, commit ou push**.  
**Progresso editorial:** 28/100 (28%, provisório). Não significa paridade medida com Ghidra.

## Problema encontrado

A auditoria estrutural `ir_semantic_audit.cpp` usava diretamente
`Block.reachable` para filtrar verificações de CFG, dominância e Phi.
Esse campo é produzido por `build_cfg`, mas ainda é dado de entrada da
auditoria: se estivesse incorreto, caminhos reais poderiam desaparecer
da verificação e/ou blocos inalcançáveis poderiam ser interpretados como
executáveis. O mapeamento `Program.block_at` também só era checado para
blocos previamente marcados como alcançáveis.

## Correção escrita

Arquivo: `native/workbench/src/ir_semantic_audit.cpp`.

1. Calcula conjunto `discovered` independentemente: visita a partir de
   `Program.entry`, seguindo apenas sucessores de índices válidos, com
   `check_cancelled(stop)` e orçamento de até 512 blocos existente.
   Ignora flags `reachable` para esse cálculo.
2. Em todos os blocos da lista (alcançáveis e inalcançáveis), compara
   `Block.reachable` com `discovered.contains(i)`; diferença registra
   `cfg_reachability_flag_mismatch` e `cfg_consistent=false`.
3. Confere que `Program.block_at` tem exatamente tantas entradas quanto
   `Program.blocks`; erro `cfg_block_index_cardinality`.
4. Confere que o endereço de **cada bloco** resolve exatamente para seu
   próprio índice e para o mesmo espaço de endereços da entrada da função;
   erro `cfg_any_block_address_mismatch`.
5. Mantém os bloqueios de endereço/aresta da etapa 027. A auditoria não
   altera flags, não inventa arestas, não corrige o CFG e não transforma a
   IR. Em caso de discordância, apenas rejeita certificação estrutural.

## Casos de regressão futuros — NÃO executados

- Bloco alcançável via entrada com `reachable=false`: erro esperado.
- Bloco desconectado com `reachable=true`: erro esperado.
- Bloco desconectado com `reachable=false`: legítimo, sem falso erro.
- `block_at` sem entrada, entrada extra, índice errado ou espaço
  divergente: rejeitar o CFG.
- Laços, ramos, joins e CFG normal recuperado pelo builder: auditoria
  não deve rejeitar sem motivo.
- Aresta inválida ou alvo externo: não adicionar blocos/arestas,
  deixar condições não comprovadas e preservar fallback.

## Dívida técnica

Os incrementos 002–028 continuam SEM compilação/regressão integradas.
A nova regra testa a consistência do CFG **observado**, não a fidelidade
dos destinos de máquina, exceções, indiretos, ABI e demais semânticas
que ainda não estão comprovadas. `rewrite_authorized=false` continua
necessário. O percentual é estimativa editorial, não medição funcional.

## Próxima etapa 029

Prioridade: liberar build/testes controlados com autorização expressa,
identificar erros reais e medir funcionalidades. Se não for autorizado,
continuar auditoria estática direcionada à correção de invariantes
existentes, sem declarar o decompilador funcional.
