# REWorkbench

Bancada desktop de engenharia reversa para Windows de 64 bits. A interface usa Qt e coordena o Ghidra em um processo separado para importar binários, analisar funções e gerar pseudocódigo C aproximado.

O código da primeira versão está implementado. **As alterações mais recentes não foram executadas, testadas nem compiladas, conforme solicitado.** O registro de validação anterior está em [docs/validation.md](docs/validation.md).

## O que está implementado

- Projetos locais com cópia da amostra, SHA-256, banco SQLite, notas e marcadores por endereço.
- Importação de PE, ELF e Mach-O, escolha de imagem em Mach-O universal e configuração explícita de firmware bruto.
- Catálogo de linguagens e compiladores do Ghidra, base de memória, entradas e contexto de registradores, incluindo `TMode=1` para Thumb.
- Exploradores paginados de funções, símbolos, strings definidas, imports e regiões de memória.
- Assembly, pseudocódigo, referências, histórico de navegação e bytes do arquivo com mapeamento de offsets.
- Renomeação persistente de funções, cache por revisão e exportação de relatório JSON e pseudocódigo da função selecionada.
- Pistas de auditoria baseadas em imports `gets`, `strcpy`, `strcat` e `sprintf`, com referências concretas quando disponíveis.
- Triagem de bytes com entropia global e strings ASCII, usando Python ou um auxiliar opcional em Rust.
- Operações do motor em fila, limites de tempo, cancelamento, heartbeat e encerramento da árvore de processos no Windows.

## Iniciar quando desejar executar

Requisitos: Windows x64, Python 3.13 x64, Ghidra **12.1.4** e JDK **21 x64**. O motor requer o pacote PyGhidra **3.1.0** correspondente a essa distribuição do Ghidra.

Caso o ambiente Python ainda não exista:

```powershell
.\scripts\setup.ps1
```

Para abrir o aplicativo:

```powershell
.\scripts\run.ps1
```

O script também reconhece o ambiente já preparado em `.worktrees/implementation/.venv`. Não baixa o Ghidra nem compila componentes. A interface inicia sem carregar a JVM; o processo do motor é criado quando necessário.

No aplicativo, abra **Análise → Configurações do motor** e informe as pastas do Ghidra e do JDK. A configuração local existente em `.tools/environment.json` é descoberta automaticamente. Também são aceitas as variáveis `REWORKBENCH_GHIDRA_HOME` e `REWORKBENCH_JAVA_HOME`.

O diagnóstico é uma ação separada e somente será executado se você o solicitar pelo botão da interface ou pelo script:

```powershell
.\scripts\doctor.ps1
```

## Fluxo de trabalho

1. Crie um projeto em uma pasta vazia ou abra uma pasta que contenha `project.json`.
2. Importe um executável, biblioteca ou firmware. Escolha o formato e a arquitetura apresentados pelo Ghidra. Em um Mach-O universal, selecione uma imagem.
3. Para firmware bruto, marque `BinaryLoader`, selecione uma linguagem, defina a base e forneça ao menos uma entrada. Exemplo: base `0x8000`, entrada `ram:0x8000`; para Thumb, `ram:0x8000 TMode=1`.
4. Abra uma função com clique duplo. Use as referências para navegar entre origem e destino, e o assembly para localizar os bytes correspondentes.
5. Registre notas e marcadores no painel Investigação. As notas são gravadas automaticamente após a edição e também pelo botão Salvar nota.
6. Exporte os resultados no menu Arquivo. Escolha uma pasta externa ao projeto ou sua subpasta `exports`.

**Examinar bytes** funciona sem Ghidra configurado. A triagem ASCII é independente das strings definidas pela análise do motor. Entropia alta pode ter várias causas e não constitui uma classificação automática de conteúdo.

## Organização técnica

| Tecnologia | Papel |
|---|---|
| Python / PySide6 | Interface desktop, coordenação, protocolo, exportação e alternativa para triagem de bytes |
| Java / PyGhidra | Importadores, modelo do programa e autoanálise do Ghidra |
| C / C++ | Componentes nativos de decompilação fornecidos pelo Ghidra e fontes das amostras de desenvolvimento |
| SLEIGH | Especificações de processadores e semântica das instruções fornecidas pelo Ghidra |
| Rust | Código do auxiliar opcional `native/bytecore`; nenhum executável gerado nesta etapa |
| Assembly | Fontes próprias das amostras ARM, Thumb e MIPS em `tests/fixtures` |
| SQL / SQLite | Índice dos registros, anotações, revisões e cache local |
| Gradle | Build do projeto Ghidra original; esta bancada utiliza uma distribuição pronta |

O produto não precisa reconstruir o Ghidra com Gradle. Os componentes Java, C/C++ e as especificações SLEIGH pertencem à distribuição do motor e não estão reimplementados ou incluídos neste repositório.

```text
src/reworkbench/
  application/  sessão, auditoria, exportação e triagem
  domain.py     endereços estruturados, opções e mensagens
  engine/       supervisor, worker e integração Ghidra
  project/      amostras, bloqueio do projeto e SQLite
  ui/           janela Qt, exploradores, código, hex e notas
native/bytecore/ auxiliar Rust opcional, ainda sem compilar
scripts/        instalação, inicialização e diagnóstico
docs/           arquitetura, plano e estado de validação
```

## Limites desta versão

Decompilação não recupera integralmente o código original. Comentários, nomes e tipos podem ter sido removidos; otimizações e ofuscação podem alterar bastante a representação. As pistas de auditoria exigem investigação manual.

Esta versão concentra-se em análise estática. Não implementa depurador, execução de amostras, desempacotamento genérico, emulação, recuperação automática de projetos originais, colaboração remota ou instalação de plugins. O leitor inicial de contêiner Mach-O universal cobre o cabeçalho FAT32 big-endian; FAT64 e cabeçalhos FAT com endianness invertido ainda não têm seleção de imagem implementada.

A persistência semântica é feita no projeto Ghidra, enquanto SQLite mantém o índice e as anotações. Em caso de interrupção do motor, a reabertura reconcilia o índice com o programa salvo. Arquivos em regiões sem correspondência na amostra, como memória não inicializada, não recebem um offset artificial.

Veja [a arquitetura aprovada](docs/superpowers/specs/2026-10-14-reverse-engineering-workbench-design.md) e a documentação do [auxiliar Rust](native/bytecore/README.md). Ghidra é um projeto externo distribuído sob sua própria licença: [repositório oficial](https://github.com/NationalSecurityAgency/ghidra).
