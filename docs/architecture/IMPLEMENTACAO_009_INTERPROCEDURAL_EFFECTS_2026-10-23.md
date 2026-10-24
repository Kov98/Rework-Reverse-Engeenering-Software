# Rework — Implementação 009: resumos interprocedurais conservadores de efeitos

**Data:** 2026-10-23
**Estado:** CÓDIGO ESCRITO E INTEGRADO AO CMake/CLI; **NÃO COMPILADO, NÃO TESTADO, SEM COMMIT** por ordem do usuário.
**Branch/worktree:** `feat/reworkbench`, `.worktrees/implementation`.
**Progresso geral estimado:** 9/100, PROVISÓRIO, sem qualquer demonstração de paridade com Ghidra.

## O que foi escrito

### 1. Contratos de evidências por função
`native/workbench/include/reworkbench/effect_summary.hpp`
- `FunctionEffectEvidence`: domínio, observação, confiança explícita `inferred`, endereço da instrução e ID do nó IR.
- `FunctionEffectSummary`: flags de leituras/escritas modeladas no corpo, possíveis efeitos observados transitivamente via chamadas diretas, chamadas não resolvidas, parcialidade, truncamento e desconhecimento de memória/registradores/controle.
- `InterproceduralEffectsObservation`: resumos, contagens de corpos/arestas, limites e avisos.

Valores `evidence_level`: `unknown`, `inferred`; `verified` está reservado para fases futuras com prova independente. **Nenhum resultado desta implementação usa `verified`**. `purity_verified` e `signature_verified` permanecem `false`.

### 2. Observação direta e propagação entre funções
`native/workbench/src/effect_summary.cpp`
- Reutiliza `observe_interprocedural_calls(snapshot)` para mapear chamadas diretas conhecidas e não resolvidas.
- Faz lift **limitado** dos corpos x86/x64 observáveis (até 128 funções; máximo de 2048 instruções, 384 blocos e 8192 nós IR por corpo; orçamentos são de segurança, não garantias de cobertura).
- Detecta `Op::Load`, `Op::Store`, `Op::Opaque` e `Op::CallOpaque` **apenas em blocos marcados alcançáveis**. `Load/Store` geram observações positivas de possível efeito, não prova de execução em todo caminho.
- Máximo de 32 evidências amostradas por função; mantém flags positivas mesmo após cap de amostras; `report_truncated` explícito.
- Arestas reversas propagam monotonicamente `transitive_read_observed`, `transitive_write_observed` e `may_reach_unresolved_call`, por fila até ponto fixo, incluindo chamadas recursivas. Os campos `read_witness_function_index` e `write_witness_function_index` apontam para a função que contém a evidência positiva observada, permitindo rastrear a origem transitiva.
- Calls externas, indiretas, corpo parcial, ISA não suportada, falha de orçamento e IR opaca preservam status desconhecido. `may_read_unknown_memory`, `may_write_unknown_memory`, `register_effects_unknown`, `control_effects_unknown` **NUNCA são liberados**.
- NÃO aplica pureza, preservação de registradores, substituição de loads, eliminação de stores, propagação de constantes ou alteração da IR existente.
- `report.complete` é somente completude da observação limitada, **não** prova de correção semântica nem de ausência de efeitos.

### 3. CLI e sistema de build (somente edições em texto)
- `native/workbench/src/cli_main.cpp`: inclui novo header, `effects_json()`, comando `effects-observe`, registro em `capabilities`, `analysis_commands` e ajuda do CLI; serializa também os índices de funções-testemunha para leituras/escritas transitivas.
- Resposta planejada: `interprocedural_effects`, campos de prova/incerteza e até 256 linhas de funções no JSON; `display_truncated` e `total_functions` expõem o limite de visualização.
- `native/workbench/CMakeLists.txt`: adiciona `src/effect_summary.cpp` a `reworkbench_native`.
- A interface gráfica e o emissor de pseudocódigo não foram alterados.

## Comando previsto, ainda NÃO executado

```powershell
reworkbench-cli effects-observe --input caminho\programa.exe
```

A existência do código NÃO garante que o CLI esteja compilando ou funcionando; é exigida validação posterior quando o usuário autorizar.

## Semântica das flags

- `direct_read_observed`: existe um `Load` modelado no corpo alcançável; não significa que a função sempre lê.
- `direct_write_observed`: existe um `Store` modelado no corpo alcançável; não prova escrita em toda execução.
- `transitive_*_observed`: o corpo atual OU alguma função conhecida alcançável no grafo direto contém a operação correspondente; caminhos podem não ser executados.
- `may_reach_unresolved_call`: cadeia do grafo que pode alcançar destino não descoberto ou indireto; `false` nunca autoriza concluir pureza.
- `may_*_unknown_memory = true`: nenhum corpo recebe certificado de ausência de efeitos.
- `signature_verified=false`, `purity_verified=false`: certificação reservada para validações futuras.
- `analysis_partial` e `report_truncated`: resultantes de modelo incompleto, orçamento ou caps de evidência.

## Riscos de engenharia e validação futura acumulada

**Prioridade alta ao autorizar validação:**
1. Compilar todos os incrementos 002–009 no mesmo worktree, conferir integração do CLI e os contratos C++.
2. Fazer regressão de `Lift/SSA`, `MemorySSA`, `abi-observe`, `call-graph` e `effects-observe`.
3. Fixar corpus de funções com `Store/Load` direto, chamada para função com Store, ciclo recursivo, função externa, chamada indireta, função parcial e binário sem ISA suportada.
4. Confirmar ausência de inferência falsa de pureza e ausência de escrita em casos negativos, inclusive corpo com intrínsecos não modelados.
5. Comparar com Ghidra após primeira bateria de integração e benchmarks.

**Atenção:** vários módulos anteriores continuam sem compilar/testar e podem conter erros C++/semânticos ainda desconhecidos; este incremento não afirma estar pronto para produção.

## Próxima implementação sugerida: 010

Criar **sumários de efeito por localização/região** (pilha/absoluta/unknown), com proveniência e diferenciação `may`/`must` apenas com prova. Conectar chamadas a contratos conservadores para planejar recuperação de assinaturas sem supor ABI verificada. Iniciar `TypeEvidence` tipado após fixar os contratos de memória e seus limites.
