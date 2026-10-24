# Rework — Implementação 013: CFG, dominadores e regiões de controle candidatas

**Data:** 2026-10-23  
**Estado:** CÓDIGO ESCRITO; NÃO COMPILADO, NÃO EXECUTADO, NÃO TESTADO E SEM COMMIT (ordem explícita do usuário).  
**Branch:** `feat/reworkbench`, worktree `.worktrees/implementation`. HEAD observado: `b2a682f`.  
**Progresso estimado:** 13/100 (13%, provisório); **não** significa 13% de equivalência verificada com Ghidra.

## Implementação

### Arquivos novos

- `native/workbench/include/reworkbench/control_regions.hpp`: API de diagnóstico para `ControlBlockFacts`, `NaturalLoopCandidate`, `ConditionalJoinCandidate`, `IrreducibleCfgComponent` e `ControlRegionObservation`. `source_control_verified` inicia e permanece false.
- `native/workbench/src/control_region_analysis.cpp`:
  - Usa a HIR real produzida no incremento 012, sem executar um segundo lifter nem alterar `decompiler_detail::Program`.
  - Normaliza sucessores/predecessores do CFG observado e verifica coerência de IDs, entrada, blocos alcançáveis, destinos condicionais e saídas conhecidas.
  - Calcula dominadores como ponto fixo de interseção dos predecessores a partir do bloco de entrada; reporta conjuntos e dominador imediato por bloco, se convergente.
  - Calcula pós-dominadores **somente** quando o CFG modelado está fechado, todos os blocos alcançáveis têm caminho a um retorno conhecido, não existem saídas não resolvidas, e os dominadores convergem. Com múltiplos retornos, considera cada retorno uma raiz de saída, sem inventar retorno de fonte.
  - Identifica arestas de retorno `tail -> header` em que o cabeçalho domina o nó de origem; recupera conjuntos de laço naturais por fecho reverso de predecessores, e classifica candidatas com uma única entrada no CFG observado.
  - Para `while_header_candidate`, exige cabeçalho condicional com um sucessor dentro e outro fora do conjunto do laço (heurística conservadora, não código transformado).
  - Usa decomposição em componentes fortemente conexas para localizar regiões cíclicas com múltiplas entradas, que exigem fallback em CFG/labels.
  - Registra `conditional_join_candidate` para condicionais com dois destinos distintos e junção de pós-dominância coerente, evitando cabeçalhos identificados em laços e componentes irredutíveis.
  - Para funções parciais, efeitos opacos, destinos não resolvidos, loops sem saída conhecida, inconsistência de CFG ou limites atingidos, declara incompletude e **não certifica** AST/control flow de fonte.
  - Limites: até 384 blocos, 800 passadas por solução de ponto fixo, 512 laços reportados e 384 componentes; consulta de cancelamento. Sem mutation/transformações no motor original.

### Integração com o CLI

- `native/workbench/CMakeLists.txt`: adiciona `src/control_region_analysis.cpp` à biblioteca `reworkbench_native`.
- `native/workbench/src/cli_main.cpp`: inclui `control_regions.hpp`, serializador `control_regions_json` e novo comando `cfg-structure`.
- `hir-observe` mantém saída `hir` e passa a incluir `control_regions`, reutilizando a HIR gerada **uma única vez**.
- `cfg-structure` retorna `control_regions`, `hir_version` e `hir_partial`.
- JSON expõe blocos/sets de dominadores e pós-dominadores, candidatos de laço/junção, componentes irredutíveis, limites, avisos e estados `cfg_closed`, `has_unknown_exits`, `post_dominators_reliable`.
- Limite da serialização diagnóstica: 128 registros por conjunto com contagens integrais e `display_truncated`.

## Uso previsto (NÃO EXECUTADO)

```powershell
reworkbench-cli cfg-structure --input caminho\exemplo.exe
reworkbench-cli hir-observe --input caminho\exemplo.exe
```

**O executável já instalado não foi atualizado.** Não houve compilação/execução/teste, conforme determinação do usuário.

## Semântica e limites

1. Dominador/pós-dominador é um fato matemático **sobre o CFG observado**, não uma garantia de que recuperamos todo o controle real do binário. Exceções, calls, tailcalls, saltos indiretos e blocos não descobertos permanecem riscos.
2. Laço natural com entrada única e cabeçalho condicional ainda NÃO equivale automaticamente a `while` de C/C++, e junção condicional ainda NÃO equivale automaticamente a `if/else`.
3. O passe não converte CFG em AST, não move blocos, não reescreve `Phi`, não gera C e não modifica o renderer original.
4. Com `opaque_call` e `opaque_instruction`, trata fechamento do CFG como desconhecido. O diagnóstico de dominadores pode ser útil, mas não certifica pós-dominância para transformações.
5. O algoritmo de pós-dominância considera caminhos do CFG até saídas conhecidas; execução infinita e semântica de exceções não estão plenamente modeladas. Manter fallback em labels.
6. Não existem testes dessa etapa nem das entregas 002–013; uma revisão C++/Qt, algoritmo e regressões serão necessárias quando o usuário autorizar a validação conjunta.

## Próxima missão 014

Projetar um **plano de estruturação** sem emitir AST falsa: modelar regiões de CFG como opções de `if/else`, laços naturais e fallback irreducível, com cobertura e restrições; distinguir fatos observados, hipóteses e decisões de transformação ainda bloqueadas. Preparar interface futura para C-like emitter somente após provas e testes. Outra alternativa prioritária antes de otimizar: melhorar semântica do lifter e rastreamento de exceções/targets.

**Checkpoint:** atualizar `REWORK_CHECKPOINT.md` e `PROGRESSO_MASTERPLAN_0_A_100.md`, sinalizando `013` e `014`. Não commitar.
