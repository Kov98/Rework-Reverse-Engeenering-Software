# REwork — Etapa 034: BSWAP x86/x64 por operações bitvetoriais existentes

**Data:** 24/10/2026.
**Repositório:** `C:\Users\pedri\OneDrive\Documents\PROJETO\Rework Reverse Engeenering Software` (`main` local).
**Situação:** implementação C++ escrita e inspecionada como fonte, **NÃO compilada, testada ou benchmarkada, sem commit/push**.
**Progresso editorial estimado:** 34/100 (34%, provisório); não é medição de correção, cobertura real nem paridade Ghidra.

## Motivação

O lifter em `native/workbench/src/decompiler_lift.cpp` já possui `Op::Extract`, `Op::Insert`, constantes e escrita de registradores. Faltava a semântica de `BSWAP`, usada para inverter a ordem dos bytes de valores de 32 e 64 bits. Antes disso, a instrução era opaca mesmo quando operava exclusivamente sobre um registrador com largura conhecida.

A auditoria `ir_semantic_audit.cpp` já verifica as larguras/offsets de Extract e Insert. O avaliador de fatos `decompiler.cpp` já interpreta ambos os operadores.

## Código implementado

`native/workbench/src/decompiler_lift.cpp`, em `Builder::translate`:

1. Reconhece `bswap` apenas com **um registrador escriturável** de **32 ou 64 bits** e sem `operand_override`.
2. Lê o valor SSA do registrador **uma vez**, antes da substituição.
3. Inicializa `reversed` com constante zero da largura da operação.
4. Para cada byte do valor original, extrai `Extract(original, offset, 8)` e o insere via `Insert` no offset `width - 8 - offset` do resultado.
5. Grava o resultado pelo `write_register` existente, inclusive a extensão com zeros para destino de registrador de 32 bits no modo x64.
6. Não altera CF/OF/SF/ZF/AF/PF, tokens de memória ou flags arquiteturais.
7. `BSWAP` de 16 bits (semântica indefinida), formas com operandos inválidos, memória e prefixo de tamanho de operando permanecem no fallback opaco. Prefixos `LOCK/REP` já eram rejeitados genericamente.

A modificação não introduz novo opcode de IR ou reescrita de CFG e não tenta implementar qualquer atomicidade. O fallback transacional da etapa 030 continua disponível.

## Casos de validação futura — NÃO EXECUTADOS

- `bswap eax` sobre `0x12345678` deve produzir `0x78563412`.
- `bswap rax` sobre `0x0102030405060708` deve produzir `0x0807060504030201`.
- `bswap r8d` em x64: bytes invertidos nos 32 bits inferiores e parte superior de R8 zerada.
- `bswap eax` repetido duas vezes deve restaurar o valor de 32 bits.
- Entrada `0`, todos bits 1 e `0x80000001`, sem alterar flags.
- Prefixo `0x66` e operações que não usam registrador de largura 32/64: permanecer opacas.
- Auditoria de IR `Extract/Insert`, estruturação e interpretação de constantes após compilação autorizada.

## Limitações

Sem compilação, testes, execução ou comparação diferencial, não há prova de funcionamento. Os incrementos 002–034 continuam pendentes de validação integrada. A integridade geral x86/ABI/efeitos, outros frontends e a paridade com Ghidra continuam objetivos não demonstrados.

## Próxima etapa 035

Priorizar compilação e regressões acumuladas **somente quando o usuário autorizar explicitamente**. Se não, prosseguir com revisão estática e implementação conservadora de semântica identificável no lifter.
