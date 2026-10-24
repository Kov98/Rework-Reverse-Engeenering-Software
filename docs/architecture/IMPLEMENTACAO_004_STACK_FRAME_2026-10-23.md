# Rework — incremento 004: catálogo de stack frame / candidatos a armazenamento

**Data:** 2026-10-23  
**Estado:** CÓDIGO SALVO, AINDA NÃO COMPILADO NEM TESTADO, por instrução do usuário.  
**Branch/worktree:** `feat/reworkbench`, pasta `.worktrees/implementation`.  
**Escopo:** detecção preliminar de faixas de memória relativas ao RSP de entrada; não é recuperação de variáveis C/C++ nem de seus tipos.

## Módulos novos

- `native/workbench/include/reworkbench/stack_frame.hpp`: API de `StackFrameReference`, `StackSlotCandidate` e `StackFrameObservation`.
- `native/workbench/src/stack_frame_analysis.hpp`: interface interna de análise sobre a `Program` de baixo nível já calculada.
- `native/workbench/src/stack_frame_analysis.cpp`: monta candidatos de slots, agrega proveniência, separa tamanhos e posições, marca sobreposições e agrupa por região.

## Módulos ampliados

- `src/stack_abi_observation.hpp/.cpp`: `frame_accesses()` reutiliza o resolvedor de expressões afins com RSP; recolhe leituras/escritas de `Load/Store` em blocos alcançáveis. Considera funções 64-bit, larguras escalares 8/16/32/64 e reporta endereços de memória não resolvidos e presença de instruções opacas.
- `include/reworkbench/function_abi.hpp`: adiciona `StackFrameObservation frame` ao resultado.
- `src/function_abi.cpp`: invoca a análise sobre a **mesma IR** obtida com `lift`, sem duplicar a geração; sinaliza análise parcial quando a análise do frame é parcial.
- `src/cli_main.cpp`: inclui `observation.stack_frame` na resposta do comando `abi-observe`, com slots, evidência, leituras, escritas, tamanho, região, sobreposição, contadores e avisos.
- `native/workbench/CMakeLists.txt`: registra a nova unidade de compilação `stack_frame_analysis.cpp`.

## Classificação preliminar de endereços

Referência comum: `RSP` **na entrada da função callee**.

- Deslocamento negativo: `local_spill_or_outgoing_area`; pode conter variável local, spill, temporário, argumento para outro call ou dados sem correspondência direta com fonte.
- Região `[RSP, RSP+8)`: `return_address_area`; não declarar variável.
- ABI Windows x64, `[RSP+8,RSP+40)`: `home_space_or_caller_storage`; NÃO presumir que os valores são argumentos da função.
- Endereço compatível com primeiro argumento por pilha da ABI em diante: `incoming_argument_address_candidate`; não presume uso efetivo como parâmetro da linguagem de origem.
- Faixa cruzando limite de regiões ou caso não reconhecido: `unknown_or_boundary_overlapping_area`.

Todas as classificações estão condicionadas ao candidato de ABI; podem ser inválidas em syscalls, thunks, custom ABI, funções naked, exceções, frames não usuais etc.

## Invariantes

- Identidade de slot é (`offset relativo ao RSP de entrada`, `largura em bytes`).
- Ranges sobrepostos não são fundidos; um objeto pode conter vários acessos/aliases e um endereço pode ser reutilizado em tempos diferentes.
- Proveniência de cada acesso preserva o endereço da instrução, sua largura e se era `Load` ou `Store`.
- Presença de `CallOpaque`/`Opaque` sinaliza efeitos de estado desconhecidos. A ausência de uma leitura registrada não prova ausência de acesso.
- Valores de endereço não comprovados como `RSP_entrada + deslocamento` não são usados para deduzir um slot.
- Limites: máximo 2.048 operações de memória examinadas, máximo 512 slots, até 64 registros de evidência por slot, com marcadores de truncamento.
- Nenhum slot gera nome de variável, classe, assinatura, tipo complexo, tempo de vida ou mutação do pseudocódigo.
- A versão compilada do Rework continua sendo a anterior; as modificações deste incremento só terão efeito após integração e futura compilação autorizada.

## Próximos incrementos recomendados

1. Unificar os acessos por intervalos e liveness **somente após** análises de memória e dominadores; distinguir stack slots fisicamente sobrepostos e reutilizados em caminhos/tempos diferentes.
2. Propagar contratos de efeitos entre chamadas com prova baseada em corpo recuperado + ABI verificada; manter desconhecido para calls indiretas/exóticas.
3. Recuperar argumentos e retornos como `TypeEvidence`/proveniência, não como declarações fictícias.
4. Projetar `HIR` e emitter C-like com controle de completude e fallback explícito para intrínsecos opacos.

## Validação pendente para rodada conjunta

- Compilação C++20 de CLI e GUI e análise de warnings.
- Suite de regressão existente e casos Win64/SysV com prologues, home/shadow space, múltiplos stores, leaf functions, CALL opaco e stack pointer variável.
- Casos negativos com ESP/RSP desconhecido, aliasing, offsets inválidos, accessos cruzando limites, CFG irreducível e ABI errada.
- Differential tests contra o Ghidra usando artefatos próprios, quando o usuário autorizar execução de testes.

**Nenhuma compilação nem teste realizado durante esta rodada.**
