# Bancada independente em C++ — direção vigente

Data: 2026-10-14. Direção explicitamente determinada pelo usuário: Ghidra é inspiração; o produto e sua arquitetura são próprios. Esta especificação substitui a arquitetura anterior baseada no motor Ghidra. A instrução de apenas escrever código, sem testes, compilação, instalações ou execução, continua vigente.

## Produto

Aplicação desktop própria em C++20 e Qt 6, para compreender executáveis, bibliotecas e firmware, registrar uma investigação e navegar entre funções, instruções, bytes, referências e fluxo de controle. O objetivo de competir com ferramentas maduras orienta a evolução; superioridade de desempenho ou de análise não é declarada sem validação comparativa.

O produto principal não inicia Ghidra, JVM, PyGhidra ou Python. Código do protótipo anterior permanece preservado como histórico técnico, fora do caminho padrão de execução. Projetos antigos não serão reinterpretados silenciosamente como projetos nativos.

## Núcleo independente

- Carregadores próprios PE32/PE32+, ELF32/64, Mach-O e firmware bruto: cabeçalhos, regiões, entradas e metadados, com limites e checagem de intervalos. Imports e símbolos são recuperados quando o formato correspondente fornecer dados suportados.
- Endereços virtuais de 64 bits separados de offsets de arquivo. Regiões não inicializadas não recebem bytes ou offsets inventados. Mach-O universal exige seleção de imagem.
- Decodificação opcional com Capstone 5, biblioteca especializada de instruções; não é um framework de análise substituindo Ghidra. Nenhuma dependência é baixada automaticamente. Sem o decoder, formatos, bytes e strings permanecem disponíveis e a indisponibilidade de assembly é explícita.
- Motor próprio de exploração a partir das entradas, símbolos e chamadas diretas: funções candidatas, instruções, xrefs e blocos de controle, com limites de instruções/funções, cancelamento e resultados parciais.
- Representação intermediária própria por função. Operações reconhecidas recebem semântica explícita; operações ainda não modeladas permanecem opacas, identificadas por instrução e endereço. Não apresentar essa primeira IR como um decompilador completo, recuperação de código original ou prova de equivalência.
- Reutilizar diretamente a biblioteca C++ de entropia e strings, sem iniciar seu executável auxiliar.

## Aplicação

Qt Widgets em C++ para área de trabalho, exploradores paginados, assembly, IR, grafo de controle, visor hex, pesquisa, histórico, notas, marcadores e exportação. Arquivos são processados em tarefas de fundo com `std::stop_token`; modelos e widgets só mudam na thread Qt. Respostas de seleções anteriores são descartadas por token.

Um trabalho principal modifica a sessão por vez. A troca de projeto e os comandos conflitantes permanecem indisponíveis durante esse trabalho. Fechamento solicita cancelamento e aguarda a tarefa concluir antes de liberar o projeto. Operações nativas têm limites explícitos e não executam a amostra.

Projetos próprios usam `project.rwb.json`, cópias por SHA-256 e SQLite. Escrita de índices, nomes e notas usa transações; bloqueio por projeto recusa uma segunda instância. Nomes do usuário e notas permanecem separados dos dados reconstruíveis. JSON usa endereços e offsets grandes em hexadecimal textual. Exportações preservam os arquivos internos e a origem.

## Entrega desta mudança

O caminho padrão de execução e CMake passa para a aplicação C++/Qt independente. O módulo legado Python/Ghidra continua somente em código histórico e com dependências opcionais identificadas como legadas. Esta fase entrega código-fonte e revisão estática, sem afirmar build ou funcionamento validado.

Decompilação com reconstrução de tipos/variáveis, SSA completa, otimizações, estruturação de controle, depuração, emulação, unpacking e paridade industrial de arquiteturas permanecem trabalho de evolução. A IR inicial deve tornar essas lacunas observáveis, sem simular capacidades prontas.
