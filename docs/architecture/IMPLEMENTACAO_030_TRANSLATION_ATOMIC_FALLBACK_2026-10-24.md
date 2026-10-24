# REwork — Etapa 030: fallback opaco atômico por instrução

**Data:** 24/10/2026
**Repositório:** `C:\Users\pedri\OneDrive\Documents\PROJETO\Rework Reverse Engeenering Software` na branch `main`.
**Status:** código escrito, revisão manual por leitura; **NÃO COMPILADO, NÃO TESTADO, NÃO EXECUTADO, SEM COMMIT/PUSH**.
**Progresso editorial global:** 30/100 (30% PROVISÓRIOS), não paridade medida com Ghidra.

## Problema real de transação identificado em fontes

O método `Builder::translate(instruction)` de `native/workbench/src/decompiler_lift.cpp` pode emitir IR intermediária de `Load`, endereço, cálculo e flags antes de verificar se uma operação final de escrita aceita seus operandos; pode depois retornar `false`. O laço `Builder::run` antes aplicava `opaque(...)` imediatamente, deixando nós e alterações especulativas da instrução malformada no programa. O fallback passava a representar duas linhas de efeitos (tentativa + opaco), podendo produzir valores/efeitos sem correspondência na execução real e SSA incorreta.

Casos ilustrativos de tradução não concluída incluem `mov` ou aritmética com fonte parcialmente lida e destino de memória não suportado, e `cmov` com operandos que provoquem IR antes de uma validação tardia. Essas possibilidades provêm do fluxo do código; não foram testadas.

## Implementação

**Arquivo alterado:** `native/workbench/src/decompiler_lift.cpp`.

Imediatamente antes de cada `translate(instruction)`:
1. Captura `program_.nodes.size()` e `block.definitions.size()`.
2. Copia `state_` e `comparison_` anteriores.
3. Captura `block.terminator`, `block.condition` e `block.return_target`.

Quando a tradução completa retorna `true`, mantém a IR e incrementa `program_.modeled` como antes.

Quando retorna `false`, antes de registrar `Opaque/CallOpaque`:
1. Reduz `program_.nodes` e `block.definitions` até os comprimentos capturados, descartando todas as definições IR provisórias.
2. Restaura os valores de registradores/flags/memória/arquitetura em `state_`.
3. Restaura a informação de comparação e os campos do terminador do bloco.
4. Limpa `extracts_` para impedir reuso de IDs SSA de nós descartados. Limpar o cache só causa novas expressões em usos posteriores; não transforma a semântica.
5. Registra **um único efeito opaco conservador para a instrução não modelada**, preservando a semântica desconhecida e o indicador `program_.incomplete`.

`program_.total` continua contando toda instrução alcançável, inclusive fallback opaco, e `program_.modeled` apenas traduções concluídas. Este patch **não elimina** semântica desconhecida: mantém todas as restrições de rewrite.

## Plano de verificação FUTURA (NÃO EXECUTADO)

- Instrução com `Load` gerado antes de falhar uma `write` de destino inválido; após fallback, nenhum Load especulativo deve sobreviver.
- Instrução aritmética parcialmente emitida antes do fallback; definições, estado e flags anteriores devem ser preservados até `Opaque`.
- Cache de `Extract` criado durante tradução malsucedida; nenhum Value ID descartado pode aparecer em usos futuros.
- Tradução bem-sucedida não deve perder nós, temporários ou estado.
- Instrução opaca com chamada deve continuar propagando todos os `StateField`, sem inferir `noreturn` ou pureza.
- Corrigir quaisquer incompatibilidades de API Qt/C++20 encontradas quando compilação/testes forem autorizados.

**Não houve build, execução, benchmark, testes, commit ou push nesta etapa.** A etapa 030 é correção de fonte de um problema lógico identificado por leitura, não um defeito corrigido e validado em produção.

## Etapa 031

Prioridade de engenharia: autorização expressa para compilar e testar as etapas 002–030 em lote e investigar as regressões acumuladas. Se não for autorizado, prosseguir com auditoria estática e correções fundamentadas na IR/lifter; não inflar garantia de funcionamento.
