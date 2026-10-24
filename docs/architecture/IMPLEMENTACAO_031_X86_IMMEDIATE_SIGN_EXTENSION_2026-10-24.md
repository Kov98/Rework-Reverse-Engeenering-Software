# REwork — Etapa 031: extensão com sinal de imediatos x86/x64

**Data:** 24/10/2026
**Repositório:** `C:\Users\pedri\OneDrive\Documents\PROJETO\Rework Reverse Engeenering Software`, branch `main`.
**Estado:** fonte C++ alterada por revisão estática; **NÃO COMPILADA, NÃO TESTADA, NENHUM BENCHMARK, SEM COMMIT OU PUSH**.
**Progresso editorial:** 31/100 (31%, provisório); não é medida de funcionamento nem paridade Ghidra.

## Problema

O lifter, em `native/workbench/src/decompiler_lift.cpp`, convertia operandos `Immediate` diretamente para constantes da largura do destino, usando `constant(operand.immediate, width)`. Quando o decodificador entrega um imediato codificado com largura menor e os bits de sinal ativos, algumas instruções x86/x64 exigem **extensão com sinal**. Tratar esses bits como positivos (extensão com zeros) pode representar resultados aritméticos e valores empilhados incorretamente.

O decodificador Capstone `decoder.cpp` preenche `Operand.width_bits` com a largura exposta pelo operando x86 e guarda o imediato em `quint64`. A correção usa essa informação apenas onde ela estiver disponível. Não presume que toda constante de largura menor deve ser estendida: as regras variam por instrução.

## Implementação escrita

Em `native/workbench/src/decompiler_lift.cpp`:

1. `Builder::read(..., bool sign_extend_immediate=false)` adiciona uma escolha explícita de extensão. Para imediatos cuja `width_bits` seja positiva e menor que a largura de operação, faz máscara da largura codificada, confere o bit de sinal e estende os bits altos quando necessário. Os outros caminhos de leitura e origem registrador/memória ficam iguais.
2. Operações `add/sub/cmp/and/or/xor/test` solicitam essa extensão quando o segundo operando é imediato menor que a largura efetiva.
3. `push` de imediato passa a solicitar extensão com sinal para a largura da pilha.
4. `mov r/m64, imm32` solicita extensão quando o decodificador fornece explicitamente largura 32 para a origem e o destino tem largura 64. `movabs` e movimentações de outras larguras não são tratadas como sign-extended por padrão.
5. A alteração não modifica o decodificador, os blocos, SSA, a arquitetura de estados ou o fallback opaco da etapa 030.

## Contratos de segurança e limites

- Se o decodificador já entregou um valor estendido ou se a fonte não é estreita, a extensão adicional não é aplicada.
- Quando a biblioteca fornecer largura **lógica** em vez da largura exata codificada, esta etapa ainda pode não recuperar todos os casos. A prova exigirá inspeção de bytes/encoding e testes.
- Não são inferidas larguras a partir de nomes ou de um padrão geral para instruções desconhecidas.
- `mov` de imediato de largura igual à do destino mantém a interpretação antiga.
- Não há garantia de semântica correta para a ISA sem execução diferencial.

## Casos para validação futura — NÃO EXECUTADOS

- `push imm8` com padrão `0xff`: representar -1 na largura de pilha.
- `add/sub/cmp` com imediato de 8 bits `0x80` e destino 32/64 bits: representar -128.
- `and/or/xor/test` usando imediato negativo estreito e registrador maior.
- `mov r/m64, imm32` com `0xffffffff`, quando fonte aparece como largura 32: destino -1.
- `movabs r64, imm64`: manter todos os 64 bits como codificados.
- `mov r32, imm32`: manter extensão não solicitada.
- Imediatos positivos e registradores/memória, para detectar regressões.
- Testar especificamente o que a versão em uso do Capstone coloca em `source.size` para instruções `83 /r`, `C7 /0`, `6A` e `68` antes de certificar o resultado.

## Próxima etapa 032

A prioridade permanece compilar e executar regressões da base acumulada com autorização expressa. Sem isso, fazer nova revisão estática do lifter/decodificador e registrar incertezas sobre larguras codificadas sem alegar compatibilidade.
