# Rework — arquitetura mestre do motor de decompilação multilíngue (proposta v1)

Data: 2026-10-23. Estado: PROPOSTA ARQUITETURAL, NÃO IMPLEMENTAÇÃO CONCLUÍDA.
Escopo: evolução incremental do motor próprio Rework, com meta de equivalência mensurável ao Ghidra por recorte, seguida de superioridade demonstrável.
Regras: manter motor independente de Ghidra, IA e MCP; código-fonte e testes reais prevalecem sobre documentação; não apagar a implementação existente.

## 1. Diagnóstico técnico observado

- Base atual: C++20, Qt, carregadores nativos, integração opcional Capstone 5; código em native/workbench/src e API em native/workbench/include/reworkbench.
- Entrada atual de decompilação: decompile_function(AnalysisSnapshot, function_index, options, stop_token), declarada em decompiler.hpp.
- Pipeline observado: decompiler_lift.cpp valida função e gera CFG; modela subconjunto escalar x86/x64, flags, registradores e memória; decompiler.cpp aplica propagação limitada e imprime SSA/estado de máquina.
- Limitações observadas: chamadas opacas por ausência de ABI/efeitos de callee; instruções SIMD/FPU e várias outras opacas; ausência de recuperação de parâmetros, assinaturas, variáveis de alto nível, tipos complexos, classes, regiões gerais de controle e decompilação semântica das demais ISAs.
- Existem testes e relatórios prévios de builds, mas NÃO executei uma nova campanha completa de equivalência decompilador×Ghidra nesta elaboração.
- Estado Git: o worktree de implementação tem alterações preexistentes; preservar todas. Este documento não altera código-fonte.

## 2. Objetivo, limites e definições verificáveis

Objetivo de longo prazo: ferramenta própria capaz de abrir artefatos heterogêneos e reconstruir abstrações de programas, com saídas especializadas para diversas linguagens; entregar automação e análise documentadas por provas/testes.

Definir três níveis diferentes de saída:
1. PSEUDOCÓDIGO ANALÍTICO: expressa operações/efeitos com intrínsecos, sem promessa de recompilar.
2. CÓDIGO DE ALTO NÍVEL INFERIDO: recupera construções, tipos e objetos com grau de confiança/evidência.
3. CÓDIGO RECOMPILÁVEL VALIDADO: fonte e eventuais shims/runtime que compilam e passam critérios explícitos de equivalência em casos suportados.

NÃO prometer que qualquer PE/ELF, firmware ou bytecode vira C++, Java, Rust, C# ou JavaScript nativo e recompilável. Um executável não contém necessariamente nomes, tipos, ownership, lifetimes ou metadados; linguagens-alvo têm modelos de memória/GC/exceções distintos. Saídas translinguagem podem precisar de representações especiais, wrappers, FFI ou runtime de compatibilidade.

Equivalência com Ghidra deve ser estabelecida por MATRIZ DE CAPACIDADES/DOMÍNIO, não por afirmação geral. Exemplo: PE/ELF x86-64, compiladores GCC/Clang/MSVC, C escalar, O0/O1/O2/O3, ABI Windows e SysV.

## 3. Arquitetura em camadas e fronteiras

A. Workbench Qt existente: UI, dock, projeto, revisão manual, histórico, overlays, cache, CLI.
B. Serviço de análise e trabalhos: fila, prioridades, snapshots imutáveis, cancelamento, limites, cache versionado, logs.
C. Ingestão de artefatos: sniff de formato, PE/ELF/Mach-O/raw/JVM/.NET/JS, regiões, relocations, símbolos, debug info, proveniência.
D. Frontends de execução:
   D1. ISA nativa (x86/x64, ARM/Thumb, AArch64, MIPS inicialmente): decoder e lifter semântico específico.
   D2. JVM: classfile, constant pool, descriptors, opcodes, exceptions, verificação de stack.
   D3. .NET: PE/CLI metadata, CIL, generics, signatures, EH tables.
   D4. JS: fonte/AST/minificação; suporte bytecode somente por engine+versão quando viável.
E. Machine IR (MIR): efeitos precisos, endereços, espaços de memória, registradores, largura de bits, aliasing, call/return/throw, memória e controle; preserva semântica não suportada como opaca.
F. Normalização e análises: CFG, dominadores, loops, SSA, MemorySSA/efeitos, liveness, alias, dados, points-to, value ranges, constantes, invariantes; versionar cada passe.
G. Recuperação interprocedural: ABI, stack frames, parâmetros, retorno, efeitos de chamadas, FFI, summaries, indiretas, jump tables, tratamento de exceções.
H. Sistema de tipos/símbolos: fatos reais de DWARF/PDB/JVM/.NET, inferência estrutural, layouts, classes/vtables, RTTI quando existe, ponteiros e casts.
I. High-Level IR (HIR): expressões, instruções, statements, funções, regions, switch/loops, try/catch, classes, objetos; inclui efeitos explícitos e informação de origem.
J. Emitters independentes: C-like como base, C++ quando evidenciado, Rust-like, Java, C#, JS; recusam ou degradam graciosamente quando não houver representação fiel.
K. Verificação e benchmarks: suíte goldens, differential testing, interpreters, fuzzing de lifts, comparações Ghidra e outros, registros reproduzíveis.

Fluxo lógico:
ARQUIVO -> DETECTOR/LOADER -> DECODER + FRONTEND SEMÂNTICO -> MIR -> CFG/SSA/ANÁLISES -> TIPOS/ABI/INTERPROCEDURAL -> HIR -> EMITTER -> SAÍDA + EVIDÊNCIAS
                                      ^                                           |
                                      +------ RESULTADOS/ANOTAÇÕES/CACHE <--------+
O frontend para bytecode deve preservar metadados que um flattening prematuro para MIR poderia apagar. HIR deve aceitar nós especializados por frontend.

## 4. Contratos fundamentais

- BinaryArtifact: sha256, origem, formato, arquitetura, ABI sugerida, regiões, permissões, relocations, símbolos, depuração e warnings.
- Address/AddressSpace: tipo de espaço, largura, byte order, mapeamento file/virtual, identidade imutável; nunca confundir RVA com offset físico.
- MIRInstruction: opcode, operandos tipados por bitwidth, estado de memória/arquitetura, controle, efeitos explícitos, origin (endereço+bytes+instrução), confidence.
- OperationEffects: conjunto de estados lidos/escritos, memory regions, exceptions, barriers, unknown; nenhuma chamada pode preservar estado se o efeito for desconhecido.
- FunctionIR: CFG, dominadores, entradas/saídas, proveniência, status partial, warnings e orçamento utilizado.
- ABIModel: registers arg/return, stack cleanup, alignment, calling convention, varargs, clobbers, vector arguments, thunk/tail call e ambiente.
- TypeEvidence: tipo candidato, origem (debug/símbolo/RTTI/análise/usuário/IA), localizações de evidência, nível (confirmado/inferido/hipótese/desconhecido), conflitos.
- HIRNode: tipo, semântica, efeitos, anotações, spans de MIR e endereços reais; permitido preservar operações irrepresentáveis como intrínsecos.
- DecompilationArtifact: saída, idioma do emissor, versão do IR, versão dos passes, hash de entrada, opções, cobertura, warnings, métricas, mapa endereço-linha/token.
- Versionamento: chave do cache inclui hash do artefato, opções de carregador/ABI, arquitetura/lifter, pass pipeline, target emitter e versão dos contratos.
- Extensões/IA/MCP consultam serviços versionados read-only por padrão; não controlam o núcleo nem representam inferências como fatos.

## 5. Invariantes semânticas não negociáveis

1. A operação sem suporte vira opaca e explicita efeitos desconhecidos; nunca adivinhar sem prova.
2. Larguras, overflow modular, shifts, aliases parciais e flags respeitam a semântica da ISA.
3. Memória é modelada com efeitos e potencial aliasing; otimizações não atravessam stores/calls inseguros.
4. Erros, traps, exceções, FP/SIMD e comportamentos dependentes de ABI não podem desaparecer silenciosamente.
5. Cada linha/nó recuperado preserva origem ou declara que é construção sintética.
6. Resultados incompletos mostram aviso, cobertura e motivo; tempo/limite/cancelamento não retornam falso sucesso.
7. Inferências de IA nunca são inseridas como fatos verificados sem validação.
8. O core processa dados não confiáveis com limites de memória, tempo, recursos, profundidade, além de testes adversariais.

## 6. Subprojetos técnicos recomendados

A. Frontend semântico ISA: preservar Capstone como decoder opcional, evoluir lifters com especificações testáveis e geradores de semântica; corrigir/validar x86-64 antes de ampliar.
B. Memória/SSA: representação explícita de regiões, calls e efeitos; dominadores/phi apenas com CFG válido, MemorySSA/alias conservadores.
C. ABI/interprocedural: Win64 + SysV AMD64 como primeiros perfis; depois x86 32, AArch64. Funções externas com catálogo verificado de protótipos e efeitos.
D. Recuperação de variáveis: stack frame, live ranges, SSA coalescing, escapes, split/merge, parâmetros/retornos. Distinguir typedef presumido e tipo inferido.
E. Tipos/objetos: constraints, unification com conflitos, DWARF/PDB e demangling, RTTI/vtables quando presentes. C++ templates não necessariamente recuperáveis.
F. Controle de alto nível: loops, conditions, switches/jump tables, irreducible CFG e exceptions com fallback claro.
G. Simplificação e saída: algebra segura, dead-code/liveness com provas semânticas locais, HIR, formatter e renderizadores por linguagem.
H. JVM/.NET/JS: pipelines específicos, priorizando preservar metadados e semânticas de VM; não tratar todos como Assembly nativo.
I. Motor de diagnóstico: trace por passe, explicações, snapshots/replay, alterações auditáveis, relatórios comparativos e métricas.
J. UI/SDK: consumir APIs do core; nunca colocar algoritmo central em widgets Qt.

## 7. Proposta de organização futura (não migrar sem teste)

native/decomp_core/
  include/rework/{artifact,address,mir,hir,abi,types,evidence,analysis,emitter}.hpp
  src/binary/{pe,elf,macho,raw,jvm,cli,js}/
  src/lifter/{x86,x64,arm,aarch64,mips}/
  src/mir/{builder,validate,serialize,effects}/
  src/passes/{cfg,dom,ssa,memoryssa,alias,range,constprop,loops}/
  src/interproc/{abi,callgraph,summary,stack,params,returns}/
  src/types/{constraints,layouts,debug_info,classes}/
  src/hir/{region,expression,control,object,validate}/
  src/emit/{c,cpp,rust,java,csharp,javascript}/
  src/verify/{semantic_oracle,comparison}/
native/workbench/ (preservar UI Qt/CLI/projetos existentes como consumidores)
tests/decompiler/{fixtures,semantic,lifters,abi,types,structuring,emitters,differential,performance}/

Migração: adaptador temporário do AnalysisSnapshot/decompile_function existente para serviços novos; uma funcionalidade por vez. Não converter todo o código para outra linguagem ou reescrever o Qt por estética. Manter projeto compilável em cada merge.

## 8. Matriz de saída multilíngue

Entrada PE/ELF nativa (C/C++/Rust/Go compilados):
  - Primeira saída: MIR/SSA + C-like explicativo.
  - C++: classes/virtual/destrutores quando há evidências; reconstrução de templates e nomes originais é geralmente parcial.
  - Rust: perspectiva Rust-like quando estruturas estão evidenciadas; ownership e lifetimes originais não podem ser garantidos.
  - Java/C#/JS a partir de nativo: pseudocódigo translinguagem para inspeção, não promessa de programa recompilável fiel; possível exigir FFI/runtime.
Entrada JVM .class/.jar:
  - Java de alta fidelidade como alvo principal; outros formatos só com requisitos explícitos de semântica e metadados.
Entrada .NET com CIL:
  - C# como alvo principal, aproveitando metadados, signatures, generics e exceções.
Entrada JavaScript fonte/bundle:
  - Parser de JS, AST, análise de módulos, mapas de fontes quando presentes, desminificação; não supor que seja decompilação de código-máquina.
Entrada bytecode V8/SpiderMonkey:
  - Compatibilidade específica por engine e versão, pois formatos internos mudam.

Cada emissor deve ter capability report: supported / degraded / not_supported por construct e explicação dos shims necessários.

## 9. Equivalência mensurável com o Ghidra: roteiro

M0 — Fotografar estado atual: código, commit, build, ABI, testes, corpus versionado, métricas e lacunas por código/linha.
M1 — x86-64 escalar correto: equivalência diferencial de instruções/flags, memória e controle em testes próprios; nenhuma inferência ilegal.
M2 — Funções C Win64/SysV em O0: ABI, stack, argumentos, returns, calls e C legível; comparar corpus com Ghidra.
M3 — Otimização O1/O2/O3: aliases, loops, switches, recuperação de expressão e variáveis, constante e preservação semântica.
M4 — Tipos, structs e C++: DWARF/PDB, classes/vtables, inferência de layout, constructors/destructors, exceções; avaliar sobre corpus real.
M5 — Robustez: binários grandes, imports, indirect calls, casos malformados, desempenho, cancelamento, análise incremental.
M6 — AArch64/ARM e demais ISAs: lifter semântico e ABI dedicados, benchmarks por arquitetura.
M7 — JVM e CIL: frontends próprios, metadados, EH e saída Java/C#; comparação com decompiladores especializados, não só Ghidra.
M8 — JS e emissores alternativos; hipóteses de recuperação de linguagem com classificação de confiança.
M9 — Pesquisa para ultrapassar Ghidra: qualidade por domínio, recuperação interprocedural, explicações rastreáveis, refinamento verificável de tipos.

Não anunciar "equivalente ao Ghidra" fora da matriz explicitamente validada.

## 10. Laboratório obrigatório

- Corpus próprio com licença/versão, fontes conhecidas, builds GCC/Clang/MSVC, O0–O3, x86/x64 (e depois ARM), DWARF/PDB com e sem símbolos.
- Oráculos: comparação diferencial de comportamento para entradas testáveis, emulador/execução controlada, propriedades de IR, compilação e testes de saída quando suportados.
- Rodar Rework e Ghidra no mesmo corpus, com mesmas convenções e tempo/memória registrados; salvar logs, versões, hashes e saídas.
- Métricas: cobertura de instruções/CFG, caminhos erroneamente omitidos, funções/params/retornos, precision de tipos, regressões de compilação, taxas de erro/timeout, qualidade legível avaliada separadamente.
- Segurança: artefatos conhecidos e sandbox para execução dinâmica; fuzz inputs do parser e do lifter; nunca executar binários desconhecidos diretamente por conveniência.
- Gates: build + unit + differential + regressão + orçamento de recursos + documentação de casos pendentes.
- Critério de superioridade: melhorias consistentes em um corpus definido sem degradar segurança/correção relevantes; avaliação independente quando possível.

## 11. Próxima implementação recomendada (primeiro incremento técnico)

Objetivo: criar protótipo robusto de recuperação de assinatura x86-64 para um corpus controlado C O0.
Tarefas:
1. Congelar versão de análise/lifter existente e construir corpus mínimo (parametros 0–6, int/ponteiros, retornos, chamadas diretas, memória e stack).
2. Adicionar perfis Win64 e SysV AMD64 versionados, com argumentos, retornos, clobbers, callee-saved e stack alignment.
3. Introduzir call effects e stack frame analysis conservadores. Chamadas desconhecidas continuam opacas; ABI inferida não é prova.
4. Ligar ABI+SSA a hipóteses de parâmetros/retorno com proveniência.
5. HIR de função e emissor C-like piloto, respeitando efeitos e intrínsecos opacos.
6. Differential tests vs Ghidra, incluindo casos negativos; manter API existente compatível.
Aceite: compila, não quebra testes atuais, recupera assinaturas coerentes para corpus suportado, sinaliza casos ambíguos e melhora contra baseline de Rework. Não reivindicar paridade geral.

## 12. Referências técnicas primárias a estudar no código local

Rework:
- native/workbench/src/decompiler_lift.cpp, decompiler.cpp, decompiler_internal.hpp
- native/workbench/src/decoder.cpp, analysis.cpp, ir.cpp, flow_recovery.cpp, types.cpp
- native/workbench/include/reworkbench/decompiler.hpp
- native/workbench/docs/decompiler.md, docs/validation.md, tests/native/test_cli.py

Ghidra da instalação local:
- Ghidra/Features/Decompiler/src/decompile/cpp/architecture.cc
- funcdata.cc, coreaction.cc, fspec.cc, heritage.cc, type.cc, variable.cc, blockaction.cc, printc.cc
- specs de processador SLEIGH, p-code e modelos de compilador.

Fontes oficiais:
- https://github.com/NationalSecurityAgency/ghidra
- https://ghidra.re/ghidra_docs/languages/html/sleigh.html
- https://www.llvm.org/docs/LangRef.html
- https://docs.oracle.com/en/java/javase/27/docs/specs/jvms/index.html
- https://ecma-international.org/publications-and-standards/standards/ecma-335/
- https://github.com/Vineflower/vineflower

## 13. Decisões ainda abertas, a validar antes de código novo

- MIR universal versus IRs por VM com normalização sem perda; decidir por protótipos e testes de Java/.NET.
- Contrato estável entre core C++ e Qt: datastructs existentes usam QString/QVector; definir fronteira sem big-bang rewrite.
- Modelo de memória por regiões e aliases, representação de exceptions e SIMD.
- Escolha de verificador de semântica (interpretação própria e/ou oráculos externos em testes).
- Profundidade inicial de C++ RTTI/vtables versus expansão de ISA.
- Política de licenças e atribuição ao estudar referências externas.
- Critérios quantitativos de equivalência por corpus, sem fixar percentuais artificiais antes de conhecer baseline.

Este documento é um PLANO proposto. Não confirma implementação, testes executados ou desempenho do motor futuro.
