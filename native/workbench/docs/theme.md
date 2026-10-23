# Rework Dark: tema, tipografia e icones

A interface usa **Rework Dark**, com superficies grafite e destaque roxo derivado do logotipo oficial. Dark Modern do VS Code permanece a referencia de sintaxe e densidade. A estrutura C++/Qt, o motor de analise, os projetos e os mecanismos reais de docking foram preservados.

## Tokens centralizados

`ui/workspace_theme.cpp` define cores e metricas por nomes de tokens. Widgets, editores, grafo e assistente consultam `theme_color` / `theme_hex` e `theme_metric`; `themed_stylesheet` resolve referencias `@{token}` em QSS, incluindo metricas com unidade px. `theme_name()` identifica o tema. Cores com alfa seguem a ordem RRGGBBAA do VS Code e sao convertidas explicitamente para QColor/QSS. Isso prepara a evolucao de temas sem acrescentar marketplace ou carregamento de extensoes.

| Token | Valor | Uso |
| --- | --- | --- |
| editor.background | #232327 | Codigo, listagem e grafo |
| editor.foreground | #D4D4D8 | Texto principal |
| sideBar.background | #1E1E22 | Explorador e assistente |
| titleBar.activeBackground | #202024 | Faixa superior |
| activityBar.background | #1B1B1F | Barra de atividades |
| panel.background | #1E1E22 | Painel inferior |
| panel.border | #34343C | Divisorias |
| brand.primary | #6018F8 | Acao primaria, amostra do logotipo |
| focusBorder | #A074FB | Foco, abas e indicadores ativos |
| input.background | #2E2E35 | Entradas |
| menu.background | #29292F | Menus e pop-ups |
| tab.activeBackground | #232327 | Aba ativa |

Os papeis de superficies e estados seguem a organizacao do [Dark Modern](https://github.com/microsoft/vscode/blob/main/extensions/theme-defaults/themes/dark_modern.json) e dos [tokens documentados](https://code.visualstudio.com/api/references/theme-color), com cores de marca proprias. O roxo #6018F8 e a amostra quantizada mais frequente nos pixels roxos de alfa alto do logotipo; o foco #A074FB mistura essa base com 40% de branco para legibilidade em fundos escuros. A origem, hash e conversao do logo estao em [assets/brand/README.md](../assets/brand/README.md). As cores de codigo seguem a heranca [Dark+](https://github.com/microsoft/vscode/blob/main/extensions/theme-defaults/themes/dark_plus.json) / [Dark VS](https://github.com/microsoft/vscode/blob/main/extensions/theme-defaults/themes/dark_vs.json), sem substituir semantica por roxo.

Raios centralizados: controles 5px, campos 6px, itens 5px, pop-ups 10px e compositor 8px; unidade de espacamento 4px. Paineis e editores mantem encaixe reto. Hover usa cinza neutro; selecao usa cinza ou roxo de baixa saturacao; foco tem contorno proprio. Menus recebem translucidez e `FramelessWindowHint`, conforme o [suporte de transparencia do Qt 6.8](https://doc.qt.io/qt-6.8/qwidget.html#creating-translucent-windows). A paleta pinta sua propria superficie alfa, incluindo cantos apos filtragem e redimensionamento.

Os nomes `syntax.*` sao a ponte interna para os papeis dessas cores: controles em roxo, palavras-chave e tipos primitivos em azul, tipos nomeados em verde-agua, funcoes em amarelo claro, variaveis/parametros em azul claro, strings em salmao, numeros em verde claro e comentarios em verde. Registradores usam o papel de variavel; mnemonicos comuns ficam cinza e instrucoes de controle de fluxo ficam roxas conforme o FlowKind real. Alvos diretos de chamadas usam amarelo claro; os demais enderecos permanecem discretos.

## Leitura de codigo

Diagnostico desta revisao: a familia global ja estava correta, mas QSS local/global reduzia cabecalhos, abas e campos para 11/12px. Os controles principais agora herdam 13px e apenas metadados secundarios usam `typography.captionPixels` de 12px. Qt preserva o rasterizador e a escala nativos do Windows; nao se aplica reducao global de zoom nem se redistribuem fontes Microsoft. A verificacao consulta QFontInfo depois de polish, inclusive nos menus reais.

Um teste isolado na plataforma Windows a 125% reproduziu outro problema: instalar o primeiro stylesheet cria um QStyleSheetStyle, que reinicializa fontes por classe da plataforma depois do setFont global anterior. QMenuBar voltava a Segoe UI 9pt/12px. A fonte compartilhada agora e aplicada ao final da instalacao do estilo. A cadeia foi conferida no [QApplication do Qt 6.8.3](https://github.com/qt/qtbase/blob/v6.8.3/src/widgets/kernel/qapplication.cpp); o teste registra a fonte efetiva por componente e cobre a primeira janela, sem depender de uma segunda aplicacao do tema.

A interface usa Segoe UI, 9,75 pontos (13px logicos a 96 DPI). O codigo usa Cascadia Code quando disponivel, com alternativa Consolas ou fonte monoespacada do sistema, 10,5 pontos (14px logicos). O espacamento de linhas segue round(fontSize * 1.35), 19px no padrao Windows, sem reduzir as metricas de uma fonte personalizada maior. Essas referencias vem de [fontInfo.ts](https://github.com/microsoft/vscode/blob/main/src/vs/editor/common/config/fontInfo.ts) e [style.css](https://github.com/microsoft/vscode/blob/main/src/vs/workbench/browser/media/style.css). O layout de texto Qt ajusta posicoes/alturas de linhas sem alterar o texto nem os formatos do highlighter. A escolha personalizada continua persistida; a revisao de tipografia migra os antigos valores padrao 10/11pt normais quando nao marcados como personalizados. Ctrl+0 restaura o novo padrao. Uma familia indisponivel recebe uma alternativa instalada explicita e diagnostico, preservando tamanho, peso e italico.

A paleta segue a proporcao oficial min(600px, 62% da largura), entrada de 32px e linhas de 22px; cresce somente conforme resultados e oferece nomes, descricoes e atalhos em regioes separadas. Referencias: [quickInputController.ts](https://github.com/microsoft/vscode/blob/main/src/vs/platform/quickinput/browser/quickInputController.ts) e [quickInput.css](https://github.com/microsoft/vscode/blob/main/src/vs/platform/quickinput/browser/media/quickInput.css). A implementacao Qt e propria. Selecao discreta na paleta, tabelas e blocos evita grandes areas azuis; nao e uma alegacao de igualdade pixel a pixel com o Electron.

O pseudocodigo possui analise lexical ordenada para comentarios, strings/caracteres escapados e estados entre linhas, seguida da classificacao de identificadores, chamadas, tipos conhecidos e operadores. Isso nao representa uma nova analise semantica C++ ou recuperacao de tipos do binario. Na listagem Assembly, os nomes de registradores disponiveis nos operandos decodificados orientam a classificacao; outras formas conhecidas de registradores e qualificadores usam reconhecimento lexical.

Numeros de linha, linha atual, guias de indentacao e selecao usam o mesmo sistema de cores. Assembly e pseudocodigo compartilham a fonte e mantem alinhamento, rolagem horizontal e navegacao.

## Codicons e licencas

Codicons agora sao centrados pela caixa de tinta do glifo via QGlyphRun, sem depender do ascent/leading de linha da fonte no Windows. Seti reserva 1px de margem proporcional a 16px, preservando o contorno original. Renderizacao e buffers continuam levando em conta o DPR. Ha simbolos distintos para export, referencia, instrucao, grafo e breakpoint; nao sao tipos de arquivo. O logo oficial nao recebe recoloracao, recorte ou redesenho.

Uma unica familia de [Codicons oficiais](https://code.visualstudio.com/api/references/icons-in-labels) e embutida como fonte Qt. O aplicativo nao depende de instalacao dessa fonte ou de acesso a rede. O helper `workbench_icon` renderiza os glifos com cor, estado e densidade de pixels do destino.

Icones/fonte: Microsoft e colaboradores, CC BY 4.0. Codigo de origem: MIT. A versao, os hashes, a atribuicao e os caminhos das licencas completas estao em [assets/codicons/NOTICE.md](../assets/codicons/NOTICE.md). Ambas as licencas estao embutidas nos recursos e incluidas na instalacao; Ajuda > Sobre exibe a atribuicao e o texto completo da licenca dos icones/fonte. A familia nao inclui marcas ou logotipos do VS Code.

Icones de arquivos e linguagens usam a fonte Seti UI original sob MIT, com cores e glifos de origem documentada em [assets/seti/NOTICE.md](../assets/seti/NOTICE.md). `file_icon` escolhe pela extensao real; `symbol_icon` usa a categoria de simbolo, sem confundir um arquivo C++ com uma funcao ou inventar arquivos no explorador. Raizes binarias e bibliotecas usam Codicons adequados. Licencas e atribuicao completas de ambas as familias estao embutidas e disponiveis em Ajuda > Sobre.

## Escopo de verificacao

Os testes da UI cobrem cores base, strings que contem marcadores de comentarios, comentarios multilinha, classificacao de variaveis/chamadas/numeros e icones embutidos com tint e alta densidade. Os testes existentes continuam cobrindo abas, historico, fontes, docking, encerramento e preparacao local da IA. Resultados efetivamente executados ficam em [validation.md](../../../docs/validation.md).
