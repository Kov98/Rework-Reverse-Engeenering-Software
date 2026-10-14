# Estado de implementação e validação

Em 14/10/2026 o usuário determinou: **“só coda agora, nao teste e compile nada ainda”**. A partir dessa instrução, o trabalho segue somente com escrita de código e leitura estática. Não foram iniciados novos testes, builds, compilações, instalações de dependências ou execuções do aplicativo.

## Evidências anteriores à instrução

Antes dessa mudança de orientação foram exercitados armazenamento de projetos, configuração do ambiente, protocolo, supervisão e coordenação da sessão. A matriz de integração real com Ghidra terminou com **11 casos aprovados e 2 falhas**. Ela já estava encerrada quando sua saída foi consultada após a instrução.

Uma falha ocorreu ao criar uma junction temporária necessária para um projeto em caminho oculto; o mesmo ELF foi importado em outro caso. A outra foi uma divergência entre a expressão esperada por um teste e o texto de uma mensagem de configuração de firmware bruto. O código de tratamento desses pontos foi alterado posteriormente e **não foi reexecutado**.

As evidências prévias não validam a versão atual inteira. A interface e as alterações posteriores da sessão ainda não receberam validação de execução.

## Código acrescentado sem execução

- Interface Qt, exploradores paginados, configuração de importação, navegação, assembly e pseudocódigo.
- Visualização de bytes por intervalos e triagem de strings e entropia.
- Auxiliar opcional em Rust, com alternativa em Python.
- Notas, marcadores, pistas de auditoria e exportação JSON / pseudocódigo.
- Ajustes de cancelamento, encerramento, proteção de destinos de exportação e revisão de cache.
- Scripts de instalação, inicialização e diagnóstico.

Somente uma revisão estática pode ser realizada nesta etapa. A compilação do auxiliar Rust, os testes automatizados e a verificação manual dos fluxos da interface ficam pendentes até nova orientação do usuário.

A revisão independente identificou nove pontos de integração e de limites de memória. O código foi alterado para tratá-los em uma única rodada, sem reexecução nem nova revisão independente. As decisões e as alterações estão registradas em [implementation-notes.md](implementation-notes.md).

## Verificações pendentes para uma etapa posterior

1. Inicialização da interface com e sem motor configurado, configuração persistente e restauração dos painéis.
2. Importação real da matriz de arquiteturas, incluindo seleção de imagem universal, base e contexto Thumb.
3. Navegação função → assembly → bytes → referências → função, incluindo seleção durante a indexação e resultados atrasados.
4. Renomeação, notas e marcadores após fechar e reabrir, com reconstrução do índice a partir do programa salvo.
5. Cancelamento e fechamento durante início da JVM, autoanálise, indexação, decompilação e triagem; verificar os processos filhos.
6. Exportação íntegra e substituição atômica diante de falhas, endereços de 64 bits, estados parciais e preservação das amostras.
7. Compilação separada do auxiliar Rust e comparação com a alternativa Python para strings que cruzam intervalos de leitura.

Essa lista registra trabalho de validação futuro e não representa verificações já realizadas.
