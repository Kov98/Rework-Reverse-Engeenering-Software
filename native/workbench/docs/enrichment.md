# Enriquecimento próprio de binários e mapas de símbolos

Implementação em `enrichment.cpp`, `debug_symbols.cpp` e `enrichment_internal.hpp`, com API pública em `include/reworkbench/enrichment.hpp`. Este código foi escrito e revisado por leitura do fonte; não foi compilado, executado ou submetido a testes nesta etapa, por orientação do usuário.

## API e integração

`enrich_binary(file, metadata, stop)` retorna `BinaryEnrichment`: símbolos, entradas, imports, referências, avisos e um objeto JSON de detalhes. As entradas só são produzidas para endereços alinhados, executáveis e com bytes no arquivo. As referências possuem origem e destino mapeados. Imports mantêm o endereço do slot separado de um destino externo desconhecido.

O chamador pode acrescentar essas informações aos metadados antes da descoberta de funções e persistir `details` junto da análise. Os fatos adicionados não modificam os bytes da amostra e não representam uma execução do carregador do sistema operacional. Os contadores e endereços de 64 bits são strings JSON para evitar perda de precisão.

`details.relocation_slots` inventaria o endereço inicial dos locais de relocação conhecidos, mesmo quando o alvo não pode ser recuperado. `details.relocation_slots_complete` é falso quando o inventário não fornece cobertura suficiente. O resolvedor de fluxo deve excluir leituras de ponteiros que se sobreponham a esses locais; com completude falsa, deve suspender a resolução de ponteiros de memória e tabelas a partir dos bytes brutos. Um slot que o carregador modifica não prova um destino estático.

A completude é conservadora: formatos Mach-O não recebem inventário de relocação nesta implementação. PE com tipos não modelados, ELF sem seções, ELF MIPS64, RELR/Android, tabelas truncadas, limites excedidos e intervalos dinâmicos sem seções correspondentes permanecem incompletos. Imagens raw não possuem um carregador de formato ou tabelas de relocação declaradas.

Erros em um componente de enriquecimento acrescentam avisos de leitura parcial e preservam os fatos já aceitos. Cancelamento propaga `Cancelled`; o chamador decide se conserva a análise anterior. A API de importação de mapas é atômica: um registro inválido rejeita a importação inteira.

## PE

- Diretório de exceções AMD64: lê registros `RUNTIME_FUNCTION` de 12 bytes; valida intervalo inicial/final, bytes de código, alinhamento, cabeçalho e armazenamento variável de `UNWIND_INFO`. Gera entradas, intervalos apresentados em detalhes e referências para handlers e encadeamentos validados. Fragmentos encadeados não comprovam funções independentes. Os opcodes de unwind e o layout da pilha não são reconstruídos.
- TLS PE32/PE32+: lê o endereço virtual preferido da tabela de callbacks, respeita a largura dos ponteiros e procura o terminador nulo dentro do limite. Adiciona callbacks com código mapeado e referências do slot para o callback. Em ARM/Thumb, o bit de modo do ponteiro deve corresponder ao modo global da imagem.
- CodeView RSDS: extrai GUID, idade, caminho gravado e chave GUID/idade. Não abre nem baixa PDBs; não recupera nomes, tipos ou linhas de PDB. Caminhos são tratados como metadados.
- Relocações de base: valida blocos, páginas, tamanhos e o segundo campo consumido por `HIGHADJ`. Inventaria os locais de relocação. Somente `HIGHLOW` em x86 e `DIR64` em x64 produzem referências para valores preferidos que possuem bytes mapeados. Nenhuma relocação é aplicada à amostra.

## ELF

Lê os formatos ELF32/ELF64, little endian ou big endian, e a numeração estendida das seções. Examina apenas executáveis e bibliotecas com endereços de link; objetos relocáveis requerem outro modelo.

Tabelas alocadas `SHT_REL`/`SHT_RELA` fornecem locais de relocação em VA. Tabelas de relocação estática e de depuração sem `SHF_ALLOC` não são tratadas como locais modificados pelo carregador. `GLOB_DAT` e `JUMP_SLOT` conhecidos de x86, x64, ARM e AArch64 associam símbolos indefinidos a seus slots. Eles permanecem imports sem endereço externo resolvido e sem biblioteca atribuída. `STT_TLS` não vira endereço comum; resolvers GNU IFUNC não viram destinos de função resolvidos.

Referências internas são derivadas de RELA absolutos reconhecidos e de relocações relativas reconhecidas. O valor usa o endereço de link, sem introduzir load bias em tempo de execução. Addends são interpretados com sinal e checagem de overflow. Outras relocações REL, stubs PLT, versões de símbolos, TLS e fórmulas não reconhecidas não são simuladas. Registros MIPS64 possuem encoding próprio e ficam fora desta versão.

O inventário compara os intervalos declarados em `DT_REL`, `DT_RELA` e `DT_JMPREL` com as seções efetivamente percorridas; também exige que `PT_DYNAMIC` possua uma seção correspondente. RELR, tabelas compactadas Android, duplicatas incompatíveis e cobertura ausente mantêm a completude falsa.

Build-ids GNU são extraídos de notas limitadas. `.gnu_debuglink` fornece nome gravado e CRC informado; nenhum arquivo externo é aberto e nenhum CRC externo é validado. Seções de depuração são inventariadas com offset, tamanho e indicação de compressão.

## Linhas DWARF

`decode_debug_symbols(file, metadata, stop)` lê programas `.debug_line` DWARF 2, 3 e 4 sem compressão, com comprimentos DWARF32 ou DWARF64. A máquina própria interpreta operações padrão, end_sequence, set_address, define_file e discriminadores DWARF 4. LEB128, intervalos, incrementos de endereço e linha são limitados e checados. ISAs diferentes do modo global, VLIW com múltiplas operações e opcodes não modelados interrompem a unidade com aviso.

O resultado `DebugSymbols.lines` contém apenas linhas com endereço exato de código mapeado, arquivo gravado, número de linha, coluna, discriminador e flag de statement. Não há interpolação de intervalos nem leitura de arquivos de código-fonte. `enrich_binary` serializa os caminhos uma única vez em `details.source_files` e usa índices em `details.source_lines`.

Não são interpretados DWARF 5, DIEs, `DW_AT_comp_dir`, informações de tipos, funções inline, split DWARF, arquivos separados ou seções comprimidas. Os caminhos podem ser relativos porque o diretório da unidade de compilação não é recuperado.

## Mapas offline

`import_symbol_map(path, metadata, SymbolMapFormat::Auto, stop)` detecta JSON ou texto pelo conteúdo, incluindo BOM UTF-8, independentemente da extensão. O resultado contém símbolos e entradas validados, avisos e proveniência. O chamador mantém a cópia e o hash do mapa no projeto; o parser não modifica binário ou banco de dados.

Formato JSON próprio:

```json
{
  "format": "reworkbench-symbols",
  "version": 1,
  "symbols": [
    { "address": "ram:0x140001000", "name": "process_input", "kind": "function" },
    { "address": "0x140004000", "name": "message_table", "kind": "symbol" }
  ]
}
```

O campo opcional `sha256` deve ser uma string hexadecimal de 64 caracteres que corresponda ao SHA-256 já conhecido da amostra. Sua ausência produz aviso. Endereços são strings hexadecimais com prefixo `0x`, no espaço `ram`. `function` exige bytes executáveis e alinhamento da arquitetura; `symbol` exige bytes mapeados. Um mapa ARM/Thumb fornece endereços físicos alinhados no modo já escolhido, sem mudar o modo pela presença de um bit baixo.

Campos desconhecidos, chaves duplicadas, chaves com escapes, nomes de chaves fora do ASCII literal, profundidade acima do esquema e tipos incorretos são rejeitados. Nomes de símbolos aceitam Unicode válido e decoração C++, com até 4095 bytes UTF-8, sem controles ou caracteres de formato invisíveis. Aliases no mesmo endereço são preservados e registros idênticos são deduplicados.

O formato de texto é deliberadamente pequeno; não interpreta automaticamente mapas de linkers de outros produtos:

```text
# endereço tipo nome
ram:0x140001000 function process_input
0x140004000 symbol message_table
```

Linhas em branco e comentários iniciados por `#` são aceitos. Os endereços do exemplo devem ser substituídos por endereços realmente mapeados na amostra. Texto não contém vínculo de identidade da amostra por hash; nomes fornecidos pelo analista não comprovam fronteiras de função ou tipos.

## Limites

| Recurso | Limite |
| --- | --- |
| Bloco de leitura, tabela materializada ou mapa completo | 16 MiB |
| Seções ou segmentos ELF | 65.536 |
| Entradas adicionais, referências, relocações ou locais de relocação | 200.000 por coleção |
| Imports adicionais | 65.536 |
| Identidades RSDS ou build-ids apresentados | 256 |
| Intervalos PE apresentados | 20.000; entradas continuam com limite próprio |
| Callbacks TLS | 1.024 |
| Strings terminadas em NUL | 4096 bytes incluindo terminador |
| Unidades de linhas DWARF | 4096 |
| Operações de linhas DWARF | 1.000.000 |
| Linhas DWARF apresentadas | 20.000 |
| Texto de nomes/caminhos DWARF | 1 MiB agregado |
| Símbolos de mapa | 100.000 |
| Linhas de mapa de texto | 200.000 |
| Profundidade JSON de mapa | 3 |

Todos os percursos de tabelas consultam o stop token. Os limites são proteções de recurso, não evidência de validação experimental.

## Referências primárias

- [Microsoft PE/COFF](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format): diretórios, runtime functions, TLS, debug e relocações.
- [LLVM CodeView debug records](https://github.com/llvm/llvm-project/blob/main/llvm/include/llvm/Object/CVDebugRecord.h): layout da identidade RSDS.
- [ELF generic ABI: relocation](https://refspecs.linuxfoundation.org/elf/gabi4+/ch4.reloc.html): registros REL e RELA.
- [glibc ELF definitions](https://github.com/bminor/glibc/blob/master/elf/elf.h): tipos de seções, registros dinâmicos e valores de relocação.
- [DWARF 4](https://dwarfstd.org/doc/DWARF4.pdf): máquina de linhas implementada.
- [GDB: separate debug files](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Separate-Debug-Files.html): build-id e `.gnu_debuglink`.
