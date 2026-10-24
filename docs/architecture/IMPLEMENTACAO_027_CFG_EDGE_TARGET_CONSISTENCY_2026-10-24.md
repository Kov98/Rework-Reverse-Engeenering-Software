# REwork — etapa 027: consistência entre destinos de CFG e arestas observadas

**Data:** 24/10/2026
**Branch:** `main` em `C:\Users\pedri\OneDrive\Documents\PROJETO\Rework Reverse Engeenering Software`.
**Situação:** alterações C++ escritas. **NÃO compiladas, NÃO testadas, NÃO benchmarkadas, SEM commit e SEM push.**
**Progresso editorial estimado:** 27/100 (27%, PROVISÓRIO). Não é equivalência comprovada ao Ghidra.

## Lacuna estática identificada

A auditoria `ir_semantic_audit.cpp` validava a reciprocidade
`successors` ↔ `predecessors`, mas isso não bastava para saber se
uma aresta declarada corresponde ao endereço de destino recuperado pelo
lifter. Um CFG artificial com aresta recíproca para o bloco errado podia
passar nesse recorte. Também faltava reprovar explicitamente arestas
repetidas e conferir a tabela `block_at`.

## Implementação da etapa 027

Arquivo `native/workbench/src/ir_semantic_audit.cpp`, na verificação de
blocos alcançáveis, antes da computação de dominância:

1. `cfg_block_address_index_mismatch`: índice em `block_at` para
   `Block.address.value` deve retornar o índice atual do bloco.
2. `cfg_duplicate_successor`: detecta sucessores duplicados.
3. `cfg_duplicate_predecessor`: detecta predecessores duplicados.
4. `cfg_successor_without_target`: toda aresta normal OBSERVADA deve
   coincidir com um endereço `taken` ou `not_taken` que aponte para o
   endereço inicial do bloco de destino no mesmo espaço.
5. `cfg_resolved_target_missing_edge`: se `taken` ou `not_taken`
   resolve para uma entrada presente em `block_at`, a aresta deve constar
   da lista de sucessores.
6. Mantidas as verificações existentes de destino válido, alcance
   e reciprocidade de predecessores e sucessores.
7. Toda falha torna `cfg_consistent=false`, impedindo a declaração de
   `structural_checks_passed`. **Não ocorre alteração do CFG, criação de
   destinos ou transformação da HIR.**

A checagem de alvos apenas confronta arestas já recuperadas; não
afirma que não existem saídas excepcionais, saltos indiretos não resolvidos
ou destinos fora da função.

## Cenários de teste FUTUROS — NÃO EXECUTADOS

- Aresta A→B e endereços `taken/not_taken` apontando para C:
  reprovar `cfg_successor_without_target`.
- Endereço `taken` apontando para B, sem A→B: reprovar
  `cfg_resolved_target_missing_edge`.
- Duplicar sucessor ou predecessor: sinalizar duplicidade.
- Corromper a tabela `block_at`: reprovar mismatch.
- Fluxos Goto, Conditional, fallthrough e loop regulares:
  não devem gerar falso positivo.
- Destino externo/indireto ausente de `block_at`:
  manter desconhecido, nunca inventar aresta.

## Limites

Inspeção de código e gravação de arquivos não substituem
compilação/execução dos testes. Etapas 002–027 ainda possuem dívida
técnica de integração e podem conter erros C++20/Qt, falsos positivos
e limitações semânticas ISA/ABI. Reescrita `rewrite_authorized=false`
permanece.

## Próxima missão 028

Compilar e validar o núcleo acumulado **somente com autorização explícita**
do usuário. Caso a restrição permaneça, continuar revisão conservadora
da conformidade CFG/SSA e critérios de regressão, sem alegar código funcional.
