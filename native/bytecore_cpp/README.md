# bytecore_cpp

Componente próprio em **C++20** para a triagem de bytes da bancada. Inclui uma biblioteca estática reutilizável e um executável que se comunica com a aplicação por JSON.

O código foi escrito sem compilar, testar ou executar, conforme a instrução do usuário. Ainda não existe um executável C++ produzido nesta etapa.

## Responsabilidades

- Ler arquivos como dados binários, em blocos de 64 KiB.
- Calcular entropia de Shannon global com um histograma de 256 posições.
- Extrair sequências ASCII imprimíveis e seus offsets de 64 bits no arquivo.
- Preservar sequências que cruzam blocos e que terminam no fim do arquivo.
- Limitar as prévias a 4.096 caracteres e a quantidade de strings ao parâmetro configurado.
- Manter o histograma de todo o arquivo mesmo após atingir o limite de strings.
- Aceitar cancelamento cooperativo por `std::stop_token` na API da biblioteca.

A memória usada depende dos blocos e dos limites de saída, em vez de carregar o arquivo inteiro. A quantidade solicitada de strings ainda determina quanta memória pode ser necessária. Entropia alta é um dado estatístico e não classifica o conteúdo automaticamente.

## API C++

O contrato público está em `include/reworkbench/bytecore.hpp`:

```cpp
reworkbench::bytecore::ScanResult scan_file(
    const std::filesystem::path& path,
    const reworkbench::bytecore::ScanOptions& options = {},
    std::stop_token stop = {}
);
```

O alvo CMake `reworkbench_bytecore_cpp` pode ser vinculado por outros componentes nativos. `ScanCancelled` distingue uma interrupção cooperativa. Falhas de entrada e parâmetros são sinalizadas por exceções; a CLI converte essas falhas em erro JSON e código de saída diferente de zero.

## CLI e protocolo

```text
bytecore_cpp <file> [--min-length N] [--max-strings N]
```

Os padrões são comprimento mínimo 4 e até 10.000 strings. O mínimo precisa ser positivo e a quantidade máxima pode ser zero. Os números são decimais sem sinal. Opções desconhecidas, repetidas, incompletas e valores fora da faixa são recusados.

Na saída padrão, o processo publica um objeto JSON ao concluir a leitura:

```json
{"size":4,"entropy":0.0,"strings":[{"offset":0,"value":"aaaa"}],"strings_truncated":false}
```

- `size` conta os bytes efetivamente lidos.
- `entropy` usa bits por byte, no intervalo de 0 a 8; arquivo vazio resulta em 0.
- `strings` contém offsets de arquivo iniciados em zero e sequências de bytes de `0x20` até `0x7e`, incluindo espaço.
- `strings_truncated` indica sequências qualificadas omitidas ou prévias encurtadas. O comprimento completo da sequência determina se ela se qualifica.

O erro usa somente a saída de erro: `{"error":"mensagem"}`. A serialização numérica usa o locale clássico e faz escape das strings JSON. O executável aceita caminhos Unicode via `wmain` e `std::filesystem::path` no Windows. O arquivo de entrada é aberto somente para leitura e nunca é executado.

## Integração com o aplicativo

`Session.scan_sample` procura os executáveis existentes nesta ordem:

1. `native/bytecore_cpp/build/Release/bytecore_cpp.exe` — geradores com múltiplas configurações, como Visual Studio.
2. `native/bytecore_cpp/build/bytecore_cpp.exe` — geradores com uma configuração, como Ninja.
3. `native/bytecore/target/release/bytecore.exe` — alternativa Rust existente.

O primeiro candidato presente é passado ao adaptador `scan_bytes`. O adaptador valida o JSON, usa argumentos sem shell, rejeita usar a própria amostra como executável, aceita somente `.exe` no Windows e verifica cancelamento a cada espera curta. Cancelar ou exceder 300 segundos interrompe o processo filho.

Se o auxiliar escolhido falhar ou produzir uma resposta inválida, a triagem continua com a implementação Python. Rust é selecionado quando o executável C++ está ausente; não é uma segunda tentativa após uma falha do C++.

Não há compilação, download ou instalação automática ao abrir o aplicativo.

## Preparação posterior

Quando a compilação for autorizada, os requisitos serão CMake 3.20 ou superior e compilador com biblioteca padrão C++20. Exemplos de comandos para uma etapa futura, a partir da raiz do repositório:

```powershell
cmake -S native/bytecore_cpp -B native/bytecore_cpp/build
cmake --build native/bytecore_cpp/build --config Release
```

Para gerador com uma configuração, selecione o tipo durante a configuração:

```powershell
cmake -S native/bytecore_cpp -B native/bytecore_cpp/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build native/bytecore_cpp/build
```

Esses comandos foram apenas documentados, sem execução nesta etapa. A integração e a equivalência dos resultados com Rust e Python permanecem pendentes de validação.
