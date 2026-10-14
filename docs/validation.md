# Estado de implementação e validação

## Versão principal nativa em C++20/Qt

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
