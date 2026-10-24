# Rework — Implementação 015: invariantes SSA e prova de ordem de memória

**Data:** 24/10/2026
**Branch de trabalho:** `main` (pasta principal `Rework Reverse Engeenering Software`).
**Commit-base:** `5795874` — merge da versão completa do Rework na `main`.
**Estado:** mudanças de código ESCRITAS, NÃO COMPILADAS, NÃO TESTADAS e NÃO COMMITADAS nesta etapa.
**Progresso editorial geral:** **15/100 = 15% PROVISÓRIOS**, não medidos por comparação Ghidra.

## Motivação

A HIR 012 e os passes 013–014 registram propostas de estruturação de CFG, mas nenhuma pode ser executada se a IR original não preservar corretamente entradas Phi, ordem de tokens de memória e relações entre blocos. Esta etapa reforça o **lifter e os invariantes**, não inventa código C/C++ recuperado.

## Mudança concreta no lifter

`native/workbench/src/decompiler_lift.cpp`
- O cache de nós `Extract` deixa de reutilizar um nó criado em outro bloco: uma definição pode não dominar um novo uso no CFG. Reutilização limitada ao **mesmo bloco de origem**.
- Não transforma instruções, não aplica CSE interblocos e não presume dominância posterior por mera identidade de expressão.

## Auditoria de IR estruturada

`native/workbench/include/reworkbench/ir_semantic_audit.hpp`
`native/workbench/src/ir_semantic_audit.hpp`
`native/workbench/src/ir_semantic_audit.cpp`

Verificações implementadas no código:
1. Estrutura de CFG (IDs, predecessores/sucessores), aridade de vetores de estado, índice de entrada e iteração de dominância.
2. Definições/operandos SSA com identidade de nós, usos antes da definição no mesmo bloco e dominância em outros blocos.
3. `Phi`: predecessor existe/é único, entrada sintética `-1` quando cabível, cobertura dos predecessores observados e compatibilidade de tipos.
4. **Aprimoramento da etapa 015 concluída:** para cada slot de estado, exige que `block.input[slot]` seja o `Phi` daquele slot/bloco, com nome e tipo correspondentes; verifica que cada `Phi.incoming` recebe exatamente `predecessor.output[slot]` ou `program.initial[slot]` na entrada. Verifica também os tipos da saída e os Inputs iniciais e mapeamentos `slot_index`.
5. Contratos de `Load`/`Store`: tipo do token de memória, largura de ponteiro e valores armazenados; `Opaque`/`CallOpaque` consomem todas as partes do estado com tipos compatíveis; `StateField` projeta campo válido e mantém tipo correto.
6. **Aprimoramento da etapa 015 concluída:** audita cada `Block.definitions` como sequência ordenada de nós pertencentes ao próprio bloco e compara a entrada do token `mem` de cada `Load`, `Store` e `Opaque` à versão corrente naquele momento. `Store` e projeção `StateField(mem)` atualizam essa versão; exige `block.output[mem]` igual à última versão construída. Exposição em `memory_state_order_consistent`.
7. Limites de blocos/nós/erros expostos como diagnóstico, sem alegar execução de teste.

## Integração e bloqueadores

- `native/workbench/include/reworkbench/hir.hpp`: `HirObservation.ir_audit`.
- `native/workbench/src/hir_analysis.cpp`: o ponto de entrada coleta auditoria da mesma `Program`; se falhar, marca a HIR parcial e `report_complete=false`.
- `native/workbench/include/reworkbench/structuring_plan.hpp` e `src/structuring_plan.cpp`: decisões `structurally_eligible` exigem auditoria estática satisfatória e CFG observável coerente; `unproved_rewrite_obligations` lista requisitos adicionais pendentes de segurança.
- `native/workbench/src/cli_main.cpp`: comando diagnóstico `ir-audit`, incorporado também a `hir-observe`; serializa `memory_state_order_consistent`, erros, avisos, Phi e métricas.
- `native/workbench/CMakeLists.txt`: inclui `ir_semantic_audit.cpp` na biblioteca nativa.
- **Nunca alterar CFG, mover efeito de memória ou declarar pureza somente pela auditoria.** `semantic_equivalence_proven=false` e `rewrite_authorized=false`.

## Limites relevantes

- A correção da análise é inferida por inspeção de texto, **ainda não validada por build/testes**; bugs/incompatibilidades dos incrementos 002–015 podem existir.
- A auditoria certifica somente relações estruturais da IR que o lifter conseguiu modelar. Ela não prova semântica x86/x64, comportamento externo, ABI, interrupções, exceções, efeitos concorrentes, aliasing real ou compilabilidade de C/C++.
- Chamadas e instruções opacas fazem a IR parcial. PHIs e laços/CFG irredutíveis exigem testes detalhados; resultados não justificam por si só reestruturação.
- Outros caminhos (por exemplo, retorno de máquina com leitura de endereço e operações semânticas parciais) exigirão provas adicionais na etapa futura.

## Validação futura, somente quando autorizada

- Compilar **incrementos 002–015** e corrigir erros/avisos C++/Qt/CMake.
- Construir corpus com caminhos em diamante, `Phi` de backedge, `Store -> Load`, `Opaque -> StateField(mem) -> Load`, falsa dominância e `Extract` repetido entre blocos.
- Verificar que a auditoria não marca incorretamente o CFG normal como inválido e que detecta IR malformada.
- Realizar regressões/Rework versus Ghidra e só então autorizar seleção/rewrite HIR.

## Próximo incremento: 016

Priorizar **semântica e contrato de edição da HIR/CFG**: separar explicitamente operações side-effect-free de efeitos de memória/controle, preservar `Phi` nas bordas e definir representação de transferências por aresta antes da emissão C-like. Começar com IR/HIR observacional e checklist de garantias; não mover instruções ou gerar código recompilável sem testes.

**Nenhum commit ou push criado neste incremento.** O push anterior da `main` até `5795874` permanece intacto.
