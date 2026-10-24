# Estado de implementação e validação

## Navegacao e produtividade da IDE — 19/10/2026

A bancada C++/Qt agora sincroniza selecoes entre Assembly, grafo e linhas de pseudocodigo, SSA e IR com origem conhecida. Os renderizadores emitem mapas de linhas baseados nos enderecos originais; nao ha extracao de enderecos por busca no texto. Declaracoes, entradas, phis, transferencias sinteticas e controles sem origem unica permanecem sem associacao presumida. A saida textual e a versao de semantica do decompilador foram preservadas. Limites de saida removem associacoes de linhas cortadas e marcadores de truncamento. O cache derivado usa chave/formato v2 com validacao de linhas e pertinencia a instrucoes da funcao; entradas antigas sao recalculadas, sem migracao do projeto.

O historico agora conserva a instrucao selecionada antes de seguir um destino e registra trocas manuais de analise. Fechar uma aba inativa preserva a posicao ativa. Movimentos dentro da mesma funcao reutilizam seus documentos e trabalhos de fundo, condicionados a identidade do snapshot; renomear uma funcao reconstroi o texto e invalida contexto anterior. A restauracao programatica da selecao do Assembly, inclusive depois de carregar outra pagina de 200 linhas, nao ecoa como nova selecao do usuario. Assim, a busca no editor conserva seu trecho selecionado enquanto os outros visores acompanham o endereco.

Pseudocodigo, SSA e IR possuem busca local por Ctrl+F, F3/Shift+F3, Enter e Esc, com retorno circular, diferenciacao de maiusculas, contagem e estado sem resultados. O realce e limitado a 1.000 ocorrencias, com indicacao `1000+` acima do limite; F3 continua percorrendo todos os resultados. A busca permanece isolada entre editores e e atualizada ao substituir o documento, inclusive em abas ocultas. Setas e selecao pelo teclado funcionam sem permitir alteracoes no codigo. O teste com setas detectou que o modo de leitura do Qt habilitava apenas selecao por mouse; a configuracao agora inclui selecao por teclado, mantendo `TextEditable` ausente. Digitacao, Delete e Backspace foram verificados sem mutacao de texto.

Validacao final do mesmo codigo compilado:

| Execucao | Resultado |
| --- | --- |
| Build Release completo, aplicativo/CLI/testes | Concluido, codigo zero |
| `pytest tests/native -v` | **26 aprovados, zero falhas e zero ignorados; 34,71s**. Inclui a suite completa Qt no plugin offscreen. |
| Suite Qt completa, Windows 100% | **39 aprovados, zero falhas e zero ignorados; 26,191s** |
| Suite Qt completa, Windows 125% | **39 aprovados, zero falhas e zero ignorados; 24,648s** |

Os totais Qt incluem 37 cenarios mais inicializacao/encerramento. Os novos casos cobrem navegacao por endereco exato, fechamento em segundo plano, historico entre analises, reutilizacao da funcao, renomeacao, busca apos paginacao, atalhos locais, limites de realce, somente leitura, substituicao de documentos, metadados dos renderizadores, truncamento e cache invalido. Os testes de fontes, icones, paineis e preferencias anteriores tambem permanecem aprovados. A regressao inicial reproduziu cinco falhas reais em `build/navigation-red.txt`; a primeira suite ampliada registrou 38 aprovados e uma falha de teclado em `build/navigation-qt-first.txt`, corrigida antes das execucoes finais. Relatorios finais: `build/navigation-native-final.log`, `build/navigation-native-final.xml`, `build/navigation-windows-100.txt`, `build/navigation-windows-125.txt`; build em `build/navigation-final-build.log`.

As capturas `build/navigation-captures-100/navigation-find.png` e `build/navigation-captures-125/navigation-find.png` sao gravacoes diretas dos widgets Qt reais, com uma fixture propria cuja primeira funcao tem 204 instrucoes, sem montagem. Assembly, resultado da busca e referencias concordam em `0x10ca`, apos carregar outra pagina. A captura `build/gui-navigation-final.png` foi obtida da janela do executavel final em 1366x738 com `build/demo-native`, Assembly/grafo e hexadecimal. A revisao visual independente nao encontrou defeitos materiais de legibilidade ou recorte; em 125%, colunas e abas usam rolagem conforme a largura disponivel. O aplicativo foi deixado aberto, em 100%, com titulo Rework e responsivo; stderr vazio na verificacao final.

Esta entrega permanece alpha 0.3.0. O mapeamento e por linha com origem conhecida, sem recuperar tipos/variaveis de origem ou associar todos os tokens. A IA continua sem modelo conectado. A suite legada completa de 77 casos nao foi repetida; nao houve verificacao fisica em varios monitores nem em todas as escalas. A compilacao ainda apresenta avisos preexistentes, e o plugin offscreen emite avisos de efeitos de janela nao suportados. Nao se declara ausencia de avisos, paridade industrial ou superioridade sobre outras ferramentas.

## Rework Dark e identidade oficial — revisao concluida em 17/10/2026

O produto visivel agora e **Rework**, com tema **Rework Dark** em grafite e roxo. O logotipo original foi copiado byte a byte, preservando transparencia e proporcoes (SHA-256 `f1e4c58ee87001516c25ec3f0f42aee5bc8243202febbc8736c7d8e8ae561233`). A amostra roxa mais frequente foi #6018F8; o foco usa uma variante clareada para contraste. O ICO possui nove resolucoes entre 16 e 256px. Recursos Qt e Windows incluem a marca; a barra superior e Sobre usam o mesmo original. A descricao e o nome de produto do executavel foram conferidos como Rework, versao 0.3.0. A extracao nativa do icone do exe e WM_GETICON da janela confirmaram icones oficiais; a janela fornece 16px e 32px. Metadados: `build/rework-executable-metadata.json` e `build/rework-window-icons.json`. IDs internos, projetos e preferencias antigas foram preservados.

A investigacao encontrou causas alem da familia da fonte: overrides QSS de 11/12px fragmentavam a interface; instalar o primeiro stylesheet no Qt reinicializava fontes por classe do Windows depois do setFont anterior; Codicons eram alinhados pelas metricas de linha em vez da tinta do glifo; Seti ocupava o retangulo inteiro sem margem. A barra de menus a 125% foi reproduzida em Segoe UI 9pt/12px no teste isolado `build/rework-font-order-red.txt`. Aplicar a fonte global depois do stylesheet corrigiu a primeira janela. A execucao final confirmou menus, abas e arvore em Segoe UI 13px, codigo em Cascadia Code 14px monoespacada, DPI logico 96 e DPR 1/1,25. Fontes Microsoft continuam lidas da instalacao do Windows, sem redistribuicao. Detalhes e fontes primarias: [theme.md](../native/workbench/docs/theme.md).

Raios, espacamento e tamanhos tipograficos foram centralizados junto das cores. Menus e paleta possuem superficies alfa reais, com cantos transparentes verificados antes/depois de filtrar e redimensionar. Hover usa cinza; selecao e foco permanecem distintos, com roxo controlado. O explorador mantem disclosure, modelo e navegacao reais. A captura inicial revelou selecao apenas no gutter: QTreeView acrescenta o estado Selected depois de entrar em drawRow. O fundo arredondado agora e pintado no delegate, com o estado final, antes do texto/icones. O teste existente amostra o pixel de selecao a direita e a margem esquerda no DPR real. O grafo tambem verifica que titulos e instrucoes nao se sobrepoem com fonte personalizada de 24pt.

Codicons usam QGlyphRun centrado pela caixa de tinta; Seti preserva o contorno com margem proporcional. Export, referencias, instrucoes, grafo e breakpoint possuem glifos distintos. Comandos reais recebem icones associados a suas acoes; comandos sem correspondencia mantem a coluna alinhada sem um simbolo generico enganoso. O assistente recebeu compositor arredondado, foco no contorno, acoes discretas e textos legiveis. Continua preparando/copiando solicitacoes locais, sem modelo conectado. A validacao adicional abre a paleta pelo QAction real, filtra "Restaurar layout", confirma um unico disparo da acao, retorno do painel de IA e fonte preservada. Nao ha comandos ficticios, marketplace ou alteracao do motor nesta entrega.

Validacao final do ultimo build: **26 testes nativos aprovados, zero falhas e zero ignorados, em 20,39 segundos**. Dentro desse conjunto, a suite Qt registrou **24 aprovados** (22 cenarios mais inicializacao/encerramento), sem falhas ou ignorados, em 10,135 segundos. Na plataforma Windows, uma execucao em 100% registrou **4 aprovados** em 1,994 segundos; em 125%, **12 aprovados** em 5,047 segundos, ambos sem falhas ou ignorados. As duas execucoes Windows incluem as capturas opcionais e o dispatch da paleta. O teste de abertura nativa foi atualizado para esperar o novo titulo Rework e encerra normalmente por WM_CLOSE. A suite legada completa de 77 casos nao foi repetida. O plugin offscreen emite avisos de efeitos de janela nao suportados; nao se declara execucao sem avisos.

A janela do executavel final foi inspecionada em **1366x738**, em 100% e 125%, com `build/demo-native`, Assembly/pseudocodigo, referencias, assistente e grafo. Capturas: `build/gui-rework-dark.png`, `build/gui-rework-125.png` e `build/gui-rework-graph.png`. Menus e buscas foram capturados diretamente dos widgets Qt nativos, em execucoes de teste com projeto proprio: `build/gui-rework-menu.png`, `build/gui-rework-palette.png` e `build/gui-rework-quick-open.png`; sao pop-ups isolados, sem montagem sobre outra imagem. Originais e relatorios ficam em `build/rework-captures-100/`, `build/rework-captures-125/`, `build/rework-captures-100.txt` e `build/rework-windows-125.txt`. A revisao independente das seis imagens nao encontrou defeitos materiais de recorte vertical, contraste, icones ou tipografia nas duas escalas. Em 125%, abas finais usam rolagem e o placeholder do explorador e elidido conforme a largura disponivel. A janela foi restaurada a 100%, aberta e responsiva no pseudocodigo.

Builds finais: `build/rework-command-icons-build.log`, precedido por `build/rework-final-build.log` e `build/rework-capture-tests-build.log`. Testes: `build/native-rework-dark-final.log`, `build/native-rework-dark-final.xml`, `build/rework-dark-qt.txt` e `build/rework-windows-125.txt`. Executavel: `build/bin/reworkbench.exe`. Capturas estaticas nao demonstram hover, teclado ou docking por si; as interacoes descritas foram cobertas pelos testes de widgets e pelos controles reais. Nao houve verificacao fisica em varios monitores nem em todas as escalas. Molduras de dialogos nativos continuam sob controle do Windows. A entrega permanece alpha 0.3.0, com os limites atuais do decompilador e do assistente.

## Fidelidade visual da IDE e verificacao em 125% — 16/10/2026

Esta entrega continua o aplicativo C++/Qt existente e preserva o motor de analise. Fontes, cores e proporcoes foram conferidas nas referencias oficiais registradas em [theme.md](../native/workbench/docs/theme.md). A interface usa Segoe UI de 13px logicos; o codigo usa Cascadia Code/Consolas de 14px, com linhas de 19px no padrao. A verificacao Windows em escala 125% confirmou as familias efetivas Segoe UI e Cascadia Code, respectivamente 13px e 14px logicos, fonte de codigo monoespacada e DPR 1,25. Preferencias completas de fonte continuam preservadas, incluindo atributos personalizados anteriores.

A Command Palette e o Quick Open agora usam largura proporcional limitada a 600px, altura conforme os resultados, linhas compactas, icones, descricoes e atalhos separados, com selecao cinza e navegacao que ignora resultados desabilitados. O explorador possui indicadores de expansao e hierarquia compacta. Codicons identificam acoes e categorias de simbolos; a fonte Seti UI original identifica arquivos e linguagens, com licencas e atribuicao embutidas. Assembly diferencia instrucoes comuns, controle de fluxo, registradores e alvos diretos usando os dados reais disponiveis. O grafo abre em escala 100% com texto legivel e oferece enquadramento geral explicito. Docking, abas, historico e persistencia continuam reais; movimento reduzido respeita a preferencia do sistema e a escolha explicita do usuario.

O assistente recebeu acoes e compositor compactos integrados ao tema. Continua preparando e copiando uma solicitacao local, sem modelo conectado. Foi reproduzida e corrigida a perda da pergunta quando o contexto possuia mais de 5.000 linhas curtas: o limite por blocos do preview foi removido, mantendo os limites existentes por caracteres. Ha teste com 6.000 linhas. Outros testes reproduziram previamente a fonte de 15px em vez de 14px, a altura excessiva do Quick Open vazio e o grafo inicialmente reduzido a 45%; os cenarios passaram apos as correcoes.

A inspecao visual em 125% encontrou controles comprimidos no hexadecimal e um nome longo de arquivo ocupando largura excessiva. A barra agora preserva a altura dos controles e o nome recebe elisao central, mantendo o caminho completo no tooltip. Um teste de regressao verifica o painel de 400x100 com nome de 120 caracteres. A geometria restaurada da janela tambem e limitada a area util da tela.

Validacao final executada: **build Release concluido; 26 testes nativos aprovados, zero falhas e zero ignorados, em 25,38 segundos**. Dentro desse conjunto, o executavel Qt registrou **23 aprovados** (21 cenarios mais inicializacao/encerramento), sem falhas ou ignorados, em 11,416 segundos. Uma execucao adicional com plataforma Windows e escala 125% registrou **10 aprovados** (oito cenarios mais inicializacao/encerramento), sem falhas ou ignorados, em 8,150 segundos. O wrapper offscreen aponta `QT_QPA_FONTDIR` para as fontes instaladas do Windows; nenhuma fonte Microsoft foi redistribuida. O plugin offscreen emite avisos sobre efeitos de janela nao suportados. A suite legada completa de 77 casos nao foi repetida nesta revisao.

A janela real foi executada com o projeto `build/demo-native` e inspecionada em 1366x738. Foram verificados Assembly/pseudocodigo, explorador, assistente, referencias, grafo, paleta e Quick Open. As capturas incluem `build/gui-ide-final.png`, `build/gui-ide-command-palette.png`, `build/gui-ide-quick-open.png`, `build/gui-ide-graph.png` e `build/gui-ide-125-final.png`. A revisao visual independente confirmou a correcao da barra hexadecimal na ultima captura a 125%, sem outro defeito visual importante observado nela. O aplicativo foi deixado aberto no pseudocodigo e nas referencias. Essa inspecao nao comprova identidade pixel a pixel com o VS Code nem uma avaliacao subjetiva de qualidade pelo usuario.

Evidencias: `build/ide-hex-layout-build.log` e `build/ide-hex-layout-build-retry.log` (objetos e links finais), `build/native-ide-final.log`, `build/native-ide-final.xml`, `build/ide-final-qt.txt` e `build/ide-windows-125-final.txt`. Executavel: `build/bin/reworkbench.exe`. A analise demonstrada continua parcial; esta entrega nao amplia a equivalencia semantica do decompilador nem conecta um provedor de IA.

## Refinamento Dark Modern, tipografia e Codicons

Esta revisao continua a interface C++/Qt existente. O tema usa tokens centralizados inspirados no Dark Modern oficial: editor #1F1F1F, laterais #181818, divisorias #2B2B2B e foco #0078D4. Segoe UI na interface e Cascadia Code/Consolas no codigo substituem a apresentacao anterior. As preferencias personalizadas e o docking real continuam preservados. Detalhes e referencias: [theme.md](../native/workbench/docs/theme.md).

O pseudocodigo passa por classificacao lexical ordenada de strings, caracteres e comentarios antes dos identificadores, tipos conhecidos, chamadas, numeros e operadores. Assembly usa os registradores presentes nos operandos decodificados, com reconhecimento lexical complementar. Isso nao acrescenta um motor semantico C++ nem recuperacao de tipos de alto nivel. Alturas de linhas tabulares acompanham a fonte, evitando cortes ao aumentar o texto.

Codicons oficiais estao embutidos no executavel, com tint e suporte a alta densidade, atribuicao e licencas CC BY 4.0/MIT. O assistente recebeu cabecalho e acoes compactos, mantendo a captura explicita de contexto e a preparacao local da solicitacao. Nenhum provedor de IA foi conectado.

Verificacao executada: **build Release concluido e 26 testes nativos aprovados, zero falhas e zero ignorados**, em 28,48 segundos. O teste de interacoes Qt inclui onze cenarios mais inicializacao/encerramento, totalizando **13 aprovados**, sem falhas ou ignorados. Os novos testes de cor base e sintaxe foram executados antes da implementacao e falharam pelas cores antigas e strings sem classificacao; a execucao final passou. A cobertura inclui fonte persistida e altura das linhas, abas, navegacao, historico, referencias, docking, encerramento, preparacao local de IA e icones renderizados a 2x. O plugin offscreen emite avisos sobre recursos de janela que nao implementa; nao se declara execucao sem avisos. A suite legada completa nao foi repetida nesta revisao visual.

A janela Windows foi aberta com `build/demo-native` e inspecionada em **1364x712**. Foram conferidos explorador, Assembly/pseudocodigo, painel inferior, assistente e grafo. A aba de grafo e seus botoes de zoom foram acionados pela UI Automation do Windows; ao final, a aba de pseudocodigo foi restaurada. Capturas da janela real: `build/gui-dark-modern.png`, `build/gui-dark-modern-graph.png` e `build/gui-dark-modern-graph-detail.png`. O ajuste inicial do grafo oferece uma visao geral; o zoom permite ler seus blocos. A revisao independente de fonte e da captura nao encontrou outros problemas relevantes, apos um ajuste documental na descricao das licencas.

Logs: `build/dark-modern-build.log`, `build/native-dark-modern.log`, `build/native-dark-modern.xml` e `build/dark-modern-qt.txt`. Executavel: `build/bin/reworkbench.exe`. A compilacao/execucao valida esta revisao da interface, sem demonstrar superioridade tecnica sobre o Ghidra ou equivalencia geral do decompilador.

## Revisao do layout inspirado no VS Code e navegacao de binarios

A revisao substitui a barra de ferramentas extensa e os exploradores tabulares laterais por uma faixa superior unica com menus, historico, busca e controles de janela, um explorador de binario em arvore e um painel inferior recolhivel. Abas de analise curtas, breadcrumbs e listagem compacta liberam altura para o codigo. O assistente possui sugestoes e compositor inferior; a preparacao continua local, sem modelo conectado.

Navegacao: Ctrl+P busca funcoes; Ctrl+Tab alterna documentos; Enter/F12 segue destinos diretos ou de memoria; Shift+F12 abre referencias. O historico inclui a funcao inicial. Selecao de instrucao destaca seu bloco; duplo clique no bloco navega para a entrada. Referencias conservam o endereco exato da origem e a selecao consulta bytes da amostra da analise. Tokens do pseudocodigo ainda nao possuem mapeamento para instrucoes.

O layout usa `workspace/v2`/estado 3 para substituir a disposicao antiga na primeira abertura desta revisao, mantendo a preferencia completa de fonte. A interface foi inspecionada em 1366x738. O nucleo e os formatos do projeto continuam inalterados por esta revisao visual.

Uma falha intermitente foi reproduzida no encerramento de janela visivel: o sinal `visibilityChanged` de um dock acessava `MainWindow::actions_` apos a destruicao dos membros. O GDB confirmou `QMap::operator[]` no callback durante `QWidgetPrivate::hideChildren`. A conexao agora chama diretamente `QAction::setChecked`, com tempo de vida controlado pelo QObject receptor. Ha teste de destruicao de quatro janelas visiveis sem chamar close, e a espera da arvore procura o destino real, em vez de contar itens ainda em carregamento. Evidencia do diagnostico: `build/vscode-gdb-stack.txt`.

Validacao final: **build Release concluido, 26 testes nativos aprovados, zero falhas e zero ignorados**, em 41,37 segundos. O executavel Qt possui nove cenarios de interface mais inicializacao/encerramento, totalizando **11 aprovados** por execucao. Oito repeticoes adicionais terminaram com codigo zero, inclusive verificando encerramento e persistencia de fonte. As configuracoes dos testes usam formato INI explicito em diretorios temporarios individuais; o aplicativo normal continua com o formato padrao do Windows. A execucao paralela identificou e corrigiu a antiga suposicao de que setDefaultFormat alterava o construtor com organizacao/aplicacao implicito.

Logs finais: `build/vscode-isolated-build.log`, `build/native-vscode-final.log`, `build/native-vscode-final.xml` e `build/vscode-isolated-stable-1.txt` ate `-8.txt`. Captura da janela real: `build/gui-vscode-workspace.png`. A suite historica completa nao foi repetida para esta revisao C++/Qt.

## Interface preta e espaco de trabalho personalizavel — primeira revisao

Implementacao C++/Qt da alpha 0.3.0: barra de atividades, abas de funcoes e representacoes reordenaveis/fechaveis, assembly e pseudocodigo lado a lado, paineis moveis/redimensionaveis/destacaveis, restauracao de layout e fonte completa, paleta de comandos e numeros de linha. Os editores e o grafo usam fundo #000000.

O Assistente IA prepara e copia uma solicitacao local com contexto explicitamente capturado da funcao. Nao ha provedor conectado nem envio de rede; Enviar fica desabilitado. Trocar a selecao invalida o contexto anterior.

Os testes de interacao usam os widgets C++ reais e QSettings temporario. Foram escritos antes da implementacao: a primeira execucao registrou seis cenarios falhando por ausencia das novas funcionalidades. A revisao posterior identificou e corrigiu a recuperacao da aba ativa apos navegar para um endereco sem funcao e a perda de peso/italico da fonte ao reabrir. Ambos possuem verificacoes na suite.

As instrucoes de uso estao em [workspace.md](../native/workbench/docs/workspace.md). A verificacao do prototipo historico abaixo permanece como evidencia da etapa anterior; a suite nativa e a referencia para este layout.

Validacao final desta interface: **build Release concluido e 26 testes nativos aprovados, zero falhas e zero ignorados**, em 17,67 segundos. Inclui os 25 casos anteriores e o executavel de interacoes Qt, com seis cenarios de interface. Logs: `build/ui-reviewed-build.log`, `build/native-workspace-final.log` e `build/native-workspace-final.xml`. A suite legada completa nao foi repetida para esta mudanca de UI.

A janela final foi executada com `build/demo-native`, inspecionada visualmente e teve o layout padrao restaurado pelo menu real. Captura: `build/gui-black-workspace.png`. A imagem confirma explorador, assembly/pseudocodigo, referencias inferiores e assistente a direita. A revisao de codigo nao identificou outros problemas importantes apos as correcoes acima.

## Validação nativa autorizada em 15/10/2026

A orientação anterior de não compilar nem executar foi substituída pela autorização explícita do usuário para testes, compilação e execução. Ambiente: Windows x64, MinGW 13.1.0, CMake 4.4.3, Ninja 1.13.2 e Qt 6.8.3 com QSQLITE. As dependências de build ficam em `.tools` na raiz do repositório; o aplicativo compilado não exige Python ou Ghidra.

- Build Release sem Capstone: concluído. `windeployqt` colocou DLLs de runtime, plugin de plataforma Windows e QSQLITE em `build/bin`.
- Testes nativos sem Capstone: **18 aprovados e 7 ignorados**, em 14,58 segundos. Os ignorados exigem decoder ou extensão de exemplo. Inclui leitura de formatos, seleção universal, erros de entrada, persistência/reabertura/movimentação e hash de amostras, patch em cópia, lançamento/leitura de registradores/desconexão do depurador com alvo próprio e abertura/fechamento da janela C++.
- Capstone 5.0.6: compilada e instalada localmente para x86, ARM, AArch64 e MIPS. Build Release com decoder, extensão de exemplo e alvo próprio de depuração: concluído.
- Testes nativos com Capstone: **25 aprovados, nenhum ignorado**, em 15,34 segundos. Além dos fluxos anteriores, cobre resultado escalar conhecido (42 + 1 = 43), os dois caminhos de um desvio condicional, interrupção por entrada desconhecida, limite de um laço infinito, decompilação/cache repetível e execução da extensão própria com verificação de hash e opt-in.
- Testes anteriores de unidade/interface Python: **32 aprovados**. Esses testes pertencem ao protótipo e não substituem os testes do executável C++.
- Suíte completa do repositório: **77 aprovados, zero falhas e zero ignorados**, em 490,28 segundos (8min10s). Inclui os 25 casos nativos e 52 do legado, com os 13 casos reais da integração histórica com Ghidra, persistência de nomes/notas pela sessão, supervisão, protocolo e interface Python. A integração legada é exercitada explicitamente pelos testes; não é motor do aplicativo C++.

A interface nativa foi aberta com `build/demo-native`, analisando a fixture própria `tests/fixtures/bin/pe64.exe`: duas funções candidatas e nove referências. O estado é `partial`, coerente com as limitações da análise. A inspeção visual confirmou funções, assembly, pseudocódigo, referências e bytes; a janela permaneceu responsiva. Captura local: `build/gui-smoke.png`. O relatório do projeto está em `build/demo-native/exports/analysis.json`. O executável desktop está em `build/bin/reworkbench.exe`; a CLI, em `build/bin/reworkbench-cli.exe`.

Foram corrigidas duas colisões com a macro Qt `slots`, nos nomes internos do decompilador e do painel de imports. Os erros foram reproduzidos pelo compilador antes das correções. O teste do depurador passou a usar um alvo C++ próprio, finito e sem saída; `--version` do Qt abriu uma caixa modal quando o alvo ficou sem terminal. O teste de Mach-O universal foi ajustado ao contrato: o contêiner continua universal após selecionar sua arquitetura interna.

Logs e relatórios locais: `build/build-no-capstone.log`, `build/build-no-capstone-retry.log`, `build/build-no-capstone-final.log`, `build/native-no-capstone-tests-final.log`, `build/native-no-capstone.xml`, `build/build-capstone.log`, `build/native-capstone-tests.log`, `build/native-capstone.xml`, `build/full-suite.log` e `build/full-suite.xml`. Há avisos de compilação não bloqueantes; não se declara um build sem warnings.

### Reproduzir o build local

Os caminhos abaixo usam as dependências já preparadas nesta máquina e partem da raiz do worktree `.worktrees/implementation`. Não instalam componentes nem alteram o PATH global:

```powershell
$deps = (Resolve-Path ../../.tools).Path
$env:PATH = "$deps/qt/Tools/mingw1310_64/bin;" + $env:PATH
cmake -S . -B build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_CXX_COMPILER="$deps/qt/Tools/mingw1310_64/bin/g++.exe" `
  -DCMAKE_MAKE_PROGRAM="$deps/build-python/bin/ninja.exe" `
  -DCMAKE_PREFIX_PATH="$deps/qt/6.8.3/mingw_64" `
  -DREWORKBENCH_WITH_CAPSTONE=ON `
  -DREWORKBENCH_CAPSTONE_INCLUDE_DIR="$deps/capstone/include" `
  -DREWORKBENCH_CAPSTONE_LIBRARY="$deps/capstone/lib/libcapstone.a" `
  -DREWORKBENCH_BUILD_EXAMPLE_EXTENSION=ON `
  -DREWORKBENCH_BUILD_TEST_FIXTURES=ON `
  -DREWORKBENCH_BUILD_UI_TESTS=ON
cmake --build build --parallel 1
& "$deps/qt/6.8.3/mingw_64/bin/windeployqt.exe" --release --compiler-runtime --no-translations --no-opengl-sw --no-system-d3d-compiler build/bin/reworkbench.exe
Copy-Item build/native/workbench/example_extension/libreworkbench_example_extension.dll build/bin/
Copy-Item "$deps/qt/6.8.3/mingw_64/bin/Qt6Test.dll" build/bin/
Copy-Item "$deps/qt/6.8.3/mingw_64/plugins/platforms/qoffscreen.dll" build/bin/platforms/
.venv/Scripts/python.exe -m pytest -v --junitxml=build/full-suite.xml
.\scripts\run.ps1 -Project "$PWD/build/demo-native"
```

Para rodar somente os testes do produto atual, use `.venv/Scripts/python.exe -m pytest tests/native -v`. O projeto de demonstração mantém sua trava enquanto a janela está aberta; feche-a antes de usar a CLI nesse mesmo projeto. Veja também [tests/native/README.md](../tests/native/README.md).

Esta é validação inicial, sem comprovação geral de equivalência semântica do decompilador, cobertura de ISAs, robustez contra todos os arquivos malformados, desempenho industrial ou superioridade sobre o Ghidra. Os registros abaixo preservam o histórico anterior à autorização e não substituem os resultados atuais.

## Ampliação nativa 0.3

Código acrescentado: decompilador escalar x86/x64 com SSA e propagação conservadora; enriquecimento PE/ELF e linhas DWARF 2–4; mapas de símbolos; resolução indireta limitada; comparação binária/de funções; patch em cópia; interpretador escalar; depurador Windows; SDK de extensões; CLI; cache por conteúdo e reaproveitamento de análise compatível. Todos são código-fonte integrado ao CMake e aos fluxos documentados, **sem configuração, compilação, testes, execução, instalação ou carregamento de extensão nesta etapa**.

A revisão desta ampliação foi leitura de fonte pelos implementadores e pelo agente principal. A revisão independente do conjunto não terminou porque o serviço dos subagentes atingiu o limite de uso; nenhuma conclusão de aprovação foi recebida para o conjunto 0.3. As revisões independentes registradas nas seções anteriores pertencem a entregas anteriores.

Além dos itens nativos abaixo, a aceitação posterior deve cobrir SSA/phi em junções e backedges, flags e registradores parciais, invalidação opaca, cópias simultâneas, condições simplificadas, limites de saída, alvos indiretos e relocações, linhas DWARF malformadas, mapa com identidade divergente, reabertura/movimentação de projetos com mapas e cache, patch entre blocos e aliases, emulação com entradas desconhecidas, depuração em cada fase do ciclo de vida e contratos de extensões. O harness `benchmark` foi apenas escrito: não há tempos, uso de memória ou resultados comparativos obtidos.

Ainda faltam capacidades importantes de um produto completo: tipos/assinaturas/ABI e variáveis de alto nível, semântica ampla das ISAs, análise interprocedural, switches/laços gerais, PDB completo, DWARF5 e arquitetura mista por endereço. Código e revisão estática não sustentam afirmação de superioridade sobre o Ghidra.

## Base nativa 0.2 em C++20/Qt e aceitação ainda pendente

A direção atual está em [independent-native-workbench-design.md](superpowers/specs/2026-10-14-independent-native-workbench-design.md). O executável principal tem interface C++/Qt, carregadores próprios, descoberta de funções, referências, CFG e IR próprios, com Capstone 5 opcional para decodificar instruções. O caminho padrão não inicia Python, Ghidra ou JVM. Os registros mais abaixo descrevem o protótipo anterior e não validam esta versão nativa.

O código nativo foi escrito e lido estaticamente. **Não foi compilado, configurado, testado ou executado.** Os scripts de preparação e diagnóstico também não foram executados nesta etapa. Não há medição de desempenho ou afirmação de equivalência com um decompilador completo. A IR atual cobre uma parte das instruções; efeitos desconhecidos permanecem opacos, com cobertura e avisos visíveis.

Aceitação futura, somente após nova orientação do usuário:

1. Compilar no Windows com Qt 6.5 ou posterior e C++20, com e sem Capstone 5; verificar DLLs e o plugin QSQLITE, inicialização e caminhos Unicode.
2. Exercitar PE, ELF, Mach-O, imagens universais e firmware bruto, incluindo arquivos truncados, tabelas sobrepostas, inteiros extremos, BSS sem bytes e seleção explícita de arquitetura, base, entradas e slice.
3. Conferir instruções, alvos, referências, limites de função e CFG contra amostras conhecidas; verificar paradas em interworking ARM/Thumb, Thumb IT, saltos indiretos e delay slots MIPS.
4. Conferir a IR contra a semântica das instruções modeladas, incluindo larguras, subregistradores, flags desconhecidas e efeitos opacos. Ausência do decoder deve produzir capacidade limitada explícita.
5. Exercitar cancelamento, limites de recursos, fechamento durante tarefas, busca paginada e resultados atrasados de seleção, leitura de bytes e progresso.
6. Criar, reabrir e mover projetos; conferir travas, notas e renomeações persistidas, falhas de gravação e retenção dos rascunhos. Confirmar recusa de cópia alterada, mesmo mantendo o tamanho: o carregamento da análise recalcula SHA-256. Verificar os limites individuais e agregados de notas e nomes antes de trazer seus textos do SQLite.
7. Conferir SHA-256 na importação, cópias atômicas e exportações, proteção de arquivos de origem e internos, links, estados parciais e preservação dos projetos antigos sem migração implícita.

## Histórico do protótipo anterior

Em 14/10/2026 o usuário determinou: **“só coda agora, nao teste e compile nada ainda”**. A partir dessa instrução, o trabalho segue somente com escrita de código e leitura estática. Não foram iniciados novos testes, builds, compilações, instalações de dependências ou execuções do aplicativo.

## Evidências anteriores à instrução

Antes dessa mudança de orientação foram exercitados armazenamento de projetos, configuração do ambiente, protocolo, supervisão e coordenação da sessão. A matriz de integração real com Ghidra terminou com **11 casos aprovados e 2 falhas**. Ela já estava encerrada quando sua saída foi consultada após a instrução.

Uma falha ocorreu ao criar uma junction temporária necessária para um projeto em caminho oculto; o mesmo ELF foi importado em outro caso. A outra foi uma divergência entre a expressão esperada por um teste e o texto de uma mensagem de configuração de firmware bruto. O código de tratamento desses pontos foi alterado posteriormente e **não foi reexecutado**.

As evidências prévias não validam a versão atual inteira. A interface e as alterações posteriores da sessão ainda não receberam validação de execução.

## Código acrescentado sem execução

- Interface Qt, exploradores paginados, configuração de importação, navegação, assembly e pseudocódigo.
- Visualização de bytes por intervalos e triagem de strings e entropia.
- Auxiliar opcional em Rust, com alternativa em Python.
- Núcleo próprio C++20 de triagem, com biblioteca estática, CLI JSON e prioridade na seleção do processador nativo. Código acrescentado após o usuário exigir C++ próprio; ainda sem compilar ou executar.
- Notas, marcadores, pistas de auditoria e exportação JSON / pseudocódigo.
- Ajustes de cancelamento, encerramento, proteção de destinos de exportação e revisão de cache.
- Scripts de instalação, inicialização e diagnóstico.

Somente uma revisão estática pode ser realizada nesta etapa. A compilação do auxiliar Rust, os testes automatizados e a verificação manual dos fluxos da interface ficam pendentes até nova orientação do usuário.

A revisão independente identificou nove pontos de integração e de limites de memória. O código foi alterado para tratá-los em uma única rodada, sem reexecução nem nova revisão independente. As decisões e as alterações estão registradas em [implementation-notes.md](implementation-notes.md).

A inclusão posterior do módulo C++20 recebeu uma revisão estática independente com escopo no novo código nativo, CMake, adaptador e documentação. Essa revisão não identificou problemas críticos ou importantes. A aceitação de build, os caminhos Unicode no Windows e a equivalência com Rust/Python continuam sem validação de execução.

## Verificações pendentes para uma etapa posterior

1. Inicialização da interface com e sem motor configurado, configuração persistente e restauração dos painéis.
2. Importação real da matriz de arquiteturas, incluindo seleção de imagem universal, base e contexto Thumb.
3. Navegação função → assembly → bytes → referências → função, incluindo seleção durante a indexação e resultados atrasados.
4. Renomeação, notas e marcadores após fechar e reabrir, com reconstrução do índice a partir do programa salvo.
5. Cancelamento e fechamento durante início da JVM, autoanálise, indexação, decompilação e triagem; verificar os processos filhos.
6. Exportação íntegra e substituição atômica diante de falhas, endereços de 64 bits, estados parciais e preservação das amostras.
7. Compilação separada do núcleo C++20 e do auxiliar Rust; comparação de ambos com Python, incluindo strings que cruzam blocos, EOF, prévias truncadas, limite zero, JSON e caminhos Unicode no Windows. Confirmar também a prioridade C++ → Rust quando ausente → Python.

Essa lista registra trabalho de validação futuro e não representa verificações já realizadas.
