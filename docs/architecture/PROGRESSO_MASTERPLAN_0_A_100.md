# REWORK — PROGRESSO DO PLANO MESTRE (0–100%)

**Checkpoint 035 — 24/10/2026. Progresso editorial estimado: 35/100 = 35%, PROVISÓRIO.** Antes: 34%.
Não é paridade demonstrada com Ghidra, build funcional ou taxa de testes aprovados. As etapas 002–035 seguem sem compilação e regressões conjuntas; o crédito poderá ser revisto.

## Rubrica

| Subsistema | Peso | Crédito provisório |
|---|---:|---:|
| Ingestão de binários, ferramentas e preparação de corpus | 8 | 5 |
| Semântica ISA, lifter e IR de máquina | 16 | 12 |
| CFG/SSA, stack, MemorySSA, alias, controle e arestas | 14 | 11,5 |
| ABI, chamadas, assinaturas e interprocedural | 12 | 2,5 |
| HIR, estruturação e emissão C | 17 | 3 |
| Tipos, objetos, structs e C++ | 11 | 1 |
| Outras ISAs e ABIs | 7 | 0 |
| Frontends JVM, CIL/.NET e JavaScript | 7 | 0 |
| Emissores multilíngues | 4 | 0 |
| Qualidade comprovada, testes, benchmarks e paridade Ghidra | 4 | 0 |
| **Total** | **100** | **35** |

O crédito da etapa 035 é apenas editorial pela modelagem estática de CBW, CWDE e CDQE com extensão do acumulador na IR existente. Não representa testes aprovados ou paridade medida.

## Evidências e limites

- Etapa 001: candidatos ABI Win64/SysV, último build/teste CLI conhecido com 25 testes aprovados, 2 pulados, ANTES das etapas seguintes.
- Etapas 002–014: observadores de ABI/memória/tipos, HIR e planejador estrutural, somente código escrito.
- Etapa 015: hardening de invariantes SSA/Phi/memória, só código escrito.
- Etapa 016: inputs Phi por aresta, código escrito.
- Etapa 017: plano simbólico de cópias paralelas e snapshots, código escrito.
- Etapa 018: contratos de fronteira e efeitos, código escrito.
- Etapa 019: normalização observacional de terminadores, critical edges e riscos noreturn/exception, código escrito.
- **Etapa 020:** inventário estático (59 caminhos CMake existentes nesse recorte, includes locais inspecionados, nomes de comandos no CLI), fixtures C, script de inventário NÃO executado e matriz de validação. NÃO compilada/testada.
- **Etapa 021:** segurança de terminadores, bloqueio de transformações e auditoria de nós ausentes. Código escrito, sem build/teste.
- **Etapa 022:** auditoria de tipos/larguras/operações e ID/dono de Phi. Código escrito sem build/teste.
- **Etapa 023:** contratos da sequência `Opaque/CallOpaque -> StateField(mem)`; escrito e não testado.
- **Etapa 024:** integridade das projeções `StateField` para todos os slots opacos; não testada.
- **Etapa 025:** dominância dos valores `Block.output` e entradas globais SSA; escrita e não validada.
- **Etapa 026:** invariantes de condições, retornos e dominância dos terminadores. Código escrito, sem build/teste.
- **Etapa 027:** coerência entre índices/endereço de bloco, arestas e destinos `taken/not_taken`, escrito e não testado.
- **Etapa 028:** alcançabilidade a partir da entrada, flags e mapa de blocos conferidos, código escrito e não testado.
- **Etapa 029:** partição das referências de instruções por blocos CFG, sem duplicatas e coerente com contagem do lifter, escrita e não testada.
- **Etapa 030:** rollback atômico do lifter em tradução malsucedida; código escrito e não testado.
- **Etapa 031:** extensão com sinal de imediatos conforme fonte lógica (substituída e refinada na 032), não testada.
- **Etapa 032:** largura codificada real de imediato no decoder/lifter x86/x64; código escrito, não testado.
- **Etapa 033:** modelagem de CLC/STC/CMC e XCHG entre registradores, escrita e não testada.
- **Etapa 034:** BSWAP 32/64 bits usando Extract e Insert, código escrito e não testado.
- **Etapa 035:** modelagem de CBW (AL→AX), CWDE (AX→EAX) e CDQE (EAX→RAX, somente x64), por SignExtend, preservação de flags e escrita de subregistradores. Código escrito, **sem compilação, testes, commit ou push**; relatório `IMPLEMENTACAO_035_X86_ACCUMULATOR_SIGN_EXTENSION_2026-10-24.md`.

## Falhas e lacunas ainda possíveis

Sem paridade Ghidra comprovada, sem compilação/integração dos incrementos 002–035, sem IR semanticamente confiável para todas as ISAs/chamadas/ABIs, sem tratamento completo de exceções e saltos indiretos, sem HIR C/C++ reescrita, classes/assinaturas recuperadas ou emissores multilíngues.
Um resultado positivo de inventário **não** valida símbolos, assinaturas, dependências transitivas, compilação C++20/Qt, link ou runtime.

## Próximo marco 036

Prioridade: obter autorização para **compilação e testes controlados** das etapas acumuladas e corrigir as falhas concretas. Se não houver autorização, apenas revisar código por inspeção estática e aperfeiçoar o corpus/contratos; não alegar testado ou compatível com Ghidra.

## Git e retomada

Branch local `main`, pasta principal `C:\Users\pedri\OneDrive\Documents\PROJETO\Rework Reverse Engeenering Software`. Último commit GitHub main `5795874`. Edições 015–035 integram o lote Git desta entrega. Confirmar o estado real do remoto com `git status --short --branch` e `git log -1`; continuam não compiladas/testadas.
Ler `REWORK_CHECKPOINT.md` e `docs/architecture/IMPLEMENTACAO_035_X86_ACCUMULATOR_SIGN_EXTENSION_2026-10-24.md`.
**Instrução do usuário: não compilar, rodar testes/benchmarks nem commitar automaticamente.**
