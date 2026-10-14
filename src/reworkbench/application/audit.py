"""Versioned import review hints. A match is a lead, never a finding of exploitability."""
from __future__ import annotations

RULESET_VERSION = 1
RULES = {
    'gets': 'A leitura não recebe um limite de tamanho. Revise o destino e o fluxo de entrada.',
    'strcpy': 'Revise se o destino comporta a origem e se o tamanho é controlado.',
    'strcat': 'Revise o tamanho já usado no destino e o espaço disponível para a concatenação.',
    'sprintf': 'Revise a capacidade do destino e a origem do formato e dos argumentos.',
}


def records(repository, analysis_id, kind):
    offset = 0
    while True:
        page = repository.page(analysis_id, kind, offset=offset)
        yield from page.items
        offset += len(page.items)
        if offset >= page.total or not page.items:
            break


def _key(address):
    return (address['space'], address['value']) if address else None


def audit_imports(imports, xrefs):
    hints = []
    by_target = {}
    for imported in imports:
        name = imported.get('name', '').split('@', 1)[0]
        for prefix in ('__imp_', 'imp_', '_'):
            if name.startswith(prefix):
                name = name[len(prefix):]
        if name not in RULES:
            continue
        hint = {'rule': f'import.{name}', 'ruleset_version': RULESET_VERSION, 'name': imported.get('name'), 'library': imported.get('library'), 'address': imported.get('address'), 'message': RULES[name], 'references': [], 'assessment': 'Pista para revisão manual; não comprova vulnerabilidade.'}
        hints.append(hint)
        for target in [imported.get('address'), *imported.get('stub_addresses', [])]:
            if target:
                by_target.setdefault(_key(target), []).append(hint)
    for reference in xrefs:
        for hint in by_target.get(_key(reference.get('target')), ()):
            # Preserve the concrete instruction and containing function so the
            # caller can inspect the actual use instead of just the import.
            if len(hint['references']) < 1000:
                hint['references'].append({'source': reference['source'], 'source_function': reference.get('source_function'), 'target': reference['target'], 'type': reference.get('type')})
            else:
                hint['references_truncated'] = True
    return tuple(hints)


def review_imports(repository, analysis_id):
    return audit_imports(records(repository, analysis_id, 'imports'), records(repository, analysis_id, 'xrefs'))
