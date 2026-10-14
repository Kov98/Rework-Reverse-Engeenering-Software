"""Compatibility launcher for the independent C++ desktop application."""
from __future__ import annotations

import argparse
import os
import subprocess
from pathlib import Path


def main(argv=None):
    parser = argparse.ArgumentParser(description='REWorkbench: aplicação própria em C++/Qt')
    parser.add_argument('project', nargs='?', type=Path)
    parser.add_argument('--sample', type=Path)
    args = parser.parse_args(argv)
    root = Path(__file__).resolve().parents[2]
    name = 'reworkbench.exe' if os.name == 'nt' else 'reworkbench'
    candidates = (root / 'build' / 'bin' / name, root / 'native' / 'workbench' / 'build' / 'bin' / name)
    executable = next((path for path in candidates if path.is_file()), None)
    if executable is None:
        parser.exit(1, 'O aplicativo C++ ainda não foi compilado. Consulte README.md. Este lançador não compila e não inicia o protótipo Ghidra.\n')
    command = [str(executable)]
    if args.project:
        command.extend(('--project', str(args.project.resolve())))
    if args.sample:
        command.extend(('--sample', str(args.sample.resolve())))
    try:
        return subprocess.run(command, shell=False, cwd=root).returncode
    except OSError as error:
        parser.exit(1, f'Não foi possível iniciar o aplicativo C++: {error}\n')


if __name__ == '__main__':
    raise SystemExit(main())
