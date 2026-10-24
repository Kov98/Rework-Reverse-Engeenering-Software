# Rework — entrega 001: perfis de ABI x86-64

**Data:** 2026-10-23  
**Estado:** implementado, compilado e testado no CLI nativo.  
**Escopo:** infraestrutura para futura recuperação de argumentos, retornos e efeitos de chamadas.

## Código alterado

- `native/workbench/include/reworkbench/abi.hpp` (novo): contrato de convenção de chamada.
- `native/workbench/src/abi.cpp` (novo): perfis Microsoft x64 e System V AMD64 e sugestão limitada pelo formato.
- `native/workbench/CMakeLists.txt`: incorpora `abi.cpp` na biblioteca nativa.
- `native/workbench/src/cli_main.cpp`: expõe `abi_hint` em `inspect` e `decompile`.
- `tests/native/test_cli.py`: regressões PE64, ELF64, e recusa em casos sem evidências.
- `native/workbench/docs/abi.md`: contrato, limites e fontes de referência.

## Comportamento comprovado

- PE x86-64: candidato Microsoft x64, RCX/RDX/R8/R9, área de shadow 32 bytes, primeiro argumento adicional em RSP+40 na entrada.
- ELF x86-64: candidato SysV AMD64, RDI/RSI/RDX/RCX/R8/R9, red zone de 128 bytes para usuários, primeiro argumento adicional em RSP+8.
- PE32, Mach-O x86-64 e RAW x86-64: `status: unknown`. Não se inventa ABI.
- Mesmo com ABI sugerida, `verified_for_function: false`: não se afirmam tipos, parâmetros nem assinaturas recuperadas.
- A semântica das instruções e o pseudocódigo atuais permanecem inalterados.

## Validação executada

Build isolado: `%TEMP%\rework-abi-build3\bin\reworkbench-cli.exe`.
Compilação completa do CLI e da biblioteca: **sucesso (exit 0)**, MinGW GCC 13.1, Qt 6.8.3, Capstone 5, CMake Ninja, Release.
Para superar um caminho físico com espaços, a compilação utilizou temporariamente uma unidade virtual para a pasta de toolchain; a unidade foi removida ao concluir.

- `python -m pytest -q tests/native/test_cli.py -k "abi_hint or abi_is_not_guessed"`: **3 passed**.
- `python -m pytest -q tests/native/test_cli.py`: **25 passed, 2 skipped, 0 failed**, exit 0. Os dois testes pulados exigem opções de build para fixture de depuração e extensão de exemplo.

Não houve build do GUI nem benchmark comparativo contra Ghidra nesta entrega.
A compilação exibiu alguns avisos do código preexistente; nenhum erro.
Outras alterações não commitadas na branch `feat/reworkbench` foram preservadas; **nenhum commit foi criado**.

## Próximo incremento verificável

Implementar um módulo independente de análise de efeitos de chamadas a partir de uma ABI **validada para aquela função**, preservando memória/flags desconhecidos e evitando falsas certezas. Criar corpus próprio de Win64 e SysV AMD64 em O0, incluindo funções sem parâmetros, parâmetros em registradores/pilha, calls indiretas, tail calls, varargs e funções com ABI especial.

Em seguida, ligar os dados observados à IR/SSA e produzir hipóteses de parâmetros e retornos com proveniência, antes de expor uma assinatura C-like no pseudocódigo.

Referências:
- https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention
- https://gitlab.com/x86-psABIs/x86-64-ABI
