# Decisões e revisão estática

## Aplicação independente em C++20/Qt

Após o usuário esclarecer que Ghidra é somente inspiração, a aplicação principal foi reescrita em C++20/Qt. A especificação e o plano atuais são os documentos `2026-10-14-independent-native-workbench-*`. O ponto de entrada principal é `native/workbench/src/main.cpp`; `scripts/run.ps1` procura exclusivamente o executável nativo. O ponto de entrada Python é apenas um lançador opcional. `legacy_main.py`, os módulos Python antigos e as dependências opcionais `legacy` preservam o protótipo anterior fora do caminho padrão.

Os contratos públicos ficam em `native/workbench/include/reworkbench`. Os carregadores próprios, a análise/IR e a persistência foram implementados em paralelo contra esses contratos. A interface usa snapshots imutáveis, tarefas em segundo plano e tokens para rejeitar resultados antigos. Renomear cria um novo snapshot; notas pendentes são gravadas antes da troca de análise, exportação e fechamento, e uma falha mantém os rascunhos.

Capstone 5 é uma biblioteca opcional de decodificação de instruções; não descobre as funções nem produz a IR. Sem ela continuam disponíveis metadados, bytes, strings e entropia, com aviso explícito da capacidade ausente. A representação intermediária é parcial, sem SSA, recuperação de tipos ou decompilação estruturada completa. Efeitos não modelados permanecem opacos. MIPS com transferências de controle não recebe um CFG que ignore seus delay slots.

Projetos nativos usam `project.rwb.json`, trava de projeto, amostras copiadas por SHA-256 e SQLite. Os caminhos das amostras são relativos no índice e reconstruídos na abertura para permitir mover o projeto. Carregar uma análise recalcula SHA-256 da cópia selecionada; salvar novos resultados e exportar IR também verificam o conteúdo. A abertura faz uma checagem estrutural das demais cópias. Exportações usam gravação atômica e protegem amostras, origens e arquivos internos. O produto não monitora continuamente alterações feitas por outros processos após uma leitura.

A revisão independente do código nativo identificou três pontos importantes: integridade de cópias alteradas mantendo o tamanho, sementes ARM/Thumb de modo incompatível e textos SQLite alocados antes dos limites. O código foi alterado para recalcular hashes, preservar símbolos incompatíveis sem explorá-los e consultar comprimentos/tipos antes de materializar notas e nomes. Escritas também respeitam os limites agregados de texto. Leituras de tabelas e bytes limpam a seleção anterior imediatamente; pistas são preparadas em segundo plano com uma passagem pelas referências e até 200 registros, e a lista de marcadores mostra até 200 entradas. Uma leitura independente posterior das correções não identificou novos problemas relevantes. Isso é evidência de revisão de fonte, sem validação de compilação ou execução.

Esta etapa respeitou a suspensão de testes, compilação, configuração, execução e instalação. A documentação de build é uma instrução futura, não uma evidência de build realizado. A revisão por leitura não substitui a aceitação pendente em [validation.md](validation.md).

## Histórico do protótipo Python/Ghidra

As decisões e revisões abaixo pertencem à versão anterior e não descrevem o caminho padrão atual.

O desenvolvimento está na branch `feat/reworkbench`, no diretório `.worktrees/implementation`. Não houve publicação nem integração automática na branch principal. O ponto de entrada é `src/reworkbench/__main__.py`; o lançador é `scripts/run.ps1`.

## Decisões de implementação

| Decisão | Motivo e limite |
|---|---|
| Manter o identificador `Task N` nos títulos do plano | Compatibilidade com o leitor de tarefas das skills; nenhum efeito no produto. |
| Implementação do núcleo e da interface pelo agente principal | Preserva os contratos compartilhados. Pesquisa e preparação do ambiente ocorreram em paralelo; o auxiliar Rust independente foi delegado posteriormente. |
| Declarar PySide6-Essentials | QtCore, QtGui e QtWidgets atendem à interface prevista. Recursos de outros módulos Qt exigirão dependências adicionais. |
| Usar uma pasta temporária de testes local na fase anterior | O diretório temporário padrão do host recusou acesso. Não altera o comportamento do aplicativo. |
| Preparar fixtures e supervisão em paralelo na fase anterior | Eram tarefas independentes após os contratos e a persistência. Nenhuma fixture foi compilada após a instrução de apenas codificar. |
| Usar Clang/LLD fornecidos pelo Zig para as fixtures próprias | Substituiu uma preparação de LLVM que estava parada. A extração de `.text` nas fixtures raw não é um importador do produto; o suporte a relocação permanece no Ghidra. |
| Criar junction temporária para caminhos ocultos | Ghidra rejeita componentes iniciados por ponto em caminhos de projeto. Os dados permanecem na pasta portátil; somente o alias é removido. Essa adaptação depende do Windows e do PowerShell. |
| Retornar total desconhecido `-1` durante streaming do worker | Evita recontar toda a análise para cada página. O índice SQLite fornece os totais usados na interface. |
| Suspender testes, compilações e execução | Instrução explícita do usuário em 14/10/2026. O código atual não recebe declaração de aprovação dinâmica. |
| Auxiliar Rust opcional, com alternativa Python | Permite acrescentar processamento nativo sem exigir compilação para usar o restante da bancada. O desempenho do auxiliar permanece não medido. |
| Utilizar Java, SLEIGH e Gradle através do Ghidra | Esses componentes pertencem ao motor externo, assim como o decompilador C/C++ do Ghidra. Após exigência explícita do usuário, a bancada ganhou também o núcleo próprio C++20 descrito abaixo. SQL usa SQLite e as fixtures incluem C e assembly. |
| Exibir a origem no diálogo e devolver o destino das exportações | O diálogo recebe o caminho da amostra; as exportações devolvem um `Path` para a confirmação da interface. Consumidores e plano foram ajustados. |
| Limitar prévias de strings e stubs | Strings definidas têm 4.096 caracteres por prévia e imports até 128 endereços auxiliares, com flags de truncamento. Xrefs permanecem indexados; o visor permite acessar os bytes. |
| Limitar cada tabela a 1.000 linhas por intervalo | Páginas de 200 registros evitam carregar a lista inteira. Anterior / Próximos preservam acesso aos resultados restantes. |

## Revisão independente

Uma revisão independente e somente de leitura foi realizada no commit `f27f32e`. Não houve execução de testes, builds, diagnósticos ou módulos do aplicativo. Nenhum problema crítico foi confirmado; seis problemas foram classificados como importantes e três como menores pelo revisor.

Os três últimos foram tratados como importantes na implementação: bytes antigos podem induzir uma investigação sobre a amostra errada; a escolha silenciosa de slice contraria a configuração explícita; e o acúmulo de linhas pode esgotar memória em análises grandes.

| Problema identificado | Alteração escrita |
|---|---|
| Nota pendente perdida na troca de projeto ou omitida da exportação | Fila própria de gravação e barreiras antes de trocar projeto, salvar, exportar e fechar. Falhas de gravação mantêm a possibilidade de nova tentativa. |
| Origem desprotegida após falha na importação | Persistir o caminho original antes da primeira chamada ao motor. |
| Indexação interrompida considerada reconciliada | Estado separado de reconciliação pendente, removido somente após todos os tipos de registro serem concluídos. |
| Erros transitórios presos no cache | Cache armazena somente pseudocódigo bem-sucedido e ignora erros antigos. |
| Assembly e referências vazios após seleção durante indexação | Snapshots atualizam também os modelos da seleção atual no visor de código. |
| Uma falha de transporte descartava toda a exportação de funções | Registrar erro por função, descartar o worker que falhou e permitir reabertura para a função seguinte. Cancelamento explícito continua interrompendo a exportação atomicamente. |
| Bytes do projeto anterior permaneciam visíveis | Limpeza do visor e invalidação do token das leituras antigas. |
| Primeira imagem universal escolhida automaticamente | Opção inicial sem slice e validação de escolha explícita. |
| Tabelas retinham todas as páginas percorridas | Intervalos navegáveis com limite de memória por tabela. |

Essas alterações receberam leitura estática pelo autor e não foram executadas, testadas, compiladas ou submetidas a uma segunda revisão independente. Não há itens menores deliberadamente adiados dessa revisão; os nove pontos entraram na mesma rodada de alterações do código.

## Limites de julgamento da revisão

- A aprovação dos testes pendentes e a verificação atual da interface foram suspensas por instrução do usuário; continuam pendentes.
- A compatibilidade efetiva de cada chamada Java/PyGhidra e de toda a matriz atual depende de verificação posterior. A evidência anterior de 11 casos aprovados não valida as alterações recentes.
- A detecção de toda configuração ARM/Thumb semanticamente incorreta está fora da garantia prevista. O programa valida opções estruturais e as entradas, mas o analista continua responsável pela escolha da arquitetura e contexto.
- Desempenho real por tamanho de binário não foi medido. Os limites de páginas, prévias, mensagens e intervalos de leitura foram escritos para controlar o consumo, mas precisam de avaliação dinâmica posterior.

## Inclusão de C++ próprio

Em 14/10/2026, após esclarecer que o C++ anterior pertencia ao Ghidra, o usuário determinou: **“tem que ter c++.”** Essa instrução foi aplicada ao fluxo de triagem existente, preservando a orientação de não testar, compilar ou executar.

Foi criado `native/bytecore_cpp`, com C++20, CMake, biblioteca estática reutilizável e CLI JSON. O módulo calcula entropia e extrai strings ASCII e offsets de 64 bits sem carregar a amostra inteira. O contrato de saída permanece compatível com `scan_bytes`; não houve mudança no protocolo de análise semântica do Ghidra.

O aplicativo passa a procurar primeiro o executável C++ nos caminhos documentados do build. Quando ele está ausente, pode usar o auxiliar Rust; se não houver auxiliar ou o escolhido falhar, utiliza Python. Não há compilação automática. A biblioteca C++ oferece cancelamento cooperativo por `std::stop_token`, e a integração de processo mantém o cancelamento e o limite de tempo existentes no adaptador Python.

A mudança torna C++ parte do código próprio e do processamento nativo da bancada. A interface permanece em Python/Qt; essa instrução não foi tratada como autorização para substituir toda a interface ou reimplementar o decompilador do Ghidra. O limite prático e a correção dinâmica do novo núcleo permanecem pendentes de validação.

Uma revisão independente e apenas estática do novo módulo, CMake, seleção do executável, protocolo e documentação não identificou problemas críticos ou importantes. Não foram executados compilador, CMake, testes, código do produto ou instalações. O executável continua ausente nesta entrega de código-fonte.
