# REwork — etapa 032: largura codificada de imediatos x86/x64

**Data:** 24/10/2026
**Branch:** `main` na pasta `Rework Reverse Engeenering Software`.
**Estado:** alterações C++ escritas por inspeção; **SEM compilação, testes, execução do binário, benchmark, commit ou push**.
**Progresso global editorial:** 32/100 (32%, provisório); não indica paridade com Ghidra.

## Problema confirmado em fontes

Na etapa 031, a extensão com sinal para ADD/SUB/CMP/AND/OR/XOR/TEST, PUSH e MOV r/m64, imm32 dependia de `Operand.width_bits`, preenchido pelo decoder a partir de `Capstone x86_op.size`. Esse tamanho corresponde à largura lógica reportada ao operando, não necessariamente à quantidade de bytes do imediato codificada na instrução. Isso pode produzir extensão errada ou omitir a extensão exigida pela forma x86 (por exemplo, formas imm8 em operações de 32/64 bits).

## Código alterado

### `native/workbench/include/reworkbench/types.hpp`

Incluído `Operand.encoded_immediate_bits` com valor padrão 0 (desconhecido). O campo é independente de `width_bits` para não reclassificar a largura lógica usada por outros subsistemas. Nenhuma alteração de formato binário persistido é afirmada; validar consumidores em build futuro.

### `native/workbench/src/decoder.cpp`

No decoder x86/x64 via Capstone API 5, para operandos `X86_OP_IMM`, captura `detail.encoding.imm_size` e `detail.encoding.imm_offset`. Só armazena a largura em bits se o trecho codificado tiver tamanho de 1 a 8 bytes, deslocamento positivo e couber por inteiro no vetor de bytes da instrução. Se esse contrato não puder ser comprovado, `encoded_immediate_bits` permanece 0. Os dados originais `immediate` e `width_bits` são preservados, assim como outros decodificadores.

### `native/workbench/src/decompiler_lift.cpp`

- `read(..., sign_extend_immediate=true)` passa a utilizar `encoded_immediate_bits`, não `width_bits`.
- Largura codificada precisa ser uma largura escalar conhecida de 8/16/32/64 bits e ser menor ou igual à largura efetiva; caso contrário, retorna `nullopt`, acionando a tradução opaca por instrução com o rollback da etapa 030.
- Quando a largura codificada é menor, utiliza máscara de largura codificada, bit de sinal e extensão explícita.
- `MOV r/m64, imm32` passa a ser identificado pelo encoding 32-bit; `MOV r64, imm64` permanece full-width; tamanho codificado inválido/desconhecido no caso MOV de imediato para destino 64-bit não é tratado como semântica comprovada.
- PUSH e aritmética continuam solicitando extensão somente para imediato (operandos não imediatos não sofrem alteração).
- Nenhum CFG/IR existente é reescrito; a mudança afeta novas traduções do lifter.

## Limites e riscos ainda abertos

1. Capstone pode representar alguns operandos imediatos de múltiplos campos (por exemplo instruções com mais de um imediato) de forma especial. Este passe não afirma rastreabilidade por campo para tais instruções, que não receberam nova modelagem.
2. O código ainda não demonstra que todos os layouts x86 reportados no Capstone 5 cumprem o contrato para instruções específicas. Quando encoding não é verificável, a intenção é fallback conservador.
3. O decodificador é opcional e a compatibilidade de API, compilações C++20/Qt e semântica de bytes ainda NÃO foram testadas.
4. Correção de encoding não comprova semântica da ISA, aliasing, ABI, memória, exceções ou paridade Ghidra.
5. Incrementos 002–032 seguem sem build/regressões conjuntas.

## Casos de teste futuros — NÃO EXECUTADOS

- Encoding x86 `83 /r imm8` em operação 32/64-bit com bit 7 do imediato ativo; preservar extensão correta.
- `81 /r imm32` no modo x64, incluindo 0x80000000 e 0xffffffff.
- `6A imm8` e `68 imm32` (PUSH) em modo 64-bit.
- `C7 /0 imm32` (MOV r/m64) versus `REX.W + B8 imm64` (MOV r64, imm64).
- `mov r32, imm32` e imediatos positivos: impedir extensão indevida.
- Metadados de offset/tamanho ausentes ou fora dos bytes: retornar semântico opaco sem restos da tradução anterior.
- Aritmética sem imediato e outros decodificadores (ARM, MIPS): evitar regressões.
- Testes de instruções com múltiplos imediatos e observação direta dos metadados Capstone.

## Próxima etapa 033

Prioridade de engenharia: compilar e testar integração 002–032 **somente com autorização explícita** do usuário. Se a restrição persistir, aprofundar revisão estática das formas ISA e dos metadados de decoding, sem inflar garantia de funcionalidade.
