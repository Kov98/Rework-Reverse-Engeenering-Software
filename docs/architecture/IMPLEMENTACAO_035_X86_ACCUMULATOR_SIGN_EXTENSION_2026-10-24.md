# REwork — etapa 035: extensões de sinal do acumulador CBW/CWDE/CDQE

**Data:** 24/10/2026
**Branch e pasta:** `main`, `C:\Users\pedri\OneDrive\Documents\PROJETO\Rework Reverse Engeenering Software`.
**Estado:** implementação escrita em C++ por análise estática; **NÃO COMPILADA, NÃO TESTADA, SEM BENCHMARK, COMMIT OU PUSH**.
**Progresso editorial:** 35/100 (35%, provisório); não é taxa de cobertura comprovada ou paridade Ghidra.

## Objetivo

O lifter já possui leitura de subregistradores, `Op::SignExtend` e escrita parcial/total de registradores. Porém ainda encaminhava as instruções `CBW`, `CWDE` e `CDQE` ao fallback opaco, apesar de sua semântica de extensão do sinal ser representável na IR existente.

## Implementação: `native/workbench/src/decompiler_lift.cpp`

1. Novo caso em `Builder::translate` para instruções normais **sem operandos explícitos**, de acordo com os mnemônicos entregues pelo decoder.
2. **CBW:** lê `AL` como valor de 8 bits e o estende com sinal para `AX` (16 bits). A escrita parcial conserva o restante do acumulador raiz.
3. **CWDE:** lê `AX` (16 bits) e estende com sinal para `EAX` (32 bits). Em x64, a escrita a EAX zera os 32 bits altos de RAX, como exige a regra arquitetural x86-64.
4. **CDQE:** lê `EAX` (32 bits) e estende com sinal para `RAX` (64 bits); a variante é aceita **somente** para `Architecture::X64`.
5. Resolve os registradores pela rotina `register_info`, exige larguras crescentes, usa `read_register`, `adapt(..., sign=true)` e `write_register`. A transformação não altera flags, memória, arquitetura, fluxo ou assinatura ABI.
6. Formas com operandos inesperados, CDQE fora de x64 e qualquer variante não reconhecida seguem no fallback opaco. Nenhuma transformação CFG/SSA ou novo opcode IR foi criada.

## Premissas e cautelas

- A identificação da variante `CBW/CWDE/CDQE` depende do mnemônico decodificado por Capstone. Não se inventa a variante com base em prefixos quando o decoder não identifica explicitamente a instrução.
- O fallback por instrução com rollback da etapa 030 continua vigente em caso de retorno `false`.
- Não foi feita inferência de `CWD/CDQ/CQO` (extensão de sinal para o par de registradores), que é uma família distinta.
- Sem compilação ou execução, não se pode declarar o código funcional nem semanticamente equivalente ao hardware.

## Testes futuros necessários — NENHUM EXECUTADO

- CBW: AL=0x80 deve produzir AX=0xff80, preservando bits altos do acumulador.
- CWDE: AX=0x8000 deve produzir EAX=0xffff8000; em x64, os bits 63–32 de RAX devem ser zerados.
- CDQE: EAX=0x80000000 deve produzir RAX=0xffffffff80000000.
- Valores positivos, limites 0 e -1, ausência de alterações em flags e memória.
- CDQE não pode ser modelada como instrução x86-32; formas com operandos inesperados ficam opacas.
- Regressões para subregistradores `AH/AL`, `MOVSX/MOVZX/MOVSXD` e operações do acumulador.
- Build C++20/Qt, ABI, IR audit, estruturação e comparação diferencial com a execução em máquina, **somente quando autorizados**.

## Situação do projeto

Etapas 002–035 ainda não têm validação de build/regressão conjunta. A porcentagem global de progresso do plano mestre é editorial e não comprova equivalência com o Ghidra.

## Próxima etapa 036

Priorizar compilação/testes de integração acumulados mediante autorização expressa. Se não autorizados, examinar estaticamente `CWD/CDQ/CQO` e outros contratos ISA conservadores, sem alegar código validado.
