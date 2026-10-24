# Rework — Implementação 020: auditoria estática e preparação do corpus

**Data:** 2026-10-24
**Branch de trabalho:** `main`, pasta principal `Rework Reverse Engeenering Software`.
**Status:** inventário estático read-only inspecionado; ferramentas e fixtures ESCRITOS no PC. **NÃO COMPILADO, NÃO EXECUTADO TESTES, NÃO BENCHMARKADO, NÃO COMMITADO/NEM ENVIADO AO GITHUB**.
**Progresso editorial global:** **20/100 (20%, provisório)**. Não é proporção de paridade com Ghidra.

## Objetivo

Antes de expandir passes HIR indefinidamente, consolidar os módulos escritos de 002–019, registrar dependências de CMake/CLI/includes, preparar fixtures portáteis C e critérios explícitos de validação a serem executados somente com autorização do usuário.

## Evidências estáticas coletadas (read-only)

Consulta de arquivos por PowerShell, **sem build, testes, execução de fixture ou benchmark**:
- `native/workbench/CMakeLists.txt`: 59 caminhos explícitos C++/QRC identificados no escopo `src/ui`, nenhum caminho referenciado ausente nessa consulta.
- Cinco fontes críticas verificadas quanto a `#include "local"`: nenhum include local ausente dentro das bases de busca consultadas.
- CLI: 10 nomes de comandos de observação rastreados em `cli_main.cpp`, cada nome apareceu em pelo menos três ocorrências textuais.
- Git: branch `main` no repositório principal, HEAD `5795874` antes das edições novas. Preservadas todas as alterações ainda não commitadas das etapas 015–019.
- Esses resultados **NÃO** certificam compilação, existência de bibliotecas Qt/Capstone, integridade do linker, parâmetros, APIs, semântica, cobertura real de branch ou resultado C.

## Arquivos escritos na etapa 020

1. `tests/fixtures/src/decompiler_stage020.c`:
   - fonte de corpus para diamond, backedge Phi, troca de valores em laço, candidato de critical edge, volatile Load/Store, extern CALL com efeitos, indiretos/noreturn desconhecido, bitwidth/signedness e condicionais aninhadas.
   - não garante formato de CFG independente de flags/compilador.
2. `tests/fixtures/src/decompiler_stage020_stubs.c`:
   - stubs externos separados e observáveis; `rw20_may_not_return` **retorna** na versão padrão, para impedir conclusões baseadas no nome.
   - ambos são somente texto-fonte e **não foram compilados**.
3. `tools/rework_stage020_inventory.py`:
   - script read-only de inventário futuro da árvore: caminho CMake, subset de `#include` e registro textual de comandos CLI.
   - **NÃO EXECUTADO** durante a etapa 020, nem sua execução substitui build/testes.
4. `docs/architecture/VALIDACAO_CORPUS_STAGE020_2026-10-24.md`:
   - matriz de casos, valores C esperados, variantes PE/ELF x86/x64, invariantes CLI/SSA/mem/CFG, alertas de falhas e critérios de aceite.
   - explicitamente distingue cenários de fonte C de comportamento de binário real após otimização.

## Revisão de risco e próximas ações

- Risco **alto**: incrementos 002–020 foram escritos em grande parte sem build/testes conjuntos; erraremos se confundirmos análise estática com validação.
- Risco **alto**: semântica x86/x64 parcial e `CallOpaque` permitem efeitos não observáveis; as regiões e os contratos de Phi não estão prontos para reescrita.
- Risco **alto**: os comandos expostos pelo CLI podem estar presentes textualmente e ainda falhar em compilação, runtime, bounds, formato JSON, exceções ou performance.
- Risco **médio**: os corpus-fontes podem ser otimizados ou inlined, mudando CFG; fixar ABI, variantes, flags, símbolos e amostras em futuras execuções.
- Risco **médio**: callbacks/calls externos precisam dos stubs; preservar diferenças entre may-return e truly-noreturn.
- Risco **médio**: o script de inventário usa heurísticas de texto e não é parser C++/CMake completo.

## Próximo incremento 021 recomendado

**Antes de mais passes:** obter autorização do usuário para uma rodada controlada de compilação e testes. Assim que permitido: registrar branch/HEAD/ambiente, compilar CLI, corrigir erros acumulados, executar IR audit e fixtures, documentar falhas com arquivo/linha, testar decompilação diferencial Rework×Ghidra. Se o usuário ainda não autorizar, limitar-se a revisões de segurança de fontes existentes e melhoria da matriz, SEM declarar projeto funcional.

## Git / continuidade

Último commit compartilhado no GitHub `main`: `5795874`.
Arquivos das etapas 015–020 ficam **no PC local**, ainda sem commit/push; não inferir autorização para realizar commit/push desta nova etapa.
Revisar `REWORK_CHECKPOINT.md`, `PROGRESSO_MASTERPLAN_0_A_100.md` e este relatório ao retomar.
