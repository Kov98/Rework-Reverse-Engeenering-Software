# REWORK — CHECKPOINT PRINCIPAL: LEIA PRIMEIRO

**Etapa atual:** **035 — CBW/CWDE/CDQE modelados com extensão de sinal do acumulador em 24/10/2026; SEM build/testes.**
**Próxima etapa:** **036 — compilar/testar somente se autorizado; caso contrário, revisão estática das formas ISA/lifter.**
**Progresso global editorial:** **35 / 100 (35%, provisório)**. Não representa paridade medida com Ghidra.
**Situação Git:** trabalho agora na branch **`main`** da pasta principal, NÃO no antigo worktree `.worktrees\implementation` da `feat/reworkbench`. HEAD base `5795874`, sincronizado com `origin/main` ANTES das edições da 015. As edições das etapas 015–035 integram o lote Git desta entrega. Verificar o estado real de commit e push com `git log -1` e `git status --short --branch`; a implementação **continua sem compilação ou testes conjuntos**. Não reintroduzir arquivos de uma branch obsoleta por engano.

## Objetivo do usuário

Construir o decompilador nativo Rework com meta de equivalência progressivamente mensurável em relação ao Ghidra, e depois aprimorá-lo. O objetivo final abrange recuperação de C/C++ e outros frontends/emissores especializados para Rust, Java, C# e JavaScript.
**Prioridade: motor do decompilador, não UI.**
O usuário pediu em toda esta sequência **não compilar nem executar testes/benchmarks** até nova autorização. Houve autorização pontual para commit + push do código completo; `main` foi atualizada no GitHub até commit `5795874`. **Não interpretar essa autorização pontual como pedido de commits automáticos para cada nova etapa**. Preservar alterações sem commit enquanto não instruído.

## Ambiente do computador

Desktop Commander dispositivo `DESKTOP-S1ATAV9`, ID `76843f51-2283-48ec-b14d-5602c3952c9c`, verificar online na retomada.
**Repo/pasta de trabalho atual:** `C:\Users\pedri\OneDrive\Documents\PROJETO\Rework Reverse Engeenering Software`.
Branch desejada: `main`. Remoto GitHub: `https://github.com/Kov98/Rework-Reverse-Engeenering-Software.git`.
A subpasta `.worktrees\implementation` corresponde a um Git REPOSITÓRIO SEPARADO, cujo snapshot foi integrado à `main` no commit `5795874`; não retomar edição lá. Não apagar, resetar, limpar ou sobrescrever trabalhos preexistentes. Conferir `git status --short --branch`.

## Marcos existentes

- **001** — perfis ABI Windows x64 e SysV AMD64; único build CLI/testes confirmados em rodada anterior (25 passaram, 2 pulados), ANTES das etapas seguintes.
- **002–005** — CallOpaque, observações ABI, frames e reaching writes de stack.
- **006–008** — grafo de chamadas, dependências/token SSA de memória, aliasing preliminar e regiões.
- **009–010** — resumos observacionais de efeitos diretos/transitivos e por região.
- **011** — TypeEvidence e pistas de largura, signedness e endereço por uso.
- **012** — HIR v0 read-only com valores, blocos, Phi, CFG e efeitos opacos.
- **013** — dominadores, pós-dominadores, candidatos de natural loop/diamond, CFG irredutível.
- **014** — plano de estruturação conservador de HIR; fallback completo de blocos, `structuring-plan`, `rewrite_authorized=false`.
- **015** — hardening da IR: cache `Extract` limitado ao mesmo bloco, `IrSemanticAudit` integrado à HIR/CLI/planejador. Auditoria verifica integridade CFG, dominância, operandos, tipos, Phis, `Phi.incoming[predecessor]` correspondendo **exatamente** a `predecessor.output[slot]` e entrada inicial, memória `Load/Store` e chamadas `Opaque/CallOpaque`, `StateField(mem)`, sequência de tokens de memória dentro dos blocos e saída `block.output[mem]`. `ir-audit` e `hir-observe` expõem diagnóstico; transformações HIR nunca autorizadas sem semântica e provas.
  - Relatório: `docs/architecture/IMPLEMENTACAO_015_IR_SSA_INVARIANTS_2026-10-24.md`.
  - Arquivos: `native/workbench/src/decompiler_lift.cpp`, `src/ir_semantic_audit.cpp/.hpp`, `include/reworkbench/ir_semantic_audit.hpp`, `include/reworkbench/hir.hpp`, `src/hir_analysis.cpp`, `include/reworkbench/structuring_plan.hpp`, `src/structuring_plan.cpp`, `src/cli_main.cpp`, `native/workbench/CMakeLists.txt`.
  - **CÓDIGO SALVO; SEM BUILD, SEM TESTES, SEM BENCHMARK.**

Todo detalhe de 001–025: `docs/architecture/IMPLEMENTACAO_0XX_...`. Plano mestre: `docs/architecture/REWORK_DECOMPILER_MASTERPLAN_V1.md`. Rubrica: `docs/architecture/PROGRESSO_MASTERPLAN_0_A_100.md`.

## Limitações inegociáveis

A maior parte dos módulos 002–025 foi escrita SEM COMPILAÇÃO/TESTES. Podem existir problemas de C++/Qt, lógica SSA/CFG, efeitos, chamadas e ABI. Não declarar que comandos CLI compilam, que IR é correta para um corpus real ou que Rework alcançou paridade com Ghidra.
`IrSemanticAudit.structural_checks_passed` é só consistência da IR MODELADA. `semantic_equivalence_proven=false` e `rewrite_authorized=false`. O planner não altera o CFG, não faz constant propagation, não autoriza remoção de memória, não emite C e retém o fallback.
`TypeEvidence.confirmed_ir` é somente fato da IR gerada, não um tipo C++ recuperado. Chamada opaca é barreira de memória/controle.
**Importante:** as edições de 015–025 são entregas de código escrito ou preparação de validação; testes conjuntos e bugs desconhecidos continuam dívida técnica.

## Etapa 016 — código escrito, não validado

`include/reworkbench/phi_transfers.hpp` e `src/phi_transfers.cpp`: contratos de atribuição SIMULTÂNEA para `Phi` por aresta `predecessor -> successor`, incluindo entrada sintética `-1`, preservando ID SSA, representação bitvector/estado, identidade, dependências e ciclos de cópias paralelas. Rejeita entradas duplicadas/ausentes, fontes fora da HIR e arestas CFG inválidas; bloqueia lowering opaco ou de tokens de memória sem garantias. Limites e incompletude explícitos, `rewrite_authorized=false`. CMake inclui `src/phi_transfers.cpp`. CLI inclui comando `phi-transfers`, seção no `hir-observe`, e o `structuring-plan` exige cobertura `Phi` antes de considerar candidatos estruturais. Não gera C nem altera IR/CFG.
Relatório: `docs/architecture/IMPLEMENTACAO_016_PHI_EDGE_TRANSFERS_2026-10-24.md`.

## Etapa 017 — código escrito, não validado

`include/reworkbench/parallel_copy_plan.hpp` e `src/parallel_copy_plan.cpp`: plano transacional de cópias SSA `Phi` por aresta com snapshots de todas as fontes escalares distintas ANTES de qualquer escrita; compartilha temporário virtual quando destinos usam a mesma fonte; resolve como PLANO SIMBÓLICO as dependências e ciclos sem escrita física. Identidades viram `identity_noop`; tokens mem/arch/opaque ficam `symbolic_state`, sem reordenação de efeitos. Auditoria de IDs, temp/contagens/ordem e bloqueadores. CMake inclui novo CPP. CLI `phi-copy-plan` e `hir-observe.phi_copy_plan`. `structuring-plan` observa integridade do escalonamento; `rewrite_authorized=false`, `lowering_executed=false`. Não compila, não testa, não commita ou envia.
Relatório: `docs/architecture/IMPLEMENTACAO_017_PARALLEL_COPY_PLAN_2026-10-24.md`.

## Etapa 018 — código escrito, ainda sem compilação/testes

`include/reworkbench/edge_effects.hpp` e `src/edge_effect_analysis.cpp`: contrato de fronteira por aresta `predecessor -> successor`, captura `predecessor_exit_after_modeled_effects` e aplicação conceitual em `successor_entry_phi_merge`; entrada sintética `-1` tratada separadamente. Analisa e mantém a ordem OBSERVADA dos IDs IR de `Load/Store/Opaque/CallOpaque` e projeções do predecessor; identifica arestas CFG inválidas, terminadores desconhecidos, loads potencialmente falhos e barreiras opacas. Tokens de memória/arquitetura permanecem simbólicos. `exceptional_control_unknown=true`, `effect_order_preservation_proven=false`, `lowering_executed=false`, `rewrite_authorized=false`. Sem mudar HIR/IR, UI ou pseudocódigo.
CMake adiciona `src/edge_effect_analysis.cpp`, CLI possui `edge-effects`, `hir-observe.edge_effects` e campos de integridade no planejador `structuring-plan`.
Relatório: `docs/architecture/IMPLEMENTACAO_018_EDGE_EFFECT_CONTRACTS_2026-10-24.md`.

## Etapa 019 — código escrito, NÃO VALIDADO

`include/reworkbench/control_safety.hpp` e `src/control_safety_analysis.cpp`: normalização OBSERVACIONAL dos terminadores do CFG (conditional/goto/return/opaque), verificação de braços tomados/não tomados, reciprocidade de arestas e detecção de `critical_edge` (origem com múltiplos sucessores e destino com múltiplos predecessores). Relaciona contratos 018 e identifica onde `edge splitting` pode ser necessário se transferências Phi forem baixadas no futuro. Detecta `opaque_call` e preserva como NÃO VERIFICADA a possibilidade `noreturn`. Exceções/indiretos continuam desconhecidos, sem fabricar destinos. CMake inclui CPP, CLI possui `control-safety`, `hir-observe.control_safety` e métricas novas no `structuring-plan`. NÃO reescreve CFG/HIR, não gera C, `rewriting_authorized=false`.
Relatório: `docs/architecture/IMPLEMENTACAO_019_CONTROL_SAFETY_2026-10-24.md`.

## Etapa 020 — auditoria de fontes e preparação de corpus (NÃO TESTADA)

**Inventário estático realizado (consulta apenas):** 59 caminhos CMake de C++/QRC inspecionados, 0 arquivos ausentes nesta verificação; includes locais de cinco fontes C++ críticas inspecionados sem ausência detectada; dez nomes de comandos CLI encontrados em múltiplas ocorrências textuais. Isso NÃO certifica build ou funcionamento.

**Novos arquivos:**
- `tests/fixtures/src/decompiler_stage020.c`: corpus C de CFG/SSA/Phi, laços, trocas, chamadas opacas, volatile mem, indiretos, signedness.
- `tests/fixtures/src/decompiler_stage020_stubs.c`: implementações auxiliares de externals e chamada que PODE retornar.
- `tools/rework_stage020_inventory.py`: ferramenta futura de leitura estática de registros CMake/includes/CLI; NÃO executada.
- `docs/architecture/VALIDACAO_CORPUS_STAGE020_2026-10-24.md`: matriz de casos/expectativas e critérios de aceite, todos NÃO EXECUTADOS.
- `docs/architecture/IMPLEMENTACAO_020_INTEGRATION_AUDIT_2026-10-24.md`: relatório e riscos da etapa.

Nenhuma etapa 002–021 foi validada por compilação/regressão em lote. **Nenhum teste/build/benchmark, commit ou push realizado na 021.**

## Etapa 021 — correções estáticas (NÃO TESTADAS)

- `native/workbench/src/control_safety_analysis.cpp`: a conferência de arestas agora falha quando o terminador correspondente é inconsistente; faltas de contrato de efeito contribuem para `partial`.
- `native/workbench/src/structuring_plan.cpp`: candidatos `if/while` exigem terminadores resolvidos e nenhum efeito de aresta pendente; bloqueadores explícitos para ambos.
- `native/workbench/src/ir_semantic_audit.cpp`: cada Phi de bloco alcançável precisa constar da lista de entradas; cada outro nó do bloco deve constar de `definitions`. Uma definição omitida invalida a auditoria de ordem de memória.
- Relatório: `docs/architecture/IMPLEMENTACAO_021_STATIC_SAFETY_FIXES_2026-10-24.md`.

**Importante:** alterações escritas e inspecionadas como texto, não compiladas ou testadas. Não foi autorizada reescrita da IR, AST C ou otimização.

## Etapa 022 — contratos da IR e integridade de Phi (ESCRITA, NÃO TESTADA)

- `native/workbench/src/ir_semantic_audit.cpp`: verifica aridade, largura, tipos e offsets de Input/Constant, aritmética, comparação, lógica, Extract/Insert, extensões, Select e Parity. Violações bloqueiam o sinal de integridade estrutural; não provam semântica ISA.
- `native/workbench/src/phi_transfers.cpp`: impede que IDs de Phi incompatíveis, bloco proprietário inválido/inalcançável ou HIR parcial recebam `all_assignments_well_formed=true`.
- Relatório: `docs/architecture/IMPLEMENTACAO_022_IR_OPERATOR_PHI_AUDIT_2026-10-24.md`.

**Sem compilação, execução de testes, benchmark, commit ou push.** Incrementos 002–023 ainda precisam de validação conjunta.

## Etapa 023 — barreiras opacas de memória (ESCRITA, NÃO TESTADA)

`native/workbench/src/ir_semantic_audit.cpp`: mantém `pending_opaque` por bloco. A auditoria rejeita Load/Store durante uma chamada/instrução opaca sem projeção `StateField(mem)`, múltiplos efeitos opacos sobrepostos, projeções que se referem ao efeito errado, projeções extras e barreira sem projeção ao fim do bloco. Novos erros: `memory_operation_crosses_opaque_barrier`, `nested_unresolved_opaque_barrier`, `memory_projection_wrong_effect`, `unresolved_opaque_memory_projection`. Violação invalida `memory_state_order_consistent` e impede liberação da estruturação, sem alterar HIR/CFG.
Relatório: `docs/architecture/IMPLEMENTACAO_023_OPAQUE_MEMORY_BARRIERS_2026-10-24.md`.

## Etapa 024 — projeção completa de estado opaco (ESCRITA, NÃO TESTADA)

`native/workbench/src/ir_semantic_audit.cpp`: rastreia `pending_state_effect` e todos os slots de estado ainda não projetados após `Opaque/CallOpaque`. O contrato exige cada `StateField` exatamente uma vez e do efeito correto, sem outra definição interrompendo o pacote de projeções. Falhas geram `opaque_state_projection_interrupted`, `opaque_state_projection_order` ou `opaque_state_projection_incomplete`, tornando `memory_state_order_consistent=false`. O check separado de `mem` da etapa 023 continua ativo, e não há reescrita da HIR.
Relatório: `docs/architecture/IMPLEMENTACAO_024_OPAQUE_STATE_PROJECTIONS_2026-10-24.md`.
**Não compilado, testado, benchmarkado, commitado ou enviado ao GitHub.**

## Etapa 025 — proveniência SSA e saídas de bloco (ESCRITA, NÃO TESTADA)

`native/workbench/src/ir_semantic_audit.cpp` reforça a auditoria SSA: `Block.output[slot]` é tratado como uso implícito no fim do bloco e deve vir de uma definição pertencente ao bloco, de bloco dominante, ou de `Op::Input` global legítimo. Nó global não-`Input` gera `invalid_global_definition`; operandos globais falsos geram `non_input_global_operand`; `Phi` sintético exige origem `Op::Input`, e uma fonte global inválida gera `phi_invalid_global_source`. Saída não dominante gera `block_exit_non_dominating_value`. Nada é reescrito.
Relatório: `docs/architecture/IMPLEMENTACAO_025_SSA_BLOCK_OUTPUT_DOMINANCE_2026-10-24.md`.
**Sem compilação, testes, benchmarks, commit ou push.**

## Etapa 026 — contratos SSA dos terminadores (ESCRITA, NÃO TESTADA)

`native/workbench/src/ir_semantic_audit.cpp`: a auditoria passa a tratar `Block.condition` e `Block.return_target` como usos SSA implícitos na saída do bloco. Condicionais exigem predicado Bits(1), dominância e não mais que dois sucessores observados. Retornos exigem destino de largura de ponteiro, dominância e nenhum sucessor CFG. Goto não admite mais de uma saída normal; bloco alcançável sem terminador é sinalizado. Não são inventados saltos indiretos, exceções ou destinos desconhecidos. Nenhuma reescrita autorizada.
Relatório: `docs/architecture/IMPLEMENTACAO_026_TERMINATOR_SSA_CONTRACTS_2026-10-24.md`.
**Nenhum build, teste, benchmark, commit ou push.**

## Etapa 027 — consistência dos destinos CFG (ESCRITA, NÃO TESTADA)

`native/workbench/src/ir_semantic_audit.cpp`: a auditoria agora exige que endereços de entrada dos blocos mapeiem corretamente via `block_at`, detecta arestas de origem ou destino duplicadas e compara cada sucessor observado com endereços `taken`/`not_taken` existentes. Se um endereço recuperado resolve para um bloco conhecido, exige a aresta correspondente. Não inventa saltos indiretos, destinos externos ou exceções; não transforma a IR.
Códigos: `cfg_block_address_index_mismatch`, `cfg_duplicate_successor`, `cfg_duplicate_predecessor`, `cfg_successor_without_target`, `cfg_resolved_target_missing_edge`. Falhas impedem `cfg_consistent` e `structural_checks_passed`.
Relatório: `docs/architecture/IMPLEMENTACAO_027_CFG_EDGE_TARGET_CONSISTENCY_2026-10-24.md`. **Sem compilação, testes, benchmarks, commit ou push.**

## Etapa 028 — conferência independente de alcance CFG (ESCRITA, NÃO TESTADA)

`native/workbench/src/ir_semantic_audit.cpp`: recalcula alcance a partir de `Program.entry`, por lista de trabalho que percorre sucessores válidos sem confiar nos flags `Block.reachable`. Compara o conjunto descoberto com os flags armazenados, incluindo blocos desconectados. Também exige `Program.block_at.size()==blocks.size()` e confirma endereço, índice e espaço de TODOS os blocos, mesmo inalcançáveis. Problemas são registrados como `cfg_reachability_flag_mismatch`, `cfg_block_index_cardinality`, `cfg_any_block_address_mismatch`; sinalizam `cfg_consistent=false`, sem alterar IR/CFG.
Relatório: `docs/architecture/IMPLEMENTACAO_028_CFG_REACHABILITY_INDEX_AUDIT_2026-10-24.md`.
**Sem compilação, testes, benchmarks, commit ou push.** Incrementos 002–028 aguardam validação conjunta.

## Etapa 029 — integridade da partição de instruções no CFG (ESCRITA, NÃO TESTADA)

`native/workbench/src/ir_semantic_audit.cpp`: verifica que todo bloco tem instruções, índices não negativos e nenhuma referência de instrução duplicada dentro de um bloco ou em vários. A contagem de referências de instruções dos blocos alcançáveis precisa corresponder a `Program.total` (contado pelo Builder), e `Program.modeled` não pode superar `Program.total`. Falhas invalidam `cfg_consistent` e `structural_checks_passed`, sem modificar a IR/CFG. Não há limite superior conferível para índices sem tamanho do snapshot original; índices não são exigidos monotônicos. Códigos: `cfg_empty_instruction_block`, `cfg_negative_instruction_index`, `cfg_duplicate_instruction_owner`, `cfg_reachable_instruction_count`, `cfg_modeled_exceeds_total`.
Relatório: `docs/architecture/IMPLEMENTACAO_029_CFG_INSTRUCTION_PARTITION_2026-10-24.md`.
**Nenhuma compilação, teste, benchmark, commit ou push realizados.**

## Etapa 030 — tradução atômica com fallback opaco (ESCRITA, NÃO TESTADA)

`native/workbench/src/decompiler_lift.cpp`: `Builder::run` agora captura os tamanhos de `Program.nodes` e `Block.definitions`, estado SSA, informações de comparação e campos do terminador antes de cada `translate(instruction)`. Se a tradução retornar `false`, restaura o snapshot, descarta os nós provisórios e limpa o cache de `Extract` (para não referenciar Value IDs descartados) ANTES de aplicar `Opaque/CallOpaque`. Se a tradução tiver êxito, mantém os nós e contabiliza o `modeled` normalmente. O objetivo é impedir que tradução incompleta crie efeitos especulativos junto ao fallback. Não foi executado.
Relatório: `docs/architecture/IMPLEMENTACAO_030_TRANSLATION_ATOMIC_FALLBACK_2026-10-24.md`.
**Sem compilação, testes, benchmarks, commit ou push.**

## Etapa 031 — extensão com sinal de imediatos x86/x64 (ESCRITA, NÃO TESTADA)

`native/workbench/src/decompiler_lift.cpp`: o método `read` aceita `sign_extend_immediate` e usa `Operand.width_bits` para estender o bit de sinal quando a origem é imediata e mais estreita que o destino. O caminho `add/sub/cmp/and/or/xor/test` e `push` solicitam essa interpretação. Em `mov r/m64, imm32`, somente uma origem marcada explicitamente com largura 32 recebe extensão; `movabs` não muda. Registradores e memória mantêm lógica anterior. A correção depende da largura que o Capstone expõe; não é validação ISA.
Relatório: `docs/architecture/IMPLEMENTACAO_031_X86_IMMEDIATE_SIGN_EXTENSION_2026-10-24.md`.
**Nenhuma compilação, testes, benchmarks, commit ou push realizados.**

## Etapa 032 — largura codificada de imediatos x86/x64 (ESCRITA, NÃO TESTADA)

`native/workbench/include/reworkbench/types.hpp`: `Operand.encoded_immediate_bits` representa separadamente a largura realmente codificada, com zero para não verificada. `native/workbench/src/decoder.cpp`: em x86/x64 Capstone 5, usa `encoding.imm_offset/imm_size` e os limites dos bytes da instrução para preencher essa largura. `native/workbench/src/decompiler_lift.cpp`: aritmética, PUSH e MOV r/m64 com imediato consultam o tamanho codificado para extensão com sinal; se não há encoding confiável no caso sensível, retorna `nullopt` e usa fallback opaco com rollback (etapa 030), sem inferir largura de `x86_op.size`.
Relatório: `docs/architecture/IMPLEMENTACAO_032_X86_ENCODED_IMMEDIATE_WIDTH_2026-10-24.md`.
**Nenhuma compilação, testes, benchmark, commit ou push realizados.** A semântica ISA e compatibilidade do Capstone ainda não foram validadas.

## Etapa 033 — XCHG entre registradores e flags de carry (ESCRITA, NÃO TESTADA)

`native/workbench/src/decompiler_lift.cpp`: suporte a `clc` (CF=0), `stc` (CF=1) e `cmc` (inversão de CF) por operações IR existentes, sem modificar as demais flags. `xchg` entre dois registradores de largura igual lê ambos os valores antes das duas escritas, mantendo semântica de subregistradores e zero-extension de 32 bits no x64. `xchg` com memória e prefixos não suportados permanecem opacos por atomicidade, sem emitir Load/Store comuns. A interpretação de `nop` foi preservada.
Relatório: `docs/architecture/IMPLEMENTACAO_033_X86_REGISTER_EXCHANGE_CARRY_FLAGS_2026-10-24.md`.
**Código escrito, NÃO compilado/testado/benchmarkado, sem commit ou push.**

## Etapa 034 — BSWAP x86/x64 de 32/64 bits (ESCRITA, NÃO TESTADA)

`native/workbench/src/decompiler_lift.cpp`: o lifter agora aceita `bswap` com um registrador escriturável de 32 ou 64 bits, sem prefixo de tamanho de operando. Lê o valor antes da transformação e constrói a inversão de bytes por `Op::Extract` + `Op::Insert`, preservando as flags. A escrita utiliza a lógica existente para os subregistradores, inclusive extensão com zeros para 32 bits em x64. Formas de 16 bits, memória e operandos inválidos seguem no fallback opaco.
Relatório: `docs/architecture/IMPLEMENTACAO_034_X86_BSWAP_BITVECTOR_LIFT_2026-10-24.md`.
**Não compilado, testado, benchmarkado, commitado ou enviado.** Não há comprovação de equivalência ISA.

## Etapa 035 — CBW/CWDE/CDQE e extensão do acumulador (ESCRITA, NÃO TESTADA)

`native/workbench/src/decompiler_lift.cpp`: o lifter modela `cbw`, `cwde` e `cdqe` sem operandos explícitos, usando `register_info`, `read_register`, `adapt(..., true)` e `write_register`. CBW estende AL 8→AX 16; CWDE estende AX 16→EAX 32 e, no x64, zera os 32 bits superiores de RAX conforme a regra de escrita em EAX; CDQE estende EAX 32→RAX 64 apenas no modo x64. Flags e memória não são alteradas. Formas não reconhecidas continuam no fallback opaco da etapa 030.
Relatório: `docs/architecture/IMPLEMENTACAO_035_X86_ACCUMULATOR_SIGN_EXTENSION_2026-10-24.md`.
**Alteração de fonte; NÃO compilada, testada, benchmarkada, commitada nem enviada.**

## Próxima missão 036

Prioridade: executar compilação/regressão 002–035 **somente mediante autorização expressa**. Se continuar proibido, revisar `CWD/CDQ/CQO` e outras semânticas ISA conservadoras sem declarar runtime funcional.

## Ritual de retomada

1. Confirmar Desktop Commander, `main`, HEAD e Git status.
2. Ler ESTE checkpoint, `PROGRESSO_MASTERPLAN_0_A_100.md`, `IMPLEMENTACAO_035_X86_ACCUMULATOR_SIGN_EXTENSION_2026-10-24.md` e `IMPLEMENTACAO_034_X86_BSWAP_BITVECTOR_LIFT_2026-10-24.md`.
3. Inspecionar os arquivos reais antes de editar; jamais adotar sem verificação um estado antigo do worktree da feature.
4. Desenvolver a etapa 036 conforme autorização do usuário: sem build/teste/benchmark enquanto não liberado. Sem commit/push salvo autorização específica.
5. Atualizar o relatório 036, este checkpoint e o percentual de progresso.
