from __future__ import annotations

import contextlib
import json
import os
import tempfile
from datetime import datetime, timezone
from pathlib import Path

from reworkbench.application.audit import records
from reworkbench.domain import RECORD_KINDS


def export_destination(repository, destination):
    destination = Path(destination).resolve()
    if destination.is_relative_to(repository.root) and not destination.is_relative_to(repository.root / 'exports'):
        raise ValueError('Exporte fora do projeto ou em sua subpasta exports para preservar os dados da análise.')
    if destination.exists():
        for analysis in repository.analyses():
            stored = repository.get_sample(analysis.sample_sha256)
            if destination.samefile(repository.root / stored.relative_path):
                raise ValueError('O destino corresponde à amostra original do projeto.')
            original = repository.get_metadata(analysis.id).get('original_path')
            if original and Path(original).is_file() and destination.samefile(original):
                raise ValueError('O destino corresponde ao binário usado na importação.')
    return destination


@contextlib.contextmanager
def atomic_text(destination):
    destination = Path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', newline='\n', prefix=f'.{destination.name}.', suffix='.tmp', dir=destination.parent, delete=False) as stream:
            temporary = Path(stream.name)
            yield stream
            stream.flush()
            os.fsync(stream.fileno())
        temporary.replace(destination)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def export_report(repository, analysis_id, destination, hints=(), cancel=None):
    destination = export_destination(repository, destination)
    analysis = repository.get_analysis(analysis_id)
    sample = repository.get_sample(analysis.sample_sha256)
    metadata = repository.get_metadata(analysis_id)
    header = {'schema_version': 1, 'exported_at': datetime.now(timezone.utc).isoformat(), 'analysis': {'id': analysis.id, 'state': analysis.state, 'revision': analysis.revision, 'options': analysis.options.to_json()}, 'sample': {'sha256': sample.sha256, 'size': sample.size}, 'metadata': metadata, 'notes': repository.notes(analysis_id), 'audit_hints': list(hints), 'pseudocode_notice': 'Pseudocódigo é uma aproximação; nomes, tipos e estrutura do código original não são recuperados integralmente.'}
    with atomic_text(destination) as stream:
        stream.write(json.dumps(header, ensure_ascii=False, indent=2)[:-1])
        stream.write(',\n  "records": {\n')
        for index, kind in enumerate(RECORD_KINDS):
            if index:
                stream.write(',\n')
            stream.write(f'    {json.dumps(kind)}: [')
            first = True
            for row in records(repository, analysis_id, kind, cancel):
                stream.write('\n      ' if first else ',\n      ')
                stream.write(json.dumps(row, ensure_ascii=False))
                first = False
            stream.write('\n    ]')
        stream.write('\n  }\n}\n')
    return destination


def export_pseudocode(repository, analysis_id, results, destination):
    destination = export_destination(repository, destination)
    analysis = repository.get_analysis(analysis_id)
    with atomic_text(destination) as stream:
        stream.write('/* Pseudocódigo aproximado produzido pelo Ghidra.\n')
        stream.write(' * Confirme comportamento, tipos e convenções no assembly.\n')
        stream.write(f' * SHA-256: {analysis.sample_sha256}\n * Análise: {analysis.id} · revisão {analysis.revision}\n */\n')
        for result in results:
            stream.write(f'\n/* Função em {result.address} */\n')
            if result.error:
                # Messages can contain arbitrary sample names. Keep them inside
                # a comment when exporting source-like text.
                message = result.error.replace('*/', '* /')
                stream.write(f'/* Decompilação indisponível: {message} */\n')
            else:
                stream.write(result.code or '')
                stream.write('\n')
    return destination
