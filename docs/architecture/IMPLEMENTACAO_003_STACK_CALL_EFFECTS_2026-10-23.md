# Rework — incremento 003: observação conservadora de argumentos na pilha e efeitos de CALL

**Data:** 2026-10-23
**Estado:** código escrito no worktree `feat/reworkbench`; NÃO COMPILADO E NÃO TESTADO nesta rodada, conforme solicitação.
**Escopo:** expandir o módulo de observação de ABI. Não alterar a semântica de execução do decompilador.

## Implementação

- `native/workbench/src/stack_abi_observation.hpp` e `.cpp` (novos):
  - Interpretador afim **restrito** para valores da SSA: `endereço = RSP_inicial + deslocamento`.
  - Reconhece apenas `Input rsp`, constantes 64-bit, `Add/Subtract` e `Phi` com resultados idênticos e acíclicos.
  - Exclui instruções opacas, valores desconhecidos, `Phi` inconsistentes, offsets maiores que 1 MiB e operações não suportadas.
  - Detecta **possíveis leituras de stack parameters** em `Op::Load` de blocos alcançáveis.
  - Examina a cadeia de memória anterior a `Op::CallOpaque`: `Op::Store`, `Op::Phi`, `Op::Input mem`. Interrompe o caminho em estado de memória desconhecido, inclusive chamadas anteriores.
  - Registra escritas em posições que poderiam conter argumentos adicionais: Microsoft x64 em `RSP_antes_CALL+32` (e seguintes), SysV AMD64 em `RSP_antes_CALL+0` (e seguintes).
  - Para entradas de callee, posições: Microsoft x64 em `RSP_entrada+40` e seguintes; SysV em `RSP_entrada+8` e seguintes.
  - Valores são **possibilidades**, não número de argumentos ou prova de uso pela chamada. Slots de home/shadow-space do Windows **não são classificados como argumentos extras**.
  - Há limites por profundidade, memória percorrida, operações examinadas e entradas reportadas, além de cancelamento.
- `native/workbench/include/reworkbench/call_effects.hpp` e `src/call_effects.cpp` (novos):
  - Contrato tipado para efeitos de chamada, separando listas **candidatas** de GPRs voláteis/preservados dos efeitos efetivamente **desconhecidos**.
  - Mesmo em ABI candidata, registros rastreados, memória, fluxo de controle, vetores, retorno e efeitos externos permanecem desconhecidos.
  - Não preserva registradores, não reduz efeitos de `CallOpaque` e não otimiza sobre chamadas.
- `native/workbench/include/reworkbench/function_abi.hpp` e `src/function_abi.cpp`:
  - Integram possíveis leituras da pilha, escritas antes de callsites, limites e evidência de memória desconhecida.
  - Mantêm detecção de registradores, produtores de valores e ligações a funções já descobertas.
- `native/workbench/src/cli_main.cpp`:
  - `abi-observe` passa a serializar `possible_incoming_stack_reads` e `possible_outgoing_stack_stores`.
  - Os offsets expõem a base correta (`callee_rsp_at_entry` ou `caller_rsp_before_call`), endereço de instrução e largura em bits.
  - O resultado de cada chamada inclui `effects`, `stack_memory_path_unknown` e flags de limites.
- `native/workbench/CMakeLists.txt` inclui os dois novos `.cpp`.

## Hipóteses que NÃO são feitas

1. Escrita na pilha compatível com a ABI NÃO prova um argumento: pode ser spill, temporário, variável local ou valor sobrescrito.
2. Um endereço lido com offset de argumento NÃO prova tipo, nome ou posição do parâmetro no código-fonte.
3. Convenção inferida pelo formato PE/ELF ou selecionada pelo usuário NÃO verifica uma função específica.
4. ABI NÃO prova ausência de efeitos de memória, exceções, chamadas especiais ou registradores vetoriais.
5. Leituras ou escritas desconhecidas por IR limitada permanecem desconhecidas; evidência negativa não prova ausência.
6. A análise não interpreta varargs, stack probes, AVX, argumentos vetoriais, tail calls, chamadas não padrão e estruturas C++.

## Próximos blocos de desenvolvimento

- Normalização das classes de armazenamento com candidatos de variável/argumento, mantendo provenance.
- Análise de stack frame por função com ranges de vida e efeitos de alias.
- Contratos por função e recuperação conservadora de efeitos interprocedurais.
- Assinatura de alto nível e HIR com classificação `confirmed/inferred/unknown`.
- Suite integrada futura para ABI Win64/SysV x64, O0/O2, chamadas diretas/indiretas, parâmetros de pilha, loops, corrupção de CFG, colisão de memória, exceções e regressões.
- Quando for autorizado testar em lote: compilar CLI+GUI, executar suite nativa, verificar saída `abi-observe` e comparar em fixtures autossuficientes com Ghidra.

Nenhum build, teste ou benchmark foi executado nesta rodada; portanto, funcionalidades descritas **não estão validadas**.
