# Decisões e revisão estática

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
| Utilizar Java, C/C++, SLEIGH e Gradle através do Ghidra | Esses componentes pertencem ao motor externo. Não foram criados módulos vazios para repetir cada linguagem na bancada. SQL usa SQLite e as fixtures incluem C e assembly. |
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
