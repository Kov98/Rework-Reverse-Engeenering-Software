# REwork — etapa 024: integridade de projeções de estado de operações opacas

**Data:** 24/10/2026  
**Branch:** `main` (pasta principal `Rework Reverse Engeenering Software`)  
**Situação:** implementação ESCRITA em C++, NÃO COMPILADA, NÃO TESTADA, SEM COMMIT/PUSH.  
**Progresso editorial:** 24/100 (24% provisório, sem equivalência comprovada ao Ghidra).

## Problema de integridade confirmado por inspeção

Na etapa 023, a auditoria de `Opaque/CallOpaque` exigia a projeção
`StateField("mem")` correta antes do uso de memória. Ainda era possível,
entretanto, omitir ou duplicar projeções de registros, flags ou estado
arquitetural (`arch`) sem o validador de ordem notar a ausência do
conjunto completo de resultados opacos.

O próprio lifter, em `decompiler_lift.cpp`, cria `Op::StateField` para
**todos** os `state_slots` logo depois de `Opaque/CallOpaque`.
O novo contrato da auditoria exige que o fluxo recebido preserve essa
sequência, conservadoramente, sem assumir ausência de efeitos.

## Alteração de código

`native/workbench/src/ir_semantic_audit.cpp`:
- `pending_state_effect` guarda o último efeito opaco pendente em cada bloco
  alcançável, e `pending_state_slots` guarda as projeções ainda esperadas.
- Uma nova operação (não-`StateField`) antes de todas as projeções
  registra `opaque_state_projection_interrupted`.
- Cada projeção deve apontar exatamente para o efeito pendente e consumir
  um slot ainda não projetado. Associação errada, repetição, projeção extra
  ou sem origem são sinalizadas por `opaque_state_projection_order`.
- Um efeito opaco que termina o bloco sem todos os `StateField` previstos
  registra `opaque_state_projection_incomplete`.
- Violações tornam `memory_state_order_consistent=false`, impedindo
  `structural_checks_passed=true`; a checagem de `mem` da etapa 023
  permanece independente e ativa.
- Nenhum nó/CFG é modificado. Nenhuma chamada é suposta pura,
  sem exceções ou sem efeitos externos. Reescrita continua desabilitada.

## Validação futura (não executada)

Preparar IR malformada com `StateField` duplicado, uma projeção omitida,
projeção apontando a `Opaque` anterior, projeção fora da sequência, segunda
chamada opaca antes do fim da anterior, e IR normal gerada pelo Builder.
Somente quando o usuário autorizar: compilar e executar regressões de
`ir-audit`, `phi-transfers`, `structuring-plan`, checar ausência
de falsos positivos e comparar com Ghidra.

**Risco:** análise escrita e inspecionada como texto, não como build/teste.
Incrementos 002–024 seguem sem validação conjunta; a porcentagem não é
uma medida de qualidade comprovada.

## Próximo incremento 025

Prioridade principal continua sendo build e regressão do núcleo quando
autorizado. Se execução continuar proibida, investigar estaticamente
as relações entre `OpaqueState`, `StateField` e versões SSA por slot,
sem declarar equivalência semântica.
