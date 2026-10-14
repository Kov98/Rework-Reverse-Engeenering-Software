# Automação e integração nativas 0.3

O target CMake `reworkbench-cli` usa Qt Core/Sql e a mesma biblioteca do desktop. Os comandos abaixo documentam uso futuro; **nenhum foi executado nesta etapa**, e nenhum executável foi produzido. A compilação continua suspensa por orientação do usuário.

## Comandos

```powershell
build/bin/reworkbench-cli.exe capabilities
build/bin/reworkbench-cli.exe inspect --input C:/amostras/programa.exe
build/bin/reworkbench-cli.exe analyze --project C:/investigacoes/novo --create-project --input C:/amostras/programa.exe
build/bin/reworkbench-cli.exe functions --project C:/investigacoes/novo --count 200 --offset 0
build/bin/reworkbench-cli.exe xrefs --project C:/investigacoes/novo
build/bin/reworkbench-cli.exe decompile --project C:/investigacoes/novo --function 0x140001000
build/bin/reworkbench-cli.exe decompile --project C:/investigacoes/novo --function 0x140001000 --output C:/investigacoes/novo/exports/funcao.c.txt
build/bin/reworkbench-cli.exe analyze --input C:/amostras/firmware.bin --raw --arch thumb --base 0x8000000 --entry 0x8000100 --endian little
build/bin/reworkbench-cli.exe analyze --project C:/investigacoes/novo --input C:/amostras/programa.exe --symbols C:/simbolos/programa.json --force
build/bin/reworkbench-cli.exe symbols --project C:/investigacoes/novo
build/bin/reworkbench-cli.exe diff --input C:/amostras/antes.exe --other C:/amostras/depois.exe
build/bin/reworkbench-cli.exe patch --project C:/investigacoes/novo --input C:/amostras/programa.exe --output C:/investigacoes/novo/exports/patched.exe --request C:/config/patch.json
build/bin/reworkbench-cli.exe emulate --project C:/investigacoes/novo --function 0x140001000 --request C:/config/emulacao.json
build/bin/reworkbench-cli.exe debug --request C:/config/debug.json
build/bin/reworkbench-cli.exe plugins --input C:/extensoes
build/bin/reworkbench-cli.exe plugin-run --input C:/extensoes/extensao.dll --sha256 HASH_OBTIDO_NA_DESCOBERTA --command describe --allow-plugin
build/bin/reworkbench-cli.exe benchmark --request C:/corpus/corpus.json
```

Cada resposta é um objeto JSON em stdout com `ok`. Exceções de operação retornam `ok:false`, mensagem e status 1. Algumas ferramentas retornam um estado interno parcial/interrompido; `ok:true` significa que o comando retornou seu relatório, não sucesso de uma análise completa ou equivalência semântica. `--request -` lê um objeto JSON de stdin. A entrada é limitada a 4 MiB. Flags de linha de comando têm prioridade sobre campos homônimos do JSON. A CLI não baixa dependências nem símbolos.

`--input` faz análise da amostra; `--project` sem input carrega a análise selecionada por `--analysis UUID`, ou a última. `--create-project` exige uma pasta vazia. `--force` ignora reaproveitamento da análise. Seletores de função aceitam nome exato ou endereço hexadecimal. Endereços e offsets binários são strings hexadecimais de até 64 bits; `--offset` e `--count` em listas são contagens decimais. Uma página tem até 2000 linhas, com padrão 200. `--slice` é índice decimal explícito da imagem universal.

`--output` para relatório/pseudocódigo/IR exige projeto e passa pela proteção de exportação. Resultados sem projeto continuam em stdout, que o chamador pode consumir. O patch usa seu próprio exportador atômico e recebe as origens protegidas do projeto quando `--project` é informado. Os contratos de patch, emulação, depuração e extensão estão em [investigation.md](investigation.md). Para `emulate`, o JSON de entrada tem o estado no objeto `initial`. Comparar funções usa `diff-functions`, a função selecionada e campos `other_analysis` e `other_function` no JSON.

## Desktop

A seleção da função prepara pseudocódigo e SSA em segundo plano, com tokens de seleção e cancelamento. As abas Assembly, IR, CFG e bytes continuam disponíveis. O menu Análise importa mapas de símbolos, mostra metadados e inicia emulação com estado explícito; Ferramentas oferece comparação, patch em cópia, depurador e inspeção de extensões. A invocação de extensões é disponibilizada pela CLI/API, com consentimento explícito por chamada; a interface apenas inspeciona metadados.

O depurador tem thread dedicada para manter as chamadas Win32 na thread que criou a sessão. A interface aguarda eventos em intervalos curtos, oferece passo, continuação, breakpoints de uso único e leitura de registradores/bytes. Fechar tenta desconectar a sessão. Sessões Windows x86 e x64 exigem um build da mesma arquitetura do alvo. O ciclo de vida ainda precisa de verificação de execução, inclusive durante falhas de desconexão e fechamento do aplicativo.

Mapas são copiados por hash para o projeto antes da nova análise. A análise anterior permanece preservada; notas são herdadas e nomes manuais são aplicados apenas às funções ainda existentes na mesma amostra. A leitura reconstrói o caminho relativo do mapa e confere o hash quando o projeto é movido.

## Cache e trabalho incremental

Uma instância `BinaryFile` mantém quatro páginas de 64 KiB para leituras pequenas, totalizando até 256 KiB; leituras grandes continuam por intervalos. Isso evita uma operação física de leitura por instrução sem mapear a imagem inteira. A cópia analisada deve permanecer imutável; não há monitoramento contínuo de alterações externas.

Análises `ready` podem ser reutilizadas por SHA-256, motor, decoder e opções, incluindo hash do mapa. Análises parciais/canceladas não entram nesse índice. Isso é reaproveitamento de uma análise integral compatível, não reanálise incremental de trechos de código modificados.

Fatos locais e ponteiros de arquivo podem acrescentar candidatos de chamadas/saltos e alimentar a exploração. Eles não substituem o operando indireto por um alvo direto no snapshot: entradas de outros predecessores ou outros estados de memória podem divergir. Essa incerteza permanece visível, e o decompilador/interpreter não assume um destino único. A resolução de uma tabela cobre apenas ponteiros absolutos, até 256 entradas, com comparação e desvio restritos; tabelas PIC relativas e switches gerais permanecem fora do modelo.

O cache de decompilação cobre somente as opções padrão. A chave inclui a versão da semântica, snapshot/função, arquitetura, revisão, nomes e contexto de símbolos/imports. Apenas resultados com fluxo de dados convergente e saída não truncada são gravados. Cache inválido ou indisponível é descartado/recalculado; uma falha de cache não vira pseudocódigo bem-sucedido. Limites por projeto: 16 MiB por registro, 256 MiB e 1024 registros; a remoção considera a data de gravação. Campos opcionais e tabelas derivadas preservam o schema nativo 1 anterior, sem migrar projetos Python/Ghidra.

## Corpus para medição futura

O comando `benchmark` é um harness de produto para uso explícito posterior, não um teste executado nesta entrega. Aceita até 32 amostras, com SHA obrigatório e opções. Cada análise tem até 60 segundos e o corpus tem prazo cooperativo de 10 minutos; cada amostra de decompilação cobre até 32 funções.

```json
{
  "samples": [{
    "path": "C:/corpus/programa.exe",
    "sha256": "HASH_REAL_DE_64_CARACTERES",
    "options": {},
    "expected_functions": ["0x140001000"],
    "reference_complete": false
  }]
}
```

O relatório contém tempo, candidatos, referências rotuladas encontradas/ausentes e cobertura de instruções modeladas. Candidatos extras só são contabilizados quando a referência é declarada completa. Não mede memória, equivalência semântica ou produtividade humana, nem invoca Ghidra automaticamente. Uma comparação séria ainda precisa do mesmo corpus, configuração e rótulos, baseline externo, medições de memória e avaliação de analistas. Não há resultados de benchmark obtidos nesta etapa.
