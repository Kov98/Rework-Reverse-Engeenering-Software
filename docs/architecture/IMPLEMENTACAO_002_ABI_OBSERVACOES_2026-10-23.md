# Rework — incremento 002: observação de ABI, dependências e chamadas

**Data:** 2026-10-23  
**Situação:** CÓDIGO ESCRITO; **NÃO COMPILADO, NÃO EXECUTADO, NÃO TESTADO** nesta rodada, por solicitação explícita do usuário.

## Resultado no código

1. A IR diferencia `CallOpaque` de outras instruções opacas. Chamadas continuam destruindo conservadoramente todo o estado rastreado até existir validação suficiente do contrato ABI. A saída anterior de pseudocódigo/SSA permanece no mesmo formato (renderização `opaque_instruction`).
2. O novo analisador `observe_function_abi()` reutiliza o lifter de x86-64 e examina as dependências de valores nos blocos alcançáveis. Ele procura registradores ABI na cadeia de dados que alcança condições, efeitos `Store` e possíveis registros de retorno.
3. Um registro só aparece como **dependência observada**, nunca como parâmetro original comprovado. Escritas detectadas em possíveis registradores de retorno não demonstram tipo, assinatura nem que existe valor retornado.
4. Os locais de `CallOpaque` mantêm o vetor de valores pré-chamada; a análise relata endereços de possíveis produtores para cada registrador de argumento ABI. Essa informação não demonstra que o valor seja um argumento da chamada.
5. Chamadas diretas são ligadas a funções já identificadas no mesmo `AnalysisSnapshot`, quando o endereço corresponde exatamente; destinos indiretos permanecem desconhecidos.
6. A análise usa candidatos automáticos de ABI somente para PE x86-64 e ELF x86-64. Suporta seleção manual por `--abi windows-x64` ou `--abi sysv-amd64`; a escolha não verifica a ABI da função.
7. Casos de IR parcialmente modelada, chamadas desconhecidas, limites de execução e formatos sem perfil recebem `partial`, `status` e avisos; não retornam assinatura fictícia.

## Arquivos tocados nesta rodada

- `native/workbench/include/reworkbench/function_abi.hpp` (novo)
- `native/workbench/src/function_abi.cpp` (novo)
- `native/workbench/include/reworkbench/abi.hpp` (ampliado)
- `native/workbench/src/abi.cpp` (ampliado)
- `native/workbench/src/decompiler_internal.hpp` (novo enum interno `CallOpaque`)
- `native/workbench/src/decompiler_lift.cpp` (marca chamadas como `CallOpaque`, mantendo efeitos opacos)
- `native/workbench/src/decompiler.cpp` (imprime `CallOpaque` como operação opaca, como antes)
- `native/workbench/src/cli_main.cpp` (novo comando `abi-observe` e opção `--abi`)
- `native/workbench/CMakeLists.txt` (adiciona `function_abi.cpp`)

Nenhuma migração, alteração de projeto existente, commit ou rebuild dos executáveis foi solicitado nesta etapa.

## Exemplos previstos (NÃO executados)

Binário PE x64:
```powershell
reworkbench-cli abi-observe --input exemplo.exe
```

Binário ELF x64:
```powershell
reworkbench-cli abi-observe --input exemplo.elf
```

Dados RAW x86-64 com seleção manual de ABI:
```powershell
reworkbench-cli abi-observe --input exemplo.bin --raw --arch x86-64 --base 0x1000 --entry 0x1000 --abi sysv-amd64
```

O resultado previsto contém `abi_hint` e `observation`, com:
`status`, `abi_candidate`, `verified_for_function: false`,
`partial`, `modeled_instructions`, `total_instructions`,
`observed_integer_input_dependencies`, `possible_integer_return_writes`,
`calls` e `warnings`.

Cada chamada inclui endereço, destino quando conhecido, indicador de alvo interno,
`effects_unknown: true` e endereços de possíveis produtores de valores dos
registradores de argumento. Não existem contagem nem tipos de parâmetros confirmados.

## Limitações conscientes

- Suporta apenas x86-64 nos formatos indicados e só observa o subconjunto de IR que o lifter já consegue modelar.
- Não analisa corretamente instruções desconhecidas nem recupera a assinatura da função.
- Não faz inferência de argumentos em registradores SIMD/FPU, nem parâmetros de pilha ou chamadas variádicas.
- O rastreamento SSA é conservador e pode reunir dependências de múltiplos caminhos; reporta relações possíveis, não prova de uso por execução.
- Em IR com instruções opacas, ausência de dependência observada **não** demonstra ausência de parâmetro.
- A análise é adicional ao comando `decompile`, para não modificar a saída atual nem refazer o lift em toda decompilação interativa.
- A implementação precisa de compilação, regressão e avaliação de desempenho antes de ser considerada pronta para uso.

## Próxima etapa de desenvolvimento

- Representar efeitos de chamadas em um contrato explícito que diferencie efeitos desconhecidos de efeitos comprovados por ABI e evidências de chamadas.
- Recuperar entradas de pilha e locais de retorno em corpus x86-64 controlado.
- Criar constraints de tipos e classes de armazenamento que alimentem futura IR de alto nível.
- Implementar sumarização interprocedural conservadora.
- Em uma etapa posterior de validação conjunta: build Release, testes de ABI e CLI, regressões de decompilação, casos negativos com ABI incompatível, benchmarking vs Ghidra.

Fontes:
- https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention
- https://gitlab.com/x86-psABIs/x86-64-ABI
