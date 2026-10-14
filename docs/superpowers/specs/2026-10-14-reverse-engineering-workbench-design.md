# Bancada desktop de engenharia reversa — especificação da primeira versão

Data: 2026-10-14

Estado: arquitetura, primeiro marco e especificação escrita aprovados pelo usuário em 2026-10-14.

## 1. Objetivo e entendimento aprovado

Criar um aplicativo desktop de engenharia reversa para três usos: compreender programas Windows, apoiar auditorias de vulnerabilidades e analisar firmware de diferentes arquiteturas. O usuário pediu uma ferramenta robusta, com várias ferramentas integradas, semelhante em propósito ao Ghidra, capaz de transformar binários em uma representação de código compreensível.

O usuário aprovou uma interface própria em Qt/PySide6, Ghidra como motor de análise em processo separado e SQLite para persistência. A primeira versão deve permitir uma investigação real de ponta a ponta: importar um arquivo, analisar, navegar por funções e referências, consultar assembly e pseudocódigo, registrar descobertas, salvar e reabrir o projeto.

Descompilação significa produzir pseudocódigo semelhante a C. Não garante recuperar o código-fonte original, nomes removidos, comentários, classes originais ou um projeto que possa ser recompilado. Essa distinção aparece na documentação e no contexto da exportação.

## 2. Decisão de abordagem

Foram consideradas três abordagens: aplicativo próprio sobre o motor Ghidra, extensão dentro da interface do Ghidra e criação de um motor novo. A primeira foi escolhida porque permite desenvolver a experiência de uso própria enquanto utiliza a análise e a descompilação existentes. A extensão limitaria a independência da interface; um motor novo ampliaria muito o trabalho antes de proporcionar uma análise útil.

A primeira distribuição será para Windows x86-64. A estrutura deve evitar dependências desnecessárias de Windows, mas empacotamento e validação em Linux/macOS ficam para marcos posteriores. Analisar formatos de outros sistemas não exige executar esses sistemas.

## 3. Escopo funcional

### 3.1 Importação e configuração

- Importar arquivos PE, ELF e Mach-O aceitos pelos carregadores da versão instalada do Ghidra. Executáveis e bibliotecas Windows são arquivos PE.
- Importar firmware bruto selecionando linguagem/processador, especificação de compilador, ordem dos bytes quando aplicável e endereço base. As opções vêm do catálogo do motor; não são uma lista inventada pela interface. Permitir informar endereços de entrada para iniciar disassembly e criação de funções; para ARM, preservar a variante e o contexto ARM/Thumb necessários à amostra. Na ausência dessas informações, não presumir que a autoanálise encontrará funções.
- Para um contêiner com múltiplas imagens reconhecidas, apresentar as opções e analisar uma imagem selecionada por trabalho. Extração genérica de sistemas de arquivos de firmware fica para depois.
- Incluir x86/x86-64, ARM/AArch64 e MIPS na matriz inicial de validação. O catálogo de outras arquiteturas pode ser exposto conforme a instalação, sem prometer que todas receberam o mesmo nível de teste.
- Mostrar tamanho, SHA-256, formato, arquitetura, endereçamento, ponto de entrada e regiões de memória, quando conhecidos. Campos desconhecidos aparecem como desconhecidos, sem inferência silenciosa.
- Manter o arquivo de origem intacto. Copiar o conteúdo para o projeto em blocos e calcular o hash durante a cópia; publicar a cópia somente após conclusão bem-sucedida.

### 3.2 Área de trabalho

- Janela principal com painéis redimensionáveis e acopláveis, menus, atalhos, histórico de navegação e indicação de trabalho/projeto ativo.
- Exploradores de funções, símbolos, strings, imports e regiões de memória, com busca e carregamento gradual dos resultados.
- Visualizações de assembly, pseudocódigo por função e bytes em hexadecimal. O painel hexadecimal inicial é de leitura.
- Selecionar uma função atualiza assembly e pseudocódigo. Referências e endereços navegáveis permitem seguir chamadas e retornar pelo histórico.
- Distinguir endereço virtual, espaço de endereços e deslocamento no arquivo. Regiões sem bytes no arquivo não recebem deslocamentos fabricados.
- Exibir referências de entrada e saída, comentários do usuário e marcadores associados a endereços ou funções.
- Permitir renomear funções pelo motor, com validação e atualização dos resultados afetados. Alteração de tipos e de assinaturas fica para um marco posterior.
- Interface em português, texto selecionável, foco de teclado visível e estados de carregamento, erro e resultado vazio distintos.

### 3.3 Apoio à auditoria

- Relacionar imports, strings e referências às funções que os utilizam, quando o motor resolver essas relações.
- Destacar chamadas que mereçam investigação a partir de uma pequena lista versionada de APIs, como cópia de strings sem limite. Cada indicação inclui a regra e o endereço que a originou.
- Tratar essas indicações como pistas de triagem, não como confirmação de vulnerabilidade. Uma função importada sem referência conhecida é exibida como tal.
- Permitir registrar observações e exportar as evidências examinadas com seu contexto.

### 3.4 Salvamento e exportação

- Salvar e reabrir projetos contendo amostras, configuração, dados do Ghidra, índices de navegação e anotações.
- Exportar relatório JSON com versão de esquema, identificação da amostra, configuração, estado da análise, funções, referências e observações disponíveis.
- Exportar o pseudocódigo das funções selecionadas para texto, incluindo identificação da amostra e endereços. Registrar falhas de descompilação por função.
- Permitir iniciar uma nova análise com outra configuração sem substituir silenciosamente os resultados anteriores.

## 4. Arquitetura e responsabilidades

| Unidade | Responsabilidade | Dependências e contrato |
| --- | --- | --- |
| Interface Qt/PySide6 | Painéis, navegação, seleção, estado do trabalho e apresentação de erros | Consome serviços da aplicação; não chama a JVM |
| Serviços da aplicação | Importação, ciclo dos trabalhos, comandos de navegação e exportação | Coordenam repositório, supervisor e mensagens tipadas |
| Supervisor do motor | Iniciar, acompanhar, cancelar e encerrar o worker | Processo Python separado, limites de tempo e IPC |
| Worker Ghidra | Importar, analisar, enumerar resultados, resolver referências, renomear e descompilar | PyGhidra e APIs Java do Ghidra |
| Repositório local | Manifesto, SQLite, índices, notas, marcadores e metadados de cache | Acesso ao banco fora da thread da interface |
| Leitor de bytes | Leitura paginada da amostra e apresentação hexadecimal | Leitura por intervalos; não carrega o arquivo inteiro |

Código pesado de análise e descompilação permanece no Ghidra. Python coordena o trabalho e a interface. A JVM nunca é iniciada dentro do processo da interface. Um worker atende uma análise por vez; a fila inicial executa um trabalho de análise por vez.

Dentro do worker, uma thread de execução serializa o acesso semântico ao programa. Um receptor de comandos independente pode cancelar o `TaskMonitor` ativo; uma thread de supervisão emite heartbeat a cada 5 segundos. Envios no canal são serializados. O supervisor externo também limita a duração de cada operação, pois heartbeat não comprova que uma operação esteja avançando.

A integração interativa será feita com PyGhidra em worker persistente. Isso evita reiniciar a JVM ao selecionar cada função e permite renomeação no mesmo projeto. `analyzeHeadless` é uma alternativa técnica para automação futura, sem manter dois caminhos de integração na primeira versão.

Os modelos Qt usam paginação e inserção incremental. As operações bloqueantes de IPC, banco, cópia e hash rodam fora da thread de interface. Os modelos visuais só são alterados na thread da interface por sinais enfileirados.

## 5. Comunicação e fluxo de análise

1. O aplicativo valida a configuração do motor, cria um trabalho e copia a amostra para o projeto.
2. O supervisor abre um canal local e inicia o worker sem janela de terminal adicional.
3. O worker inicia o Ghidra, identifica opções de carregamento e aguarda a seleção quando houver ambiguidade.
4. O worker importa a imagem, publica metadados e executa autoanálise com progresso e cancelamento.
5. Resultados são transmitidos em lotes e indexados pelo repositório. A interface pode navegar pelos snapshots já disponíveis, identificando resultados parciais. Os snapshots são publicados entre etapas estáveis; durante autoanálise, leituras adicionais do motor, renomeações e descompilação ficam aguardando. A leitura dos bytes da amostra permanece disponível.
6. A seleção de uma função solicita descompilação sob demanda. Respostas antigas não substituem a função atualmente selecionada; podem alimentar o cache correspondente.
7. Renomeações usam uma transação do Ghidra, salvam o programa e só então confirmam sucesso. O índice é atualizado e o cache de pseudocódigo da análise é invalidado.
8. Ao encerrar, o aplicativo salva o estado, fecha o programa/projeto no worker e encerra o processo.

O IPC utiliza JSON UTF-8 em mensagens enquadradas sobre conexão local autenticada, com `multiprocessing.connection.send_bytes/recv_bytes`; não utiliza serialização pickle. O listener é restrito a `127.0.0.1`, com porta efêmera e segredo aleatório por execução transmitido ao worker no ambiente do processo. Não há servidor de acesso remoto.

O handshake informa versão do protocolo e capacidades do motor. Mensagens contêm identificador de requisição, identificador de análise, operação e resultado ou erro estruturado. O tamanho máximo inicial de mensagem é 8 MiB, com paginação para resultados maiores. Logs do motor usam stdout/stderr capturados em arquivo separado e nunca são interpretados como respostas JSON.

Endereços são representados por espaço de endereços e valor hexadecimal em texto. Deslocamentos no arquivo são campos separados. Nenhum cliente deve converter endereços para ponto flutuante.

## 6. Persistência e consistência

Cada projeto tem um manifesto com versão de esquema e identificador; amostras por SHA-256; diretórios de análise com seu projeto Ghidra; banco SQLite; e logs por trabalho. O manifesto usa caminhos relativos internos ao projeto para permitir movê-lo entre pastas.

O Ghidra é a fonte de verdade para a análise semântica e nomes de funções. O SQLite contém índices de navegação, configurações e histórico de trabalhos, observações do usuário, marcadores e cache de pseudocódigo. Observações desta versão são próprias do aplicativo e não fingem ser comentários internos do Ghidra.

O cache identifica amostra, análise, revisão, linguagem, endereço da função e versão do motor. Ao reabrir, o aplicativo verifica a compatibilidade e reconcilia o índice com o programa Ghidra antes de aceitar operações semânticas. Índices e pseudocódigo podem ser reconstruídos sem perder observações.

Um serviço de repositório serializa as escritas no SQLite e aplica migrações transacionais. Somente um processo do aplicativo pode editar o mesmo projeto; uma segunda tentativa informa que ele está em uso. O protocolo de bloqueio deve liberar projetos depois de um encerramento inesperado sem remover o bloqueio de um processo ativo.

## 7. Estados, erros e limites

- Estados explícitos: aguardando configuração, na fila, importando, analisando, pronto, parcial, cancelado e falhou. Um estado parcial informa quais etapas terminaram.
- Ghidra ausente, JDK incompatível ou worker sem inicialização produzem instruções de configuração. A interface pode abrir para configurar dependências e visualizar bytes, sem simular análise ou pseudocódigo.
- Tempo limite inicial de autoanálise: 300 segundos por imagem; descompilação: 30 segundos por função. São configuráveis. Funções que falham não impedem consultar as demais.
- O supervisor aplica também prazo externo de 330 segundos à autoanálise e de 35 segundos à descompilação, ajustado quando os limites forem configurados. Ao vencer o prazo, cancela a operação; o limite de 5 segundos para encerramento forçado continua valendo. Um resultado parcial só aceita novos comandos semânticos após o motor voltar a um ponto estável.
- A inicialização do worker tem limite de 60 segundos. Durante trabalho ativo, o watchdog aceita mensagens de progresso ou heartbeat; 60 segundos sem atividade de supervisão provocam falha controlada do worker.
- Cancelamento é cooperativo primeiro. Após 5 segundos sem término, o supervisor encerra a árvore de processos que iniciou, incluindo JVM e descompilador nativo. No Windows, o worker deve entrar em um Job Object supervisionado antes de iniciar a JVM, para que o encerramento inclua os descendentes. Projetos interrompidos precisam ser reabertos e verificados pelo motor antes de reutilização.
- Paginação inicial de 200 registros, resultados de busca limitados por página e leitura hexadecimal de até 64 KiB por solicitação mantêm o uso de memória independente do tamanho total das listas.
- Os valores acima são limites iniciais de operação, não promessas de tempo de análise nem de suporte a qualquer tamanho de arquivo.
- Nenhuma operação desta versão executa o binário analisado. Separação de processos serve à supervisão e responsividade; não é apresentada como sandbox de segurança.

## 8. Ambiente e distribuição inicial

O ambiente inspecionado contém Python 3.13.14 e Java 8 ativo. A referência de integração é Ghidra 12.1.4, que requer JDK 21 de 64 bits e aceita Python 3.9–3.14 para PyGhidra. Portanto, o Python existente é compatível; o JDK ativo não atende ao motor escolhido.

As dependências Python serão isoladas em ambiente virtual do projeto. A configuração permite indicar caminhos do Ghidra e do JDK sem alterar o Java padrão do computador. O primeiro pacote detecta uma instalação local do Ghidra; não faz download silencioso nem inclui o motor sem preparar seu empacotamento.

O primeiro marco inclui uma forma documentada de executar o aplicativo no Windows e diagnosticar dependências. Um instalador único com runtimes integrados é um marco posterior.

## 9. Verificação e critérios de aceite

Os testes precisam exercitar resultados observáveis e falhas reais, não apenas repetir a implementação.

1. Importar amostras PE x86 e x86-64 produzidas a partir de código conhecido e consultar metadados, funções, assembly, strings, imports, referências e pseudocódigo real de pelo menos uma função recuperada.
2. Importar uma amostra ELF AArch64 e uma Mach-O x86-64 reconhecidas, navegar suas funções e exportar resultados com endereços corretos. Registrar como cada fixture foi produzida, seus direitos de distribuição e seu hash.
3. Importar firmware bruto de teste com configuração ARM e MIPS conhecida, incluindo endereços de entrada e contexto necessários. A configuração escolhida é preservada e os bytes/disassembly esperados em endereços conhecidos são verificados. As funções criadas a partir dessas entradas permitem solicitar pseudocódigo real; falhas de funções específicas são explícitas.
4. Renomear uma função, salvar, fechar e reabrir mantém o nome no motor e na navegação. Observações e marcadores também persistem.
5. Selecionar funções rapidamente durante descompilação não apresenta a resposta de uma função como se pertencesse a outra.
6. Cancelar autoanálise e descompilação recupera o controle da aplicação e encerra os processos iniciados para o trabalho quando necessário.
7. Ghidra ausente, JDK incompatível, arquivo truncado, formato desconhecido, timeout e falha do worker produzem estados específicos e recuperáveis.
8. Endereços de 64 bits, endereços em espaços diferentes e regiões sem backing de arquivo não sofrem truncamento ou confusão com offsets.
9. Exportar JSON e texto preserva identificação da amostra, configuração, estados parciais e falhas de descompilação. O arquivo de origem permanece byte a byte intacto.
10. Uma lista sintética extensa comprova que a interface busca páginas de dados; uma amostra grande comprova leitura hexadecimal por intervalos. Não se impõe um benchmark universal antes de medir em uma máquina de referência.

Testes unitários cobrem contratos de IPC, persistência, endereços e estados do supervisor. Testes de integração usam o Ghidra real para os critérios de análise. Testes de interface cobrem navegação, seleção e recuperação de falhas. Testes que exigem motor devem declarar a dependência e não podem ser relatados como aprovados quando apenas ignorados.

## 10. Fora deste marco e evolução

Depuração e execução dinâmica, emulação, alteração dos bytes do binário, comparação entre versões, extensões de terceiros, recuperação especializada de .NET/Java, extração genérica de firmware e colaboração remota ficam para marcos posteriores. O primeiro marco não inclui um motor novo de descompilação nem promete desfazer ofuscação ou compactação arbitrárias.

Os pontos de extensão internos são o contrato do motor, os carregadores expostos por ele, a camada de repositório e os painéis da interface. Não será criado um sistema público de plugins antes de esses contratos serem exercitados pela primeira versão.

## 11. Fontes técnicas

- Release estável consultada: https://github.com/NationalSecurityAgency/ghidra/releases/tag/Ghidra_12.1.4_build
- Requisitos da release: https://github.com/NationalSecurityAgency/ghidra/blob/Ghidra_12.1.4_build/GhidraDocs/GettingStarted.md
- PyGhidra da release: https://github.com/NationalSecurityAgency/ghidra/blob/Ghidra_12.1.4_build/Ghidra/Features/PyGhidra/src/main/py/README.md
- API de descompilação: https://ghidra.re/ghidra_docs/api/ghidra/app/decompiler/DecompInterface.html
- Modelo de dados Qt: https://doc.qt.io/qtforpython-6/PySide6/QtCore/QAbstractItemModel.html

As escolhas de IPC, persistência, limites e divisão de responsabilidades são decisões deste projeto, não garantias fornecidas por essas bibliotecas.
