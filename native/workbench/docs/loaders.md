# Carregadores nativos de imagens

O código em `src/binary_image.cpp` e `src/loaders.cpp` é uma implementação própria em C++20. Não usa APIs do Ghidra e não inicia a amostra. Esta entrega foi escrita e revisada como fonte; não houve compilação, configuração CMake, testes ou execução por instrução do usuário. Compatibilidade prática e robustez precisam de validação posterior autorizada.

## Contrato de arquivo e endereços

`BinaryFile` mantém um `std::ifstream` aberto. No Windows, abre um `std::filesystem::path` criado do caminho Unicode de `QString`. Um mutex protege cada posicionamento/leitura. Tamanho e offsets são de 64 bits; uma leitura é limitada a 16 MiB, precisa caber integralmente no tamanho capturado ao abrir e precisa retornar exatamente a quantidade solicitada. Leitura vazia em EOF é permitida. Truncamento durante a leitura gera erro; o carregador não promete detectar alterações externas que preservem o tamanho.

Regiões usam intervalos virtuais e tamanhos em arquivo separados. O offset ausente representa memória sem bytes no arquivo. Caudas de BSS/zero fill nunca recebem bytes fictícios. Regiões vazias são omitidas. `file_offset(metadata, address, length)` exige que o intervalo inteiro pertença a uma única região, caiba tanto em `virtual_size` quanto em `file_size` e permaneça dentro do arquivo original. Intervalos que cruzem regiões são recusados, mesmo se as regiões forem adjacentes. `executable_address` consulta a permissão virtual; antes de ler código, o consumidor também precisa consultar `file_offset` para excluir caudas não inicializadas.

Em Mach-O universal, `BinaryMetadata.size` continua sendo o tamanho do arquivo completo. A origem da fatia é adicionada uma única vez ao gravar cada `Region.file_offset`; os consumidores já recebem offsets absolutos do arquivo original. As leituras privadas do parser continuam relativas à fatia e não podem acessar fatias vizinhas.

Regiões ficam ordenadas por espaço/endereço. Regiões virtuais sobrepostas geram um erro explícito nesta versão, incluindo layouts válidos mas ambíguos para este modelo simples. Isso evita escolher bytes conforme a ordem dos cabeçalhos. Endereços e tamanhos usam operações com checagem de overflow; regiões de imagens com ponteiros de 32 bits precisam caber em 32 bits.

O campo SHA-256 fica a cargo do armazenamento do projeto, que conhece a cópia importada e sua origem; os parsers não calculam hashes ou executam análises de conteúdo.

## Formatos representados no código

| Formato | Dados interpretados | Limitações explícitas |
| --- | --- | --- |
| PE32 / PE32+ | DOS/NT/COFF, cabeçalho opcional e diretórios, seções e permissões, base preferida, entrada, exports nomeados/por ordinal e aliases, imports normais por nome/ordinal e slots IAT | Não aplica relocação, delay imports, símbolos COFF/PDB, TLS callbacks ou resolução de biblioteca. IAT vinculada sem tabela original pode ser recusada quando não contém RVAs recuperáveis. |
| ELF32 / ELF64 | Ambos os endians, cabeçalho, numeração estendida de tabelas, segmentos PT_LOAD, permissões, entrada, SHT_SYMTAB/SHT_DYNSYM vinculadas a SHT_STRTAB | ET_EXEC/ET_DYN; ET_REL e core dumps são recusados. Não aplica relocação, GOT/PLT, mapeamento de bibliotecas, símbolos dinâmicos recuperados apenas por PT_DYNAMIC ou SHT_SYMTAB_SHNDX. Tabelas de símbolos/nomes comprimidas são recusadas. Símbolos SHN_XINDEX são omitidos com aviso. Símbolos TLS não são apresentados como endereços comuns. |
| Mach-O fino 32 / 64 | Ambos os endians, comandos de carga, segmentos/seções e permissões, LC_MAIN, LC_SYMTAB/nlist, símbolos indefinidos e ordinais de dylib em namespace de dois níveis | MH_OBJECT, core dumps e SG_HIGHVM são recusados. LC_THREAD/LC_UNIXTHREAD não fornecem entradas nesta versão; isso gera aviso. Não aplica slide/relocação, bind opcodes, export trie, chained fixups, recuperação de slots via símbolos indiretos ou descriptografia. |
| Mach-O universal FAT32 / FAT64 | Cabeçalho, limites e alinhamento das fatias, checagem do cabeçalho Mach-O interno, CPU e subtipo básico, lista de seleção | Seleção de índice é obrigatória mesmo com uma fatia. Fatias sobrepostas, aninhadas, arquivo archive no lugar de Mach-O ou fatia fora do arquivo geram erro. |
| Bruto | Arquitetura/endian/base explícitos, região que cobre os bytes, entradas manuais | Permissões R/W/X são hipóteses de análise. Se nenhuma entrada for indicada, usa a base como semente; arquivo vazio não recebe semente. O contrato atual infere 32 bits para MIPS bruto, pois ainda não oferece uma opção MIPS64 separada. |

Exports PE que encaminham para outro módulo viram metadados de import não resolvido; o endereço da string de encaminhamento não é apresentado como uma função. Um export PE em região executável recebe `kind = "function"`; um export de dados permanece `"export"`. ELF STT_FUNC e símbolos Mach-O em região executável também usam `"function"`, para alimentar as sementes de análise, quando seu modo é compatível com o decoder selecionado. ELF identifica Thumb pelo bit zero de STT_FUNC, e Mach-O por N_ARM_THUMB_DEF em n_desc. Símbolos de modo diferente permanecem visíveis como `function-thumb-unselected` ou `function-arm-unselected`, com aviso, e não podem iniciar ou continuar a exploração no modo errado.

As arquiteturas representadas pelo contrato são x86, x64, ARM, Thumb, ARM64 e MIPS. CPUs fora desse conjunto preservam formato, bits, endian, regiões e bytes com arquitetura desconhecida e aviso. Isto não equivale a oferecer um decoder para toda máquina ou extensão de ISA identificada. ELF ARM com entrada Thumb seleciona Thumb globalmente; alternância de modo por região/função permanece uma limitação observável. Metadados não são uma simulação do carregador do sistema operacional.

`OpenOptions.entries` acrescenta sementes manuais a formatos reconhecidos e a imagens brutas. Cada semente manual precisa estar em região executável com bytes no arquivo; sementes repetidas são eliminadas. A entrada declarada pelo formato é preservada mesmo quando não tem bytes executáveis, acompanhada de aviso. `probe_binary` devolve `raw_required = true` para conteúdo desconhecido; assinatura reconhecida com estruturas malformadas produz erro durante o carregamento, sem converter silenciosamente para bruto.

## Limites de recursos

- 16 MiB por leitura e por tabela estrutural lida como bloco; tabelas de cabeçalhos e de símbolos ELF também têm limite de 16 MiB de tamanho declarado. Tabelas de strings podem ser maiores, mas seus nomes são consultados separadamente.
- 4096 bytes examinados por nome, incluindo o terminador NUL; nomes sem terminador nesse intervalo geram erro.
- 16 MiB de texto de nomes contabilizado por parser de metadados, com strings compartilhadas de biblioteca em imports.
- 4096 regiões mapeadas e entradas manuais; até 65536 cabeçalhos ELF/comandos Mach-O/seções Mach-O, dentro dos outros limites.
- Até 200000 entradas de símbolos/exports por formato; aliases PE contam individualmente. ELF contabiliza conjuntamente as entradas lidas de suas tabelas de símbolos.
- Até 65536 imports; PE também limita descritores e busca do terminador de thunks.
- Até 256 fatias Mach-O.

Limites excedidos geram erros descritivos, não resultados silenciosamente truncados. `load_binary` consulta `std::stop_token` nas iterações de cabeçalhos, símbolos, imports, entradas e nomes; `probe_binary` tem interface sem token e trabalho limitado às primeiras estruturas e, no máximo, 256 fatias. Uma operação individual de I/O não é interrompida no meio pelo token.

## Referências primárias de formato

Os campos e constantes foram conferidos em documentação primária, sem incorporar código de parser de outro produto:

- [Microsoft PE/COFF](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format).
- [System V ELF: cabeçalhos](https://refspecs.linuxfoundation.org/elf/gabi4+/ch4.eheader.html), [segmentos](https://refspecs.linuxfoundation.org/elf/gabi4+/ch5.pheader.html) e [símbolos](https://refspecs.linuxfoundation.org/elf/gabi4+/ch4.symtab.html).
- [Apple Mach-O loader.h](https://github.com/apple-oss-distributions/xnu/blob/main/EXTERNAL_HEADERS/mach-o/loader.h) e [fat.h](https://github.com/apple-oss-distributions/xnu/blob/main/EXTERNAL_HEADERS/mach-o/fat.h).
- [Arm ELF ABI: valores e modos dos símbolos](https://github.com/ARM-software/abi-aa/blob/main/aaelf32/aaelf32.rst) e [Apple nlist.h: N_ARM_THUMB_DEF](https://github.com/apple-oss-distributions/xnu/blob/main/EXTERNAL_HEADERS/mach-o/nlist.h).

## Revisão disponível nesta entrega

Revisão estática de intervalos, overflow, contagens, terminadores, origem de fatias, distinção entre bytes e memória virtual, nomes e permissões, além da correspondência com os cabeçalhos públicos. Não constitui evidência de compilação, funcionamento, ausência de vulnerabilidades ou compatibilidade com um corpus de arquivos reais. Nenhum comando de teste ou execução de amostra faz parte desta entrega.
