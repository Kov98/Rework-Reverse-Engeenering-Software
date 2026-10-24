# REwork — implementação 025: proveniência SSA de saídas de bloco e valores globais

**Data:** 24/10/2026
**Repositório de trabalho:** `C:\Users\pedri\OneDrive\Documents\PROJETO\Rework Reverse Engeenering Software`; branch `main`.
**Estado:** alterações de C++ escritas, **sem compilação, testes, benchmarks, commit ou push**.
**Estimativa editorial global:** 25/100 provisórios; não é paridade com Ghidra nem comprovação de funcionamento.

## Problemas vistos na revisão estática

A auditoria `ir_semantic_audit.cpp` validava tipos em `Block.output[slot]` e dominância de operandos explícitos de instruções, mas **`Block.output` também é um uso implícito no fim do bloco**. Uma saída de estado com tipo correto poderia vir de um bloco irmão que não domina a saída.

Também havia uma exceção ampla para `Node.block < 0` nos operandos SSA. Esse caminho deveria permitir apenas `Op::Input` global legítimo (`block == -1`), não nós arbitrários fora do CFG.

## Alterações em `native/workbench/src/ir_semantic_audit.cpp`

1. **Contrato de saída por slot:** cada `Block.output[slot]` válido precisa ser um `Op::Input` global legítimo, um nó pertencente ao próprio bloco, ou um nó cuja definição domina o bloco segundo a análise já calculada. Caso contrário: `block_exit_non_dominating_value`, falha SSA.
2. **Definições globais:** qualquer nó que tenha `block == -1` e não seja `Op::Input` causa `invalid_global_definition`. IDs de blocos inválidos continuam tratados separadamente.
3. **Operandos globais:** a exceção de dominância para `def.block < 0` verifica explicitamente `block == -1 && op == Input`; se não, informa `non_input_global_operand`.
4. **Phi e a entrada sintética:** uma fonte da aresta `-1` exige `Op::Input` real; fontes globais ilegítimas em outras arestas geram `phi_invalid_global_source`.

Todas essas falhas entram em `ssa_definitions_consistent`/contadores e, por consequência, impedem `structural_checks_passed`. As regras não transformam a IR/CFG e não autorizam emission C/C++.

## Limites e dívidas

- As verificações se aplicam ao CFG/IR levantados; **não provam equivalência de semântica x86, aliasing, exceções ou ABI**.
- Permanecem sem build e regressões conjuntas os incrementos 002–025. Um erro de C++/Qt ou falso positivo poderá aparecer na futura validação.
- Quando o usuário permitir, criar casos com valor de ramo irmão usado em `Block.output`, saída com `Input` global, usos `Phi` legítimos de backedge, valor global não-Input, blocos inalcançáveis e operandos sem definição.
- **Nenhum teste foi executado nesta etapa**. Nenhum commit ou push.

## Próxima etapa 026

A maior prioridade segue sendo compilar/testar o núcleo existente com autorização explícita do usuário e corrigir falhas concretas; enquanto não houver liberação, aprofundar revisão estática de tipos, fluxo SSA e fronteiras de blocos, preservando o fallback.
