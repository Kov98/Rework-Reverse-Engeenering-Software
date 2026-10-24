# REwork — etapa 033: XCHG entre registradores e manipulação de CF no x86/x64

**Data:** 24/10/2026
**Pasta de trabalho:** `C:\Users\pedri\OneDrive\Documents\PROJETO\Rework Reverse Engeenering Software`, branch `main`.
**Status:** implementação de fonte escrita, **SEM COMPILAÇÃO/TESTES/BENCHMARKS/COMMIT/PUSH**.
**Progresso editorial estimado:** 33/100 = 33% provisórios; não é medida de desempenho, cobertura binária ou paridade Ghidra.

## Oportunidade encontrada

`Builder::translate`, em `native/workbench/src/decompiler_lift.cpp`, já modela escritas e leituras de registradores/subregistradores via `read`/`write` e operações bitvetoriais. Entretanto não tratava instruções `XCHG` entre dois registradores nem `CLC`, `STC` e `CMC`; elas caíam no fallback `Opaque`, tornando toda a função parcial e impedindo recuperação de estado útil onde a semântica era simples e representável.

## Código escrito

**`native/workbench/src/decompiler_lift.cpp`**:

- `clc` sem operandos: grava `CF=0` com `Op::Constant` de 1 bit.
- `stc` sem operandos: grava `CF=1` com `Op::Constant` de 1 bit.
- `cmc` sem operandos: grava `CF = BooleanNot(CF)` com `Op::BooleanNot` de 1 bit.
- A modificação de CF preserva os outros estados de flags, memória, arquitetura e registradores já modelados; comparação CMP prévia só pode ser reutilizada quando definições de flags ainda correspondem, conforme o guard existente no método `condition`.
- `xchg` exige **exatamente 2 operandos register-register**, ambos escrituráveis e com a mesma largura. Lê os dois valores originais antes de fazer quaisquer escritas, evitando destruir o valor da segunda origem. `write_register` mantém as regras existentes de subregistradores e zero-extension dos registradores de 32 bits no modo 64.
- `xchg` com operando de memória permanece **opaco**: memória x86 `XCHG` tem semântica de atomicidade implícita; emitir `Load`+`Store` comuns seria incorreto.
- Prefixos previamente rejeitados (`LOCK/REP` e segmentos) continuam rejeitados. Fluxo não normal continua voltando a `Opaque`. `nop` conserva sua interpretação anterior.
- Nenhum nó novo do enum `Op` nem alteração de formatos públicos; novas operações são expressas com infraestrutura já existente.

## Verificações que faltam — NÃO EXECUTADAS

1. `clc; jc`, `stc; jc` e `cmc; jc`: verificar predicados derivados de CF, evitando reutilização incorreta de `CMP`.
2. `xchg eax, ebx`, `xchg r8d, ecx`: verificar troca, atualização de SSA e extensão 32→64.
3. `xchg al, ah`: assegurar que leituras antecedem escritas e outros bits do registrador não se alteram.
4. `xchg eax, eax`: conferir comportamento da forma realmente decodificada, inclusive possíveis aliases de NOP.
5. `xchg [mem], reg` e `lock xchg`: manter opaco; nenhuma emissão de memória não atômica.
6. Instruções com prefixos inválidos ou operandos adicionais: fallback sem efeitos parciais via rollback da etapa 030.
7. Confirmar que C++20/Qt/Capstone compilam, que auditoria de IR e emissão reconhecem as operações existentes, e que regressões ISA e binárias mantêm equivalência.

## Riscos e situação

Os incrementos 002–033 continuam sem build/regressões conjuntas. Esse avanço é cobertura escrita e revisão estática de uma parcela da semântica x86/x64, não funcionalidade validada. O fallback opaco permanece obrigatório quando a forma não é reconhecida e a reescrita não é autorizada por isso.

## Próxima missão 034

Priorizar compilação/testes controlados de todo o núcleo quando houver autorização explícita. Se a restrição continuar, seguir ampliando semântica conservadora de instruções realmente recuperadas, sem simular atomicidade, exceções ou ABI.
