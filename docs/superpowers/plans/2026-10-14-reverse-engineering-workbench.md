# Reverse Engineering Workbench Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Entregar uma bancada desktop Windows que importe programas e firmware, apresente análise e pseudocódigo reais, preserve investigações e exporte evidências.

**Architecture:** Interface Qt/PySide6 e serviços Python coordenam um worker PyGhidra persistente em processo separado. SQLite mantém índices e observações; Ghidra mantém a análise semântica. Um canal local autenticado transporta JSON paginado, e um supervisor controla cancelamento, prazos e encerramento da árvore de processos.

**Tech Stack:** CPython 3.13 x64, PySide6, SQLite, Ghidra 12.1.4/JDK 21 x64, PyGhidra fornecido pela instalação, pytest e pytest-qt; LLVM somente para gerar fixtures de teste.

**Spec:** `docs/superpowers/specs/2026-10-14-reverse-engineering-workbench-design.md`, aprovada em 2026-10-14.

## Global Constraints

- A primeira distribuição será para Windows x86-64.
- A JVM nunca é iniciada dentro do processo da interface.
- Um worker atende uma análise por vez; a fila inicial executa um trabalho de análise por vez.
- Tempo limite inicial de autoanálise: 300 segundos por imagem; descompilação: 30 segundos por função.
- O supervisor aplica também prazo externo de 330 segundos à autoanálise e de 35 segundos à descompilação, ajustado quando os limites forem configurados.
- A inicialização do worker tem limite de 60 segundos; heartbeat a cada 5 segundos e falha após 60 segundos sem atividade durante trabalho ativo.
- Cancelamento é cooperativo primeiro. Após 5 segundos sem término, o supervisor encerra a árvore de processos que iniciou, incluindo JVM e descompilador nativo.
- Paginação inicial de 200 registros; leitura hexadecimal de até 64 KiB por solicitação; mensagem IPC de no máximo 8 MiB.
- Endereços são representados por espaço de endereços e valor hexadecimal em texto. Deslocamentos no arquivo são campos separados.
- Os modelos visuais só são alterados na thread da interface por sinais enfileirados.
- Nenhuma operação desta versão executa o binário analisado.
- Pseudocódigo é uma aproximação semelhante a C; a interface e a exportação não o apresentam como fonte original.
- Dependências ficam em `.venv`; runtimes locais em `.tools`, ambos ignorados pelo Git. Downloads de desenvolvimento vêm dos publicadores oficiais e têm origem e checksum registrados. O aplicativo não baixa runtimes silenciosamente.
- O usuário pode configurar Ghidra/JDK por caminho; não alterar Java global. Se faltar dependência, a interface abre para configuração e leitura de bytes, sem resultados simulados.
- Não implementar recursos fora do marco definido na especificação. Uma única aplicação compartilha projeto, supervisor e navegação; as tarefas abaixo são entregas desse fluxo integrado.

## Review Focus

- Caminhos com espaços/acentos e origem alterada durante importação: preservar argumentos, detectar mudança e não publicar cópia incompleta; teste na tarefa 1.
- Encerramento entre gravação semântica e atualização do índice: reabrir reconciliando pelo Ghidra sem perder observações; testes nas tarefas 4 e 5.
- Resultado atrasado após trocar função ou projeto: associar análise/requisição/revisão antes de atualizar a tela; testes nas tarefas 5 e 7.
- Firmware sem entrada ou com configuração ARM/Thumb incompatível com o catálogo: não inventar funções nem aceitar opções inválidas; permitir nova análise com configuração corrigida, sem prometer detectar toda escolha semanticamente incorreta; testes nas tarefas 4 e 7.
- Projeto já aberto ou migrado por versão posterior: recusar escrita concorrente/incompatível e preservar os dados; teste na tarefa 1.

## Organização dos arquivos

| Caminhos | Responsabilidade |
| --- | --- |
| `src/reworkbench/domain.py`, `project/{repository,storage,bytes}.py` | Tipos, persistência, cópia verificada e leitura por intervalos |
| `src/reworkbench/engine/{config,protocol,supervisor,winjob,worker}.py` | Diagnóstico, IPC e ciclo de processos |
| `src/reworkbench/engine/{backend,loaders,records,decompiler}.py` | Adaptador das APIs Ghidra |
| `src/reworkbench/application/{session,audit,exports}.py` | Coordenação da investigação, triagem e exportação |
| `src/reworkbench/ui/{window,models,settings,import_dialog,code_view,hex_view,investigation}.py` | Interface, modelos paginados e navegação |
| `src/reworkbench/{__init__,__main__}.py`, `scripts/{run,doctor}.ps1` | Inicialização e diagnóstico no Windows |
| `tools/build_fixtures.py`, `tests/fixtures/{src,manifest.json}` | Fixtures próprias, receitas, endereços e hashes |
| `tests/{unit,integration,ui}`, `tests/conftest.py` | Verificação por camada e fluxo completo |

Criar `__init__.py` nos pacotes necessários. O título visível inicial será “Bancada de engenharia reversa”; `reworkbench` é somente o identificador interno do pacote.

## Contratos compartilhados

Definir estes contratos na tarefa 1; importar os mesmos tipos em todas as tarefas. `JsonObject` é um objeto JSON com valores recursivos, sem objetos Java, `Path`, bytes ou floats para endereços.

- `Address(space: str, value: str)`: hexadecimal normalizado, validado com inteiros Python; `to_json() -> JsonObject` e `from_json(data: JsonObject) -> Address`.
- `EntryPoint(address: Address, context: dict[str, str])`: contexto de registradores, incluindo ARM/Thumb quando aplicável.
- `ImportOptions(loader_id: str | None, load_spec_id: str | None, language_id: str | None, compiler_id: str | None, image_base: str | None, entries: tuple[EntryPoint, ...], analysis_timeout_s: int = 300, decompile_timeout_s: int = 30)`.
- `EngineConfig(ghidra_home: Path | None, java_home: Path | None)`; `Sample(sha256: str, size: int, relative_path: str)`; `Analysis(id: str, sample_sha256: str, options: ImportOptions, state: str, revision: int)`.
- `Page(items: tuple[JsonObject, ...], offset: int, total: int)`; `DecompileResult(address: Address, code: str | None, error: str | None, revision: int)`.
- `RecordKind = Literal['functions', 'symbols', 'strings', 'imports', 'regions', 'xrefs', 'assembly']`. Toda linha tem `key`; funções têm `address,name,size`; símbolos `address,name,external`; strings `address,value`; imports `name,library,address`; regiões `start,end,permissions,file_offset`; xrefs `source,target,type,source_function`; assembly `address,bytes,mnemonic,operands,function_address`. Endereços opcionais e offsets inexistentes usam `null`.
- `Envelope(version: int, request_id: str, analysis_id: str | None, operation: str, payload: JsonObject, error: JsonObject | None)`. Protocolo inicial `version=1`; erro contém `code,message,details`; operações: `hello,catalog,inspect,open,analyze,page,decompile,rename,save,close,cancel,heartbeat`.
- `Cancellation`: `cancel() -> None`, `cancelled() -> bool`, `bind(cancel_callback: Callable[[], None]) -> None`. Permite que o receptor cancele o monitor Java ativo sem executar outras operações semânticas.

### Task 1: Projeto persistente, contratos e leitura de amostras

**Arquivos:** criar `pyproject.toml`, `.gitignore`, `src/reworkbench/domain.py`, `src/reworkbench/project/repository.py`, `src/reworkbench/project/storage.py`, `src/reworkbench/project/bytes.py`, `tests/unit/test_project.py`, `tests/unit/test_bytes.py` e `tests/conftest.py`.

**Interfaces:** `ProjectRepository.create(root: Path) -> ProjectRepository`, `open(root: Path) -> ProjectRepository`, `close() -> None`, `import_sample(source: Path, cancel: Cancellation) -> Sample`, `create_analysis(sample: Sample, options: ImportOptions) -> Analysis`, `get_analysis(analysis_id: str) -> Analysis`, `analyses() -> tuple[Analysis, ...]`, `set_metadata(analysis_id: str, metadata: JsonObject) -> None`, `get_metadata(analysis_id: str) -> JsonObject`, `set_state(analysis_id: str, state: str) -> None`, `replace_records(analysis_id: str, kind: RecordKind, pages: Iterable[Page]) -> None`, `page(analysis_id: str, kind: RecordKind, offset: int = 0, limit: int = 200, query: str = '') -> Page`, `set_note(analysis_id: str, address: Address, text: str, bookmarked: bool) -> None`, `notes(analysis_id: str) -> tuple[JsonObject, ...]`, `set_revision(analysis_id: str, revision: int) -> None`, `get_cache(analysis_id: str, address: Address, revision: int) -> DecompileResult | None`, `put_cache(analysis_id: str, result: DecompileResult) -> None`; `read_range(path: Path, offset: int, length: int = 65536) -> bytes`.

- [ ] Escrever testes com arquivos temporários: `test_copy_is_atomic_and_source_unchanged`, `test_unicode_path_and_changed_source`, `test_same_content_is_deduplicated`, `test_cancel_removes_only_partial_copy`, `test_project_lock_and_future_schema`, `test_notes_survive_reopen`, `test_migration_rolls_back`, `test_addresses_do_not_collide`, `test_literal_search_and_pagination`, `test_hex_reads_only_requested_range`. Asserções centrais:
  ```python
  assert read_range(sample_path, 2, 3) == b'cde'
  assert Address.from_json(Address('ram', '0xfffffffffffffff0').to_json()).value == '0xfffffffffffffff0'
  assert repo.page(analysis.id, 'functions', offset=200).offset == 200
  ```
- [ ] Executar `.venv/Scripts/python -m pytest tests/unit/test_project.py tests/unit/test_bytes.py -q`; antes da implementação, confirmar falha pelos módulos/contratos ausentes. Criar `.venv` e instalar pacote editável com extra `dev` para esses testes; `PySide6>=6.9,<7`, `pytest>=8.3,<9`, `pytest-qt>=4.4,<5`; motor em extra separado usando o wheel correspondente da instalação Ghidra. Registrar versões resolvidas em `requirements-dev.lock`.
- [ ] Implementar contratos, manifesto versão 1, tabelas `samples,analyses,records,notes,decompile_cache`, migrações transacionais, caminhos relativos e bloqueio de projeto pelo sistema operacional. Persistir registros JSON com chaves únicas e índices por análise/tipo; consultas literais e ordenação estável. Copiar em blocos para arquivo temporário, verificar origem antes/depois e publicar por substituição atômica; recusar symlinks de destino que escapem do projeto. Serializar acesso SQLite em um executor próprio; permitir leitura hexadecimal sem Ghidra.
- [ ] Reexecutar os dois arquivos de teste; exigir aprovação dos casos de persistência, bloqueio, busca e leitura por intervalos.
- [ ] Commit: `git add pyproject.toml .gitignore requirements-dev.lock src/reworkbench tests` e `git commit -m "feat: add project persistence and binary reader"`.

### Task 2: Ambiente verificável e fixtures próprias

**Arquivos:** criar `src/reworkbench/engine/config.py`, `tools/build_fixtures.py`, `tests/fixtures/src/fixture.c`, `tests/fixtures/src/audit_api.c`, `tests/fixtures/src/arm.s`, `tests/fixtures/src/thumb.s`, `tests/fixtures/src/mips.s`, `tests/fixtures/manifest.json`, `tests/unit/test_environment.py` e `tests/integration/test_fixture_manifest.py`.

**Interfaces:** `validate_engine(config: EngineConfig) -> JsonObject` retorna `ready,issues,versions`; `build_fixtures(output: Path, llvm_bin: Path) -> JsonObject` produz o manifesto, sem executar os binários. Consumir `EngineConfig` da tarefa 1.

- [ ] Escrever `test_java8_is_rejected`, `test_missing_engine_has_actionable_issue`, `test_paths_are_not_shell_commands`, `test_manifest_matches_headers_and_hashes`. Exigir `validate_engine(EngineConfig(None, None))['ready'] is False`; validar hashes e magic bytes PE/ELF/Mach-O, entradas raw `0x1000` e proveniência de cada fixture.
  ```python
  assert validate_engine(EngineConfig(None, None))['ready'] is False
  assert hashlib.sha256(fixture_path.read_bytes()).hexdigest() == fixture_meta['sha256']
  ```
- [ ] Executar `.venv/Scripts/python -m pytest tests/unit/test_environment.py tests/integration/test_fixture_manifest.py -q`; confirmar falha inicial por implementação/fixtures ausentes.
- [ ] Implementar diagnóstico com argumentos de processo em lista e tempo limite, leitura da versão Ghidra e validação do JDK 21 x64. Procurar instalações configuradas; se ausentes, preparar runtimes portáteis de desenvolvimento em `.tools` a partir de releases oficiais com checksum. Verificar ferramentas LLVM e targets antes de gerar fixtures; não assumir que o instalador contém `ld64.lld` e `llvm-objcopy`.
- [ ] Implementar receitas freestanding sem SDK/CRT: Clang/LLD para PE x86/x64, ELF AArch64 e Mach-O x64; assembly/LLD/objcopy para ARM, Thumb e MIPS little-endian. Criar DLL/import library próprias com `strcpy` exportada e PE que a importe, use string literal e chame função aritmética conhecida. Incluir Mach-O universal com slices x64/ARM64 para seleção de imagem. Preservar fontes, comandos exatos, versão LLVM, nomes, entradas, contexto e SHA-256 no manifesto. Ajustar decoração do entry point x86 conforme o símbolo do objeto; nenhuma receita é considerada validada antes de compilar e conferir o resultado.
- [ ] Reexecutar os testes e conferir os formatos com ferramentas LLVM. Se o provisionamento falhar, registrar o bloqueio real; não substituir fixtures por bytes que apenas imitem cabeçalhos.
- [ ] Commit: `git add src/reworkbench/engine/config.py tools tests/fixtures tests/unit/test_environment.py tests/integration/test_fixture_manifest.py` e `git commit -m "test: add reproducible multi architecture fixtures"`.

### Task 3: Transporte autenticado e supervisão de processos

**Arquivos:** criar `src/reworkbench/engine/protocol.py`, `src/reworkbench/engine/supervisor.py`, `src/reworkbench/engine/winjob.py`, `src/reworkbench/engine/worker.py`, `tests/unit/test_protocol.py`, `tests/integration/test_supervisor.py` e `tests/helpers/worker_process.py`.

**Interfaces:** `ProtocolError(ValueError)`, `encode(message: Envelope) -> bytes`, `decode(data: bytes) -> Envelope`; `WorkerSupervisor(config: EngineConfig, on_event: Callable[[Envelope], None])`, `start() -> None`, `submit(operation: str, analysis_id: str | None, payload: JsonObject) -> str`, `cancel(request_id: str) -> None`, `close() -> None`; `WindowsJob.attach(pid: int) -> None`, `terminate() -> None`, `close() -> None`. O worker de teste existe apenas em `tests/helpers`.

- [ ] Escrever `test_bad_auth_and_protocol_version`, `test_oversized_and_truncated_message`, `test_engine_logs_are_not_protocol`, `test_cancel_while_operation_blocks`, `test_heartbeat_does_not_extend_operation_deadline` e `test_windows_job_kills_grandchild`. Verificar rejeição de tamanho `8*1024*1024+1`; usar relógio injetável para prazos de 60/330/35/5 segundos, mantendo teste real de descendente supervisionado.
  ```python
  with pytest.raises(ProtocolError):
      decode(b'{' )
  ```
- [ ] Executar `.venv/Scripts/python -m pytest tests/unit/test_protocol.py tests/integration/test_supervisor.py -q` e confirmar falha inicial.
- [ ] Implementar listener `127.0.0.1`, porta efêmera, autenticação por segredo no ambiente, `send_bytes/recv_bytes`, validação JSON e bloqueio de envios concorrentes. Iniciar worker aguardando comando de boot; anexá-lo ao Job Object antes de autorizar JVM/descendentes. Preservar logs fora do canal. Criar receptor de cancelamento e heartbeat independentes da thread semântica; fechar canais, threads e handles nos caminhos de falha. Prazo externo é timeout configurado +30 segundos para análise e +5 para descompilação; depois dele vale a graça de cancelamento de 5 segundos.
- [ ] Reexecutar testes; confirmar ausência dos processos descendentes após encerramento forçado. Usar chamadas Win32 via `ctypes`, sem nova dependência de runtime.
- [ ] Commit: `git add src/reworkbench/engine tests/unit/test_protocol.py tests/integration/test_supervisor.py tests/helpers` e `git commit -m "feat: supervise isolated analysis workers"`.

### Task 4: Motor Ghidra real e operações semânticas

**Arquivos:** criar `src/reworkbench/engine/backend.py`, `src/reworkbench/engine/loaders.py`, `src/reworkbench/engine/records.py`, `src/reworkbench/engine/decompiler.py`, `tests/integration/test_engine.py`; modificar `src/reworkbench/engine/worker.py`.

**Interfaces:** `GhidraBackend(config: EngineConfig)`, `catalog() -> JsonObject`, `inspect(sample_path: Path) -> JsonObject`, `open(analysis_dir: Path, sample_path: Path, options: ImportOptions, cancel: Cancellation) -> JsonObject`, `analyze(timeout_s: int, cancel: Cancellation) -> JsonObject`, `page(kind: RecordKind, offset: int = 0, limit: int = 200, query: str = '') -> Page`, `decompile(address: Address, timeout_s: int, cancel: Cancellation) -> DecompileResult`, `rename(address: Address, name: str, cancel: Cancellation) -> JsonObject`, `save() -> None`, `close() -> None`. Retornos `catalog` incluem linguagens/compiladores; `inspect` inclui escolhas com IDs estáveis e configuração necessária. Metadados incluem `size,sha256,format,language_id,compiler_id,image_base,entry_points,ghidra_version,program_path,revision`.

- [ ] Escrever testes parametrizados PE32/PE64/ELF/Mach-O/ARM/Thumb/MIPS: `test_real_import_and_pseudocode`, `test_raw_requires_explicit_configuration`, `test_thumb_context_is_preserved`, `test_fat_macho_requires_slice_choice`, `test_rename_survives_new_worker`, `test_unbacked_region_has_no_file_offset`, `test_failure_of_one_function_does_not_poison_next`. Exigir função conhecida, instruções nos endereços do manifesto, pseudocódigo não vazio gerado pelo motor e import/string com xrefs na fixture PE. Não comparar o texto inteiro do decompilador.
  ```python
  result = backend.decompile(Address('ram', '0x1000'), 30, Cancellation())
  assert result.code and result.error is None  # Fixture raw com entrada conhecida.
  ```
- [ ] Executar `.venv/Scripts/python -m pytest tests/integration/test_engine.py -q`; confirmar falha inicial com dependências prontas. Esses testes exigem configuração real; ausência é erro explícito, não um resultado aprovado por skip.
- [ ] Implementar com APIs atuais `pyghidra.start`, `open_project`, `program_loader`, `program_context`, `transaction` e monitores canceláveis. Só importar `ghidra/java/jpype` no worker. Selecionar load spec/slice explicitamente; raw usa `BinaryLoader`, linguagem, base, contexto e entradas configuradas para disassembly/criação de funções. Enumerar registros com endereços por espaço e mapeamentos de bytes reais; representar ausência de mapeamento por `null`.
- [ ] Usar `DecompInterface` persistente, validar `decompileCompleted`, timeout por função e liberação de recursos. Renomear em transação, salvar antes da confirmação e aumentar revisão; reabrir por `program_path` persistido sem reimportar a amostra. Salvar e publicar snapshots somente em pontos estáveis. Ligar os comandos do worker aos métodos, sem acesso semântico concorrente durante autoanálise.
- [ ] Reexecutar a matriz real, incluindo novo processo para reabertura, cancelamento e recuperação após erro; exigir aprovação antes de registrar a entrega.
- [ ] Commit: `git add src/reworkbench/engine tests/integration/test_engine.py` e `git commit -m "feat: integrate Ghidra analysis and decompilation"`.

### Task 5: Sessão da investigação, fila e reconciliação

**Arquivos:** criar `src/reworkbench/application/session.py`, `tests/unit/test_session.py`, `tests/integration/test_session_engine.py`; modificar repositório e worker somente para os contratos abaixo.

**Interfaces:** `Session(config: EngineConfig, emit: Callable[[JsonObject], None])`, `create_project(root: Path) -> Future[None]`, `open_project(root: Path) -> Future[None]`, `catalog() -> Future[JsonObject]`, `list_analyses() -> Future[tuple[Analysis, ...]]`, `inspect_sample(source: Path) -> Future[JsonObject]`, `start_analysis(source: Path, options: ImportOptions) -> Future[str]`, `select_function(analysis_id: str, address: Address) -> str`, `fetch_page(analysis_id: str, kind: RecordKind, offset: int = 0, query: str = '') -> Future[Page]`, `rename_function(analysis_id: str, address: Address, name: str) -> Future[None]`, `set_note(analysis_id: str, address: Address, text: str, bookmarked: bool) -> Future[None]`, `notes(analysis_id: str) -> Future[tuple[JsonObject, ...]]`, `cancel_active() -> None`, `close() -> Future[None]`. `Future` é `concurrent.futures.Future`; eventos JSON usam `kind,analysis_id,request_id,revision,payload`.

- [ ] Escrever `test_only_one_analysis_runs`, `test_semantic_requests_wait_for_stable_snapshot`, `test_stale_function_and_project_response_is_not_displayed`, `test_reopen_rebuilds_index_after_worker_crash`, `test_cancelled_copy_never_starts_engine`, `test_rename_invalidates_cache`, `test_failure_does_not_erase_notes`. Asserções: resposta antiga não emite evento `selection_result` para a seleção atual; reabertura obtém nome do Ghidra e preserva notas SQLite.
  ```python
  shown = [e for e in events if e['kind'] == 'selection_result']
  assert shown[-1]['request_id'] == newest_request_id
  ```
- [ ] Executar `.venv/Scripts/python -m pytest tests/unit/test_session.py tests/integration/test_session_engine.py -q` e confirmar falha inicial.
- [ ] Implementar máquina de estados da especificação, importação/hash/cópia em executor e fila de análise unitária. Persistir versões/configuração e distinguir análise, comando e seleção atual. Reconciliar registros na reabertura e depois de renomear; manter histórico de configurações em análises distintas. Cache exige análise/endereço/revisão/versão/linguagem compatíveis. Expor eventos para a UI por callback sem chamar widgets.
- [ ] Reexecutar testes unitários e integração real; exigir aprovação de reconciliação, revisão e seleção.
- [ ] Commit: `git add src/reworkbench/application/session.py src/reworkbench/project src/reworkbench/engine/worker.py tests/unit/test_session.py tests/integration/test_session_engine.py` e `git commit -m "feat: coordinate persistent investigation sessions"`.

### Task 6: Janela desktop, configuração e exploradores paginados

**Arquivos:** criar `src/reworkbench/ui/window.py`, `src/reworkbench/ui/models.py`, `src/reworkbench/ui/settings.py`, `src/reworkbench/ui/hex_view.py`, `src/reworkbench/__main__.py`, `tests/ui/test_workspace.py` e `tests/ui/test_models.py`.

**Interfaces:** `MainWindow(session: Session)`, `PagedTableModel(fetch: Callable[[int, str], Future[Page]], columns: tuple[str, ...])`, `set_query(text: str) -> None`; `SettingsDialog(config: EngineConfig)`, `selected_config() -> EngineConfig`; `HexView`, `show_file(path: Path, offset: int = 0) -> None`; `main() -> int`. Uma ponte QObject reemite eventos da sessão como sinais enfileirados.

- [ ] Escrever `test_window_opens_without_engine`, `test_configured_paths_survive_restart`, `test_large_model_fetches_200_rows`, `test_hex_window_is_bounded`, `test_model_mutations_stay_on_gui_thread`, `test_active_job_has_progress_and_cancel`. Com um fornecedor de 100.000 linhas, exigir primeira busca com offset 0/limite 200 e próxima com offset 200; o teste não cria 100.000 itens Qt.
  ```python
  assert model.rowCount() == 200  # Após concluir a primeira página.
  assert fetch.call_args.args == (0, '')
  ```
- [ ] Executar `.venv/Scripts/python -m pytest tests/ui/test_workspace.py tests/ui/test_models.py -q`; confirmar falha inicial; usar `QT_QPA_PLATFORM=offscreen` apenas nos testes automatizados.
- [ ] Implementar docks de funções/símbolos/strings/imports/regiões, busca, seletor de análise, detalhes, área central e barra de estado. Usar modelos Qt com `canFetchMore/fetchMore`, estados vazios explícitos, restauração de layout e configurações por usuário via QSettings. Ligar menus/atalhos de novo/abrir projeto, importar, configurar e cancelar; copiar/ler dados fora da thread Qt. Adicionar entrada `python -m reworkbench` e script GUI do pacote.
- [ ] Reexecutar testes e abrir a janela real no Windows para verificar foco, acoplamento e configuração ausente.
- [ ] Commit: `git add src/reworkbench/ui src/reworkbench/__main__.py pyproject.toml tests/ui` e `git commit -m "feat: add native reverse engineering workspace"`.

### Task 7: Importação configurável e navegação entre assembly e pseudocódigo

**Arquivos:** criar `src/reworkbench/ui/import_dialog.py`, `src/reworkbench/ui/code_view.py`, `tests/ui/test_navigation.py`; modificar `src/reworkbench/ui/window.py`.

**Interfaces:** `ImportDialog(catalog: JsonObject, choices: JsonObject)`, `selected_options() -> ImportOptions`; `CodeView`, `show_function(analysis_id: str, address: Address) -> None`, `apply_result(event: JsonObject) -> None`; `NavigationHistory.push(analysis_id: str, address: Address) -> None`, `back() -> tuple[str, Address] | None`, `forward() -> tuple[str, Address] | None` em `code_view.py`. Consumir contratos Session da tarefa 5.

- [ ] Escrever `test_slice_choice_is_explicit`, `test_raw_dialog_preserves_thumb_and_entry`, `test_function_selection_updates_both_views`, `test_old_result_cannot_replace_new_selection`, `test_xref_follow_and_back`, `test_rename_validation_and_error`, `test_analysis_error_keeps_hex_available`. Exigir que endereço/rotulagem da função exibida corresponda ao request atual; entradas inválidas não iniciam análise e configuração corrigida cria nova análise.
  ```python
  assert dialog.selected_options().entries[0].address == Address('ram', '0x1000')
  assert dialog.selected_options().entries[0].context['TMode'] == '1'
  ```
- [ ] Executar `.venv/Scripts/python -m pytest tests/ui/test_navigation.py -q` e confirmar falha inicial.
- [ ] Implementar seleção real das opções do catálogo, detalhes raw, validação de endereço/contexto e escolha de imagem. Adicionar visores de assembly e pseudocódigo com texto selecionável, destaque sintático básico, endereços clicáveis, xrefs de entrada/saída, histórico voltar/avançar e ação renomear. Exibir erro por função, carregamento e espera por autoanálise sem bloquear a janela. Invalidar seleção pendente ao mudar análise/projeto.
- [ ] Reexecutar testes e navegar a fixture PE real da lista de funções até uma chamada e de volta.
- [ ] Commit: `git add src/reworkbench/ui tests/ui/test_navigation.py` e `git commit -m "feat: add configurable imports and semantic navigation"`.

### Task 8: Observações, triagem e exportação

**Arquivos:** criar `src/reworkbench/application/audit.py`, `src/reworkbench/application/exports.py`, `src/reworkbench/ui/investigation.py`, `tests/unit/test_audit_exports.py`, `tests/ui/test_investigation.py`; modificar janela e sessão para expor exportação.

**Interfaces:** `audit_imports(imports: Iterable[JsonObject], xrefs: Iterable[JsonObject]) -> tuple[JsonObject, ...]`; regra versão 1 para `strcpy,strcat,gets,sprintf`, normalizando decoração sem confundir nomes de funções arbitrárias. `export_report(repo: ProjectRepository, analysis_id: str, destination: Path) -> None`; `export_pseudocode(repo: ProjectRepository, analysis_id: str, results: Iterable[DecompileResult], destination: Path) -> None`; `Session.export_report(analysis_id: str, destination: Path) -> Future[None]`, `export_functions(analysis_id: str, addresses: tuple[Address, ...], destination: Path) -> Future[None]`. Exportação solicita funções ainda não descompiladas e registra falhas individuais.

- [ ] Escrever `test_import_hint_contains_rule_and_reference`, `test_unresolved_import_is_not_a_fabricated_call`, `test_notes_survive_reopen`, `test_report_preserves_64bit_and_partial_status`, `test_export_keeps_old_file_on_failure`, `test_pseudocode_export_records_failed_functions`, `test_selected_functions_are_requested_before_export`. Exigir indicação como pista, endereços em texto, identificação SHA-256 e erro explícito quando `code is None`.
  ```python
  assert report['schema_version'] == 1
  assert report['records']['functions'][0]['address']['value'] == '0xfffffffffffffff0'
  ```
- [ ] Executar `.venv/Scripts/python -m pytest tests/unit/test_audit_exports.py tests/ui/test_investigation.py -q` e confirmar falha inicial.
- [ ] Implementar painel de notas/marcadores/pistas conectado à seleção e persistência. Exportar JSON com campos superiores `schema_version=1,analysis,sample,metadata,records,notes` por escrita incremental e substituição atômica, incluindo configuração, motor e estado/etapas nos metadados da análise. Exportar texto por função com endereço, origem e aviso de aproximação. Limitar consumo de memória à página; não sobrescrever amostra/arquivos internos do projeto com destinos de exportação.
- [ ] Reexecutar testes, fechar/reabrir e exportar investigação real com a fixture PE importada.
- [ ] Commit: `git add src/reworkbench/application src/reworkbench/ui tests/unit/test_audit_exports.py tests/ui/test_investigation.py` e `git commit -m "feat: preserve findings and export analysis evidence"`.

### Task 9: Execução Windows e validação integral

**Arquivos:** criar `scripts/run.ps1`, `scripts/doctor.ps1`, `README.md`, `docs/validation.md`, `tests/integration/test_end_to_end.py`; modificar `tests/conftest.py` e metadados de pacote para a entrega final.

**Interfaces:** `scripts/run.ps1` inicia a aplicação com Python da `.venv` sem interpolação em comandos; `scripts/doctor.ps1` apresenta o diagnóstico de `validate_engine` e retorna código diferente de zero se o motor não estiver pronto. README documenta instalação, caminhos Ghidra/JDK, execução, configuração raw, limites, recuperação e exportação.

- [ ] Escrever `test_windows_end_to_end_project` com Qt + worker reais: criar projeto, importar PE, selecionar função, obter assembly/pseudocódigo, navegar referência, renomear, anotar, exportar, fechar e reabrir em novo worker; verificar hash intacto, nome e nota persistidos. Escrever `test_worker_crash_recovery`, `test_close_window_terminates_descendants` e `test_doctor_script_reports_missing_engine`; este último executa o script com caminhos temporários inexistentes e exige código não zero e explicação legível.
  ```python
  assert hashlib.sha256(original.read_bytes()).hexdigest() == original_sha256
  assert reopened_function['name'] == 'funcao_investigada'
  assert report['notes'][0]['text'] == 'Observação persistida'
  ```
- [ ] Executar `.venv/Scripts/python -m pytest tests/integration/test_end_to_end.py -q`; confirmar falha do teste do script ainda ausente e registrar o estado real dos testes do fluxo já implementado. Não introduzir quebras artificiais em comportamentos existentes para produzir uma falha.
- [ ] Implementar scripts/documentação e completar a ligação de inicialização/encerramento. Registrar versões realmente usadas, comando de geração das fixtures, contagens dos testes e limites ainda observados em `docs/validation.md`. Corrigir falhas do fluxo antes de ampliar escopo; não produzir instalador com runtimes integrados neste marco.
- [ ] Executar `.venv/Scripts/python -m pytest tests/unit tests/ui tests/integration -q`, `scripts/doctor.ps1` e inicialização real pelo script. Exigir matriz Ghidra executada, sem skips apresentados como aprovação. Conferir fechamento de processos, navegação por teclado, painéis, estado parcial e saída exportada. Se dependências impediram integração, declarar o marco incompleto e identificar os testes não executados.
- [ ] Fazer revisão independente do conjunto contra a especificação, corrigir problemas materiais e repetir somente os testes afetados e a validação integral quando necessário.
- [ ] Commit: `git add scripts README.md docs/validation.md tests/integration/test_end_to_end.py tests/conftest.py pyproject.toml` e `git commit -m "feat: deliver verified Windows analysis workflow"`.

## Cobertura da especificação e execução

Importação e arquiteturas: tarefas 2/4/7. Área de trabalho: 5/6/7. Auditoria e exportação: 8. Persistência: 1/4/5. Comunicação, cancelamento e limites: 3/4/5. Distribuição inicial e aceite integral: 9. Todos os dez critérios da seção 9 da especificação têm testes nas tarefas correspondentes; a matriz real não é substituída por dublês de teste.

As tarefas 2 e 3 podem avançar independentemente depois da tarefa 1. As tarefas 6 e 8 podem avançar depois de estabilizar Session, mantendo arquivos de integração sob coordenação. A primeira implementação deve respeitar esses contratos; alterações de contrato exigem atualizar consumidores e plano juntos.

Recomendação de execução: **nativa**, com o agente principal implementando as tarefas em sequência e uma revisão independente ao final. Os contratos entre repositório, worker, sessão e Qt são estreitamente relacionados; manter a implementação desses contratos no mesmo contexto reduz divergências. Pesquisas e verificações independentes podem ser delegadas sem dividir a autoria dos contratos.

## Referências de implementação

- APIs da release adotada: https://github.com/NationalSecurityAgency/ghidra/blob/Ghidra_12.1.4_build/Ghidra/Features/PyGhidra/src/main/py/README.md
- Dependências do adaptador: https://github.com/NationalSecurityAgency/ghidra/blob/Ghidra_12.1.4_build/Ghidra/Features/PyGhidra/src/main/py/pyproject.toml — respeitar o pin de JPype1 fornecido pelo PyGhidra, sem atualização isolada.
- Receitas de fixtures: https://clang.llvm.org/docs/CrossCompilation.html, https://lld.llvm.org/, https://lld.llvm.org/MachO/index.html, https://llvm.org/docs/CommandGuide/llvm-objcopy.html

Estado: plano escrito; aguardando revisão do usuário e escolha do método de execução antes da implementação.
