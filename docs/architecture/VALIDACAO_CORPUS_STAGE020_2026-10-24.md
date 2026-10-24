# Rework — matriz de validação futura do decompilador (etapa 020)

**Preparada:** 24/10/2026. **NÃO EXECUTADA.**
**Escopo:** Rework nativo, etapas 002–019/020. Esta matriz define critérios de
aceite futuros, e não afirma que o código compila ou passa nos critérios.

## Corpus-fonte versionável

- `tests/fixtures/src/decompiler_stage020.c` — funções sem `main`;
  cobertura de CFG, Phi, memória, opacos, callbacks e bitwidth.
- `tests/fixtures/src/decompiler_stage020_stubs.c` — vínculos opcionais para
  as duas funções externas, usados apenas nas variantes futuras que
  necessitem ligação; `rw20_may_not_return` **retorna** nesta versão.
- `tools/rework_stage020_inventory.py` — inventário estático de
  dependências de arquivos, CMake, includes locais e presença de comandos.
  **Arquivo escrito mas não executado nesta etapa.**

## Casos esperados (o CFG exato depende de compilador, alvo e otimização)

| Caso/função | Entrada exemplar | Resultado C esperado | Invariantes do decompilador a exigir |
|---|---|---|---|
| `rw20_diamond` | `(1,10,8)` | `16` | Arestas e Phi/sources não trocados; junção não vira if falso |
| `rw20_diamond` | `(0,10,8)` | `5` | Ausência de cópias Phi sequenciais incorretas |
| `rw20_loop_recurrence` | `(0)`, `(1)`, `(2)` | `3`, `1`, `6` | Backedge, Phi, dominância e retorno preservados |
| `rw20_backedge_swap` | `(4,9,1)`, `(4,9,2)` | `5`, `-5` | Cópia simultânea; ciclos com snapshots virtuais |
| `rw20_critical_edge_candidate` | `(2,7,0)`, `(2,7,1)` | `2`, `10` | Detectar aresta crítica **somente se houver** no CFG real |
| `rw20_volatile_memory` | `(&v,10)` | `11`; `v=10` | Store/Load não podem desaparecer nem atravessar a ordem |
| `rw20_external_call` | `(2)` com stub | `21` | CALL opaca: efeitos, memória e retorno não inventados |
| `rw20_indirect_call` | Callback `f(x)=x+3`, x=4 | `7` | Não inventar destino de call indireta nem pureza |
| `rw20_possibly_nonreturning` | `(0)`, `(5)` com stub | `1`, `6` | `noreturn` só com evidência confiável, não nome de função |
| `rw20_bitwidth_sign` | `(8,-3)` | `7` | Signed vs unsigned preservados; sem tipo-fonte confirmado |
| `rw20_nested_branch` | `(1,-1,4)` | `5` | Condicionais aninhadas sem perda de CFG/efeitos |

**Cuidados com C:** entradas devem evitar overflow com sinal e INT_MIN para
`rw20_bitwidth_sign`; ponteiros têm de apontar para memória válida. Funções
externas dependem dos stubs; não afirmar que o binário linka sem eles.
O resultado C esperado não é um oráculo de CFG/assembly para qualquer flag
de compilação. `volatile` preserva acessos observáveis, mas não resolve todas
as semânticas de memória multithread.

## Plano de variantes, ainda NÃO EXECUTADO

| Variantes de build de fixture | Finalidade |
|---|---|
| x64 Windows PE, debug e otimizado | ABI Win64 e SSA em formas CFG diferentes |
| x64 Linux ELF, debug e otimizado | SysV AMD64; comparar hipóteses ABI |
| x86 PE 32-bit | Evitar extrapolar regras de x64 para ponteiros de 32 bits |
| Link com stubs e link com implementações alternativas | CALL retornável versus chamada não retornável |
| Debug-info presente/ausente | Separar tipo comprovado por metadados de pistas TypeEvidence |
| Binário adversarial com salto indireto, trap e tailcall | Nunca inferir saída inexistente ou pureza |

Nenhuma dessas variantes foi compilada na etapa 020.

## Matriz diagnóstica (quando autorizada)

- `ir-audit`: contagem de falhas de CFG, Phi, dominância e ordem de memória;
  tipos de token corretos e `rewrite_authorized=false`.
- `phi-transfers`: predecessor de cada entrada Phi, inclusive `-1` na
  entrada, cobertura de arestas e fontes; ausência de transferências inválidas.
- `phi-copy-plan`: captura virtual de toda origem escalar antes do primeiro
  commit lógico; ciclos são diagnósticos, não execuções.
- `edge-effects`: capturas e aplicações em fases separadas, Load/Store e
  chamadas opacas conservados, efeitos não reordenados.
- `control-safety`: terminadores e arestas recíprocas; arestas críticas só
  quando `outdegree(pred)>1` e `indegree(succ)>1`; desvio indireto, exceção
  e chamada `noreturn` sem inferência injustificada.
- `structuring-plan`: fallback completo dos blocos, obrigações de prova,
  `rewrite_authorized=false`, mesmo para planos estruturais elegíveis.
- `hir-observe`: identidades SSA preservadas e JSON limitado sinalizando
  truncamento, sem sumir silenciosamente com nós.
- CLI antigo `analyze`, `decompile`, `call-graph`, `effects-observe`:
  regressões de interface e saída.
- Módulos analisadores devem lidar com cancelamento, limites de nodes/edges,
  input malformado e alcance parcial sem crash ou hipótese inventada.

## Critérios de aceite FINAIS (não alcançados)

1. CMake configurar, compilar e linkar CLI/GUI sem erros de assinatura e includes.
2. Testes unitários de IR com Phi bem-formado, Phi de predecessor errado,
   memory-state fora de ordem, token incorreto, laço e aresta crítica.
3. Testes de integração nos binários das variantes acima, com logs e códigos
   de saída retidos, sem confundir CLI "capabilities" com execução.
4. Comparação diferencial Rework × Ghidra por função e por arquitetura:
   fluxo, chamadas, memória, tipo e divergências documentados.
5. Benchmarks de desempenho, fuzz e qualidade de decompilação medidos com
   corpus versionado, tempo/versão/configuração registrados.
6. Nenhuma transformação de IR/SSA permitida sem prova/revalidação da ordem
   de efeitos, Phi nas arestas e fluxo excepcional.

## Registro da auditoria estática já observada (NÃO teste de compilação)

Inspeção *read-only* de arquivos em 24/10/2026, por comandos de consulta
PowerShell; **não** foi executado o script Python proposto:
- 59 caminhos `src/` ou `ui/` C++/QRC explícitos encontrados em CMake
  e **0 arquivos referenciados ausentes** nessa verificação.
- Includes entre aspas de cinco unidades C++ críticas verificados na árvore
  local/include: **0 resoluções ausentes** nesse recorte.
- Nomes dos dez comandos de observação procurados em `cli_main.cpp`:
  todos presentes em pelo menos três ocorrências textuais na inspeção.
- Não valida C++20/Qt, organização de targets, transitividade de headers,
  link, cobertura CLI real, runtime, semântica de ISA ou comportamento futuro.

**Estado final etapa 020:** fixtures e plano PREPARADOS, não executados.
