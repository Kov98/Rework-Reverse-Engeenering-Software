# Espaco de trabalho desktop

O Rework continua em alpha 0.3.0. Esta interface e uma primeira bancada utilizavel; a maturidade do decompilador e os limites de analise continuam descritos nos documentos do motor.

## Navegacao e personalizacao

- A faixa superior reune menus, historico, busca central e controles da janela. A barra lateral abre explorador, busca de funcoes, grafo, depurador e assistente.
- O explorador mostra a amostra em arvore: funcoes, imports, regioes, simbolos e strings. Um clique ou Enter abre o destino. O filtro procura nomes, enderecos e detalhes; categorias grandes carregam mais resultados em lotes de 1.000, com processamento em segundo plano.
- Abrir uma funcao pelo explorador cria uma aba. Clicar na aba navega novamente para aquela funcao. As abas podem ser arrastadas para reordenar e fechadas pelo X ou Ctrl+W.
- Assembly e representacoes ficam lado a lado, separados por um divisor ajustavel. Pseudocodigo, SSA, IR e grafo possuem abas reordenaveis e podem ser reabertos em Visualizar > Editores.
- Referencias, hexadecimal, saida, dados tabulares, anotacoes e metadados compartilham um painel inferior com abas no topo. Sua barra de opcoes permite mover ou destacar o painel. Explorador e assistente podem ser arrastados pelos cabecalhos, destacados ou ocultos.
- A ordem e visibilidade das representacoes, a representacao ativa, os divisores, os paineis e a fonte sao restaurados ao reabrir. As abas de funcoes sao especificas da analise aberta e nao sao documentos persistidos.
- Visualizar > Restaurar layout padrao recupera paineis fechados ou destacados.
- Ctrl+Shift+P abre a paleta: digite parte do comando e pressione Enter ou escolha um item na lista.
- Ctrl++ e Ctrl+- ajustam a fonte do codigo; Ctrl+0 restaura Cascadia Code (ou Consolas) 14px logicos (10,5pt a 96 DPI). A engrenagem lateral tambem oferece escolha de fonte e restauracao do layout.
- Visualizar > Reduzir movimento desliga a animacao nativa de docking e menus e salva a preferencia. A configuracao de movimento reduzido do Windows tem precedencia.

O tema [Rework Dark](theme.md) usa editores/grafo #232327, laterais #1E1E22, divisorias #34343C e foco roxo derivado do logo. Menus, paleta, campos, selecoes e controles usam raios centralizados; editores e paineis mantem encaixe reto. Codicons representam acoes/simbolos e Seti representa arquivos. O logo oficial aparece na faixa superior, em Sobre o Rework e no icone Windows. A janela possui controles proprios, arraste pelo espaco livre da faixa superior, duplo clique para maximizar/restaurar e redimensionamento pelas bordas no Windows. O layout continua salvo em `workspace/v2`; a revisao visual preserva essa disposicao e as preferencias personalizadas de fonte.

| Atalho | Acao |
| --- | --- |
| Ctrl+P | Busca rapida de funcao por nome/endereco |
| Ctrl+Shift+P | Paleta de comandos |
| Ctrl+Tab / Ctrl+Shift+Tab | Proxima aba / aba anterior |
| Ctrl+W | Fechar aba |
| Alt+Esquerda / Alt+Direita | Historico de navegacao |
| Ctrl+G | Ir para endereco |
| Enter / F12 na listagem | Seguir destino da instrucao |
| Shift+F12 | Abrir referencias |
| Ctrl+B / Ctrl+J / Ctrl+Alt+B | Alternar explorador / painel inferior / IA |
| Ctrl+1 / Ctrl+2 | Focar listagem / pseudocodigo |
| Ctrl+\ | Alternar listagem lado a lado |
| Ctrl+F no editor | Buscar texto no pseudocodigo, SSA ou IR |
| F3 / Shift+F3 no editor | Proximo / anterior resultado, com retorno ao inicio/fim |
| Esc na busca | Fechar busca e devolver foco ao codigo |

A selecao de instrucao destaca seu bloco no grafo, posiciona pseudocodigo/SSA/IR nas linhas com origem conhecida e atualiza anotacoes e bytes. Mover o cursor para uma linha mapeada faz o caminho inverso. O editor de origem conserva a selecao, inclusive um resultado de busca quando o assembly precisa carregar outra pagina. A associacao e emitida pelos renderizadores a partir das instrucoes; declaracoes, entradas, phis e outros trechos sinteticos sem origem unica nao recebem um endereco presumido. Isso e navegacao por linha, nao recuperacao de variaveis/tipos nem mapeamento de cada token.

Um clique em um bloco seleciona sua entrada nos outros visores; duplo clique navega para ela. Mover dentro da mesma funcao preserva os documentos, a busca e os trabalhos em andamento. Uma revisao diferente da analise, como renomear uma funcao, reconstroi os documentos e invalida o contexto anterior. Voltar/Avancar conserva a instrucao selecionada antes de seguir um destino e inclui trocas de analise. Fechar uma aba em segundo plano preserva a posicao da aba ativa.

A busca e literal e local a cada editor, com contagem, opcao de maiusculas/minusculas e estado sem resultados. O realce e limitado a 1.000 ocorrencias; acima disso a contagem mostra `1000+`, enquanto F3 continua percorrendo todas. Substituir o texto atualiza a busca e limpa associacoes de enderecos antigas. O grafo abre em escala de leitura (100%) e possui controles de zoom e ajuste para visao geral; Ctrl+roda tambem aproxima/afasta.

## Preparacao local de IA

1. Selecione uma funcao e abra Assistente IA pela barra lateral.
2. Use + Funcao atual no compositor para anexar o contexto: amostra, hash, arquitetura, endereco, assembly e pseudocodigo parcial. A previsualizacao pode ser expandida acima do compositor.
3. Escreva a pergunta, ou escolha uma sugestao, e use a seta de preparacao. Sem captura previa, a solicitacao indica que o contexto nao foi incluido.
4. Revise e copie o texto. O contexto e limitado a 32.768 caracteres; a pergunta, a 8.000.

Trocar de funcao ou analise limpa o contexto capturado e a solicitacao preparada. Mudar a pergunta tambem invalida a solicitacao anterior. O painel nao possui modelo conectado, resposta simulada ou envio pela rede. Enviar fica desabilitado ate uma implementacao posterior de provedor.

## Verificacao

`REWORKBENCH_BUILD_UI_TESTS=ON` habilita o executavel opcional `reworkbench-ui-tests`, que usa Qt Test com os componentes reais. Os casos exercitam tema, persistencia, restauracao, fonte, preparacao de contexto e navegacao entre funcoes. Veja [validation.md](../../../docs/validation.md) e [tests/native/README.md](../../../tests/native/README.md).
