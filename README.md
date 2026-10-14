# REWorkbench Native

Bancada independente de engenharia reversa, com **C++20 como linguagem principal e interface Qt**. Ghidra e uma referencia de produto; o aplicativo nativo nao o utiliza como motor e nao precisa de JVM, PyGhidra ou Python.

Esta entrega contem **codigo-fonte sem testes, compilacao ou execucao**, conforme orientacao do usuario. Ainda nao foi produzido um executavel da aplicacao nativa. O objetivo de evoluir para uma ferramenta superior e de longo prazo; nenhuma superioridade ou paridade industrial foi validada nesta etapa.

## Codigo da aplicacao propria

- Carregadores proprios de PE32/PE32+, ELF32/64, Mach-O32/64 e firmware bruto, com escolha explicita de imagem universal e configuracao de arquitetura/base/entrada para raw.
- Modelo proprio de regioes, entradas, simbolos e imports, com enderecos virtuais separados de offsets do arquivo.
- Analise propria de funcoes candidatas, chamadas, referencias e blocos de controle a partir das entradas e simbolos suportados.
- Decodificacao de instrucoes com biblioteca especializada Capstone5, quando disponivel na compilacao. Capstone nao fornece o projeto, a interface, os carregadores, a descoberta de funcoes, o CFG ou a IR desta bancada.
- Decompilador proprio inicial para instrucoes escalares x86/x64: IR tipada por largura, SSA, juncoes phi, propagacao conservadora de constantes, flags e formas limitadas de condicoes/lacos. A saida explicita estado de maquina e efeitos opacos; **nao recupera assinaturas, tipos de origem ou um programa C recompilavel**. A IR anterior permanece em aba separada.
- Biblioteca propria C++ de strings ASCII e entropia, usada diretamente pelo nucleo sem processos auxiliares.
- Interface C++/Qt com exploradores paginados, assembly, IR, grafo de controle com zoom, visor hexadecimal, referencias, busca e historico de navegacao.
- Projetos nativos com amostras por SHA-256, bloqueio de projeto, SQLite, renomeacoes, notas, marcadores e exportacao JSON/IR.
- Trabalho em segundo plano, cancelamento, limites de exploracao e descarte de respostas antigas de selecao.
- Enriquecimento proprio de PE/ELF: funcoes de excecao AMD64, callbacks TLS, identidade CodeView/PDB, relocações, imports e linhas DWARF 2–4; mapas de simbolos locais com hash e nova analise que herda anotacoes compativeis.
- Comparacao de bytes e de funcoes, exportacao atomica de copia com patch, interpretador escalar com entradas explicitas e depurador Windows com launch/attach, eventos, passo, breakpoints de uso unico e leitura de registradores/memoria.
- CLI C++ para automacao, cache persistente de decompilacao, reaproveitamento de analises compativeis e SDK de extensoes Qt com descoberta por metadados e invocacao explicita por hash.

Os detalhes e limites de formatos estao em [loaders.md](native/workbench/docs/loaders.md), os do motor em [analysis.md](native/workbench/docs/analysis.md), e os de persistencia em [projects.md](native/workbench/docs/projects.md).

As adicoes da versao 0.3 estao em [decompiler.md](native/workbench/docs/decompiler.md), [enrichment.md](native/workbench/docs/enrichment.md), [investigation.md](native/workbench/docs/investigation.md) e [automation.md](native/workbench/docs/automation.md). Todos esses recursos continuam sem compilacao ou execucao nesta entrega.

## Organizacao

```text
CMakeLists.txt                    build nativo principal
native/workbench/include/         contratos C++ da aplicacao e motor
native/workbench/src/             carregadores, decoder, analise, IR e SQLite
native/workbench/ui/              janela Qt, exploradores, hex, importacao e CFG
native/workbench/src/cli_main.cpp automacao nativa por comandos/JSON
native/extensions/example/       exemplo opcional do SDK de extensoes
native/bytecore_cpp/              biblioteca propria C++ de triagem de bytes
scripts/run.ps1                  inicia somente o executavel nativo existente
src/reworkbench/__main__.py       lancador Python opcional para o mesmo executavel
src/reworkbench/legacy_main.py    entrada explicita do prototipo historico
```

Os arquivos ficam no worktree `.worktrees/implementation`, branch `feat/reworkbench`. O prototipo Python/Ghidra e seus testes continuam preservados como historico; eles nao fazem parte do caminho padrao do produto. Suas dependencias Python estao isoladas no extra `legacy` e nao sao requisitos da aplicacao C++.

## Preparacao e compilacao futura

**Nenhum dos comandos abaixo foi executado nesta etapa.** Requisitos previstos: compilador e biblioteca padrao C++20, CMake3.20+, Qt6.5+ com Core, Widgets, Concurrent, Sql e o plugin QSQLITE. Capstone5.0.6 e a referencia do decoder opcional. Nao existe download automatico de dependencias no CMake.

Quando a compilacao for autorizada, a partir da raiz deste worktree:

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/caminho/Qt/versao/msvc_64"
cmake --build build --config Release
```

Se Capstone estiver instalado fora dos caminhos padrao, informe os caminhos de headers e biblioteca na configuracao futura:

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/caminho/Qt/versao/msvc_64" -DREWORKBENCH_CAPSTONE_INCLUDE_DIR="C:/deps/include" -DREWORKBENCH_CAPSTONE_LIBRARY="C:/deps/lib/capstone.lib"
```

Sem Capstone, o codigo contempla modo parcial de formatos, bytes e strings, sem fingir que assembly ou IR estao disponiveis. Uma versao de API diferente de Capstone5 deve ser adaptada explicitamente. Empacotar o executavel exige tambem disponibilizar os runtimes/plugins Qt e o decoder usado; isso nao foi realizado.

O destino previsto do executavel e `build/bin/reworkbench.exe`. Quando ele existir:

```powershell
.\scripts\run.ps1
.\scripts\run.ps1 -Project "C:/investigacoes/projeto" -Sample "C:/amostras/programa.exe"
```

O lancador nao compila e nao retorna ao motor legado. `scripts/setup.ps1` apenas explica os requisitos nativos; `scripts/doctor.ps1` lista caminhos sem iniciar compilador, aplicativo ou Ghidra.

## Fluxo nativo

1. Criar um projeto em pasta vazia ou abrir uma pasta com `project.rwb.json`.
2. Importar o binario. Para imagens universais, escolher o slice; para firmware bruto, definir arquitetura, ordem dos bytes, base e entrada.
3. Explorar funcoes, assembly, referencias, bytes, pseudocodigo, SSA, IR e CFG. Campos desconhecidos e semantica nao modelada permanecem explicitos.
4. Renomear funcoes e registrar notas/marcadores. Salvar notas pelo botao ou Ctrl+S; trocas de projeto/analise, exportacao de relatorio e fechamento tambem persistem rascunhos antes de continuar.
5. Exportar JSON da investigacao ou IR da funcao, mantendo hash, configuracao, enderecos, estado parcial e avisos.

Projetos nativos e projetos do prototipo usam manifestos diferentes. O programa recusa conversao silenciosa de projetos antigos. Mover uma pasta de projeto nativo rebaseia o caminho da copia da amostra; a proveniencia original permanece registrada separadamente.

## Limites e evolucao

A qualidade industrial de um decompilador envolve recuperacao de funcoes, tipos, variaveis, convencoes de chamada, SSA, otimizacoes e estruturacao de controle. A IR inicial e uma base para esse trabalho e mantem as operacoes desconhecidas como opacas. Nao recupera o codigo original nem promete gerar codigo recompilavel.

O pipeline SSA ja esta escrito para um subconjunto escalar x86/x64. Permanecem lacunas importantes: recuperacao automatica de ABI/tipos/estruturas/variaveis de alto nivel, semantica ampla de ISA e SIMD/FPU, switches e lacos gerais, leitura completa de PDB/DWARF5, modos mistos por endereco e analise interprocedural. O inventario completo aparece nos documentos dos modulos; nao ha paridade industrial declarada.

Os limites iniciais sao 200.000 instrucoes globais, 4.096 por funcao, 10.000 funcoes, 300 segundos de analise, 10.000 strings e previsualizacoes de 4.096 caracteres; tabelas mostram 200 linhas por pagina e o hex le ate 64 KiB por intervalo. Limites e cancelamento produzem resultados parciais quando disponiveis. Desempenho real e comportamento de fechamento ainda precisam de validacao dinamica.

Ha restricoes especificas para imports, relocacoes, arquiteturas mistas e delay slots. Depuracao, emulacao escalar, patching em copia, comparacao e plugins possuem implementacoes iniciais com limites documentados; unpacking, colaboracao e isolamento de extensoes ainda precisam ser desenvolvidos. A analise estatica e o interpretador nao executam a amostra no sistema operacional; somente o depurador inicia ou anexa a processos por acao explicita. Extensoes nativas executam codigo confiado pelo usuario quando invocadas explicitamente.

Veja a [especificacao da ampliacao](docs/superpowers/specs/2026-10-14-advanced-native-analysis-design.md), o [plano 0.3](docs/superpowers/plans/2026-10-14-advanced-native-analysis.md), a [base nativa](docs/superpowers/specs/2026-10-14-independent-native-workbench-design.md) e o [estado de validacao](docs/validation.md).

Referencias das dependencias especializadas: [Qt Widgets](https://doc.qt.io/qt-6/qtwidgets-index.html), [Capstone5.0.6](https://github.com/capstone-engine/capstone/tree/5.0.6). O codigo proprio permanece separado dessas bibliotecas.
