# Rework — Incremento 011: TypeEvidence, IR width e usos tipados

**Data:** 2026-10-23
**Estado:** CÓDIGO SALVO, NÃO COMPILADO, NÃO TESTADO, NÃO COMMITADO, por solicitação do usuário.
**Branch:** `feat/reworkbench` worktree `.worktrees/implementation`.
**Progresso global editorial:** 11/100 (PROVISÓRIO; NÃO reflete equivalência medida com Ghidra).

## Entrega

1. `native/workbench/include/reworkbench/type_evidence.hpp`
   - Tipos públicos `TypeClue`, `ValueTypeEvidence`, `TypeEvidenceObservation`.
   - Cada evidência tem significado, origem (definição IR, uso como operando, candidato ABI), confiança (confirmed_ir/inferred/unknown), instrução, ID IR e detalhe.
   - Por nó IR: representação física, largura, uso em signed/unsigned, uso como expressão de endereço, conflito entre interpretações, limitação de amostras, `source_type_status: unknown`.
   - API `observe_function_types(snapshot, function_index, stop)`, sem obrigar a descobrir ABI.

2. `native/workbench/src/type_evidence_analysis.hpp/.cpp`
   - Análise read-only de `decompiler_detail::Program` existente; valor e largura da IR são fatos **estruturais** do lifter, não prova da semântica do programa original.
   - O uso como operador de comparação, SignExtend e ZeroExtend gera pistas de contexto para cada OPERANDO; um bitvector de 64 bits NÃO vira `int64_t` automaticamente.
   - Um valor usado como operando de endereço em Load/Store ganha pista `address_expression_use`, NÃO prova de ponteiro de fonte ou ownership.
   - `Input` coincidente com registradores de candidato Windows x64 ou SysV AMD64 ganha pista `possible_abi_argument_register`, sem afirmar parâmetro-fonte.
   - Comparações ou operações assinadas e não assinadas sobre o MESMO nó IR preservam pistas divergentes e marcam `interpretation_conflict`; nenhuma escolha de signedness é forçada.
   - Resultados de comparações/BooleanNot ganham apenas papel candidato de predicado, sem provar `bool` na linguagem-fonte.
   - Não interpreta types de debug info por conta própria. Nomes de símbolos não são declarações de tipo.
   - Limites: 32768 nós IR inspecionados, até 1536 valores reportados, 12 clues por valor, com flags de truncamento, cancelamento e tratamento de `Limit`.
   - Não transforma IR, não troca pseudocódigo nem usa inferência para otimização.

3. Integrações
   - `native/workbench/include/reworkbench/function_abi.hpp`: campo `TypeEvidenceObservation type_evidence`.
   - `native/workbench/src/function_abi.cpp`: produz TypeEvidence sobre a MESMA `Program` levantada para `abi-observe`, sem segundo lift.
   - `native/workbench/src/cli_main.cpp`: `type_evidence_json()`, comando independente `type-observe`, inclusão também em `abi-observe.observation.type_evidence`; saída capada em 256 valores, com contadores totais, conflitos e incerteza explícita.
   - `native/workbench/CMakeLists.txt`: inclui `src/type_evidence_analysis.cpp`.

## Uso PREVISTO — NÃO EXECUTADO

```powershell
reworkbench-cli type-observe --input caminho\exemplo.exe
reworkbench-cli abi-observe --input caminho\exemplo.exe
```

O primeiro não exige ABI para recolher pistas da IR. Ambos continuam indisponíveis no executável atualmente instalado até build posterior.

## Limites e validação acumulada

- Tipos C, C++, Rust, Java, C#, JS não estão recuperados. Classe C++ e ownership exigem muito mais que pistas de largura/endereço.
- `confirmed_ir` significa somente que a IR tem aquela largura/operação; um lifter incompleto ou incorreto não valida semântica de máquina.
- Ausência de clue não prova ausência de tipo; dados não modelados, limitação de nós, calls opacas e dados sem debug permanecem unknown.
- Debug info real (PDB/DWARF), símbolos tipados e fontes externas precisarão de parser confiável e proveniência própria, antes de classificar qualquer tipo como confirmado.
- Futura validação CONJUNTA dos incrementos 002–011 (somente quando autorizada): build CLI/GUI, C++20/Qt, teste de IR bitwidth/predicate/signed-vs-unsigned/address-use/ABI candidato/truncamento, regressões, CMake, corpus diferencial Ghidra.

## Próximo incremento 012

Projetar **HIR tipada mínima** e adapter de valores IR -> operações de nível intermediário, com `source_type_status: unknown`, efeitos explícitos e instrução de proveniência. Não gerar código C/C++ falsamente legível ou compilável; construir estrutura primeiro e fallback para operações não modeladas.

Não houve build, test, commit ou benchmark na etapa 011.
