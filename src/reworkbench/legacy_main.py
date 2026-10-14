from __future__ import annotations

# Historical Python/Ghidra prototype. This is not the product's default entry.

import argparse
import sys
from pathlib import Path


def main(argv=None):
    parser = argparse.ArgumentParser(description='Bancada desktop de engenharia reversa com Ghidra')
    parser.add_argument('project', nargs='?', type=Path, help='Pasta de projeto existente')
    parser.add_argument('--sample', type=Path, help='Binário para importar no projeto aberto')
    args = parser.parse_args(argv)

    from PySide6.QtCore import QSettings, QTimer
    from PySide6.QtGui import QColor, QFont, QPalette
    from PySide6.QtWidgets import QApplication

    from reworkbench.application.session import Session
    from reworkbench.domain import EngineConfig
    from reworkbench.engine.config import discover_config
    from reworkbench.ui.window import MainWindow

    app = QApplication([sys.argv[0]])
    app.setOrganizationName('REWorkbench')
    app.setApplicationName('Workbench')
    app.setApplicationDisplayName('Bancada de engenharia reversa')
    app.setStyle('Fusion')
    app.setFont(QFont('Segoe UI', 10))
    palette = QPalette()
    for role, color in (
        (QPalette.Window, '#181e28'), (QPalette.WindowText, '#dce4ef'),
        (QPalette.Base, '#111722'), (QPalette.AlternateBase, '#192230'),
        (QPalette.Text, '#dce4ef'), (QPalette.Button, '#253244'),
        (QPalette.ButtonText, '#e4edf7'), (QPalette.Highlight, '#286d75'),
        (QPalette.HighlightedText, '#ffffff'), (QPalette.ToolTipBase, '#253244'),
        (QPalette.ToolTipText, '#e4edf7'), (QPalette.Link, '#8bd5ca'),
    ):
        palette.setColor(role, QColor(color))
    palette.setColor(QPalette.Disabled, QPalette.Text, QColor('#748195'))
    palette.setColor(QPalette.Disabled, QPalette.ButtonText, QColor('#748195'))
    app.setPalette(palette)
    app.setStyleSheet('''
        QMainWindow::separator { background: #2b394d; width: 3px; height: 3px; }
        QDockWidget::title { background: #202b3b; padding: 7px; font-weight: 600; }
        QToolBar { padding: 6px; spacing: 5px; border: 0; border-bottom: 1px solid #304055; }
        QToolButton { padding: 7px; }
        QHeaderView::section { background: #243145; padding: 6px; border: 0; }
        QTableView { border: 0; gridline-color: #28364a; }
        QLineEdit, QComboBox, QSpinBox { padding: 5px; border: 1px solid #35465e; border-radius: 3px; }
        QPushButton { padding: 7px 14px; border-radius: 4px; border: 1px solid #3a506a; background: #26384d; }
        QPushButton:hover { background: #33516a; border-color: #6babb5; }
        QPushButton:disabled { color: #748195; }
        QTabBar::tab { padding: 7px 12px; }
        QTabBar::tab:selected { color: #8bd5ca; }
        QProgressBar { max-height: 10px; border: 0; background: #263449; }
        QProgressBar::chunk { background: #73c8bd; }
        QLabel#overline { color: #8bd5ca; font-weight: 700; letter-spacing: 2px; }
        QLabel#welcomeTitle { font-size: 32px; font-weight: 700; padding: 12px 0; }
        QLabel#muted { color: #91a2b8; padding-top: 18px; }
        QLabel#codeHeading { background: #243145; color: #eaf2fc; font-weight: 600; padding: 10px; }
        QLabel#panelTitle { color: #8bd5ca; font-size: 10px; font-weight: 700; padding: 5px; }
        QStatusBar { border-top: 1px solid #304055; }
    ''')
    settings = QSettings()
    discovered = discover_config()
    ghidra = settings.value('engine/ghidra_home', str(discovered.ghidra_home or ''))
    java = settings.value('engine/java_home', str(discovered.java_home or ''))
    config = EngineConfig(Path(ghidra) if ghidra else None, Path(java) if java else None)
    session = Session(config, lambda event: None)
    window = MainWindow(session, settings=settings)
    window.show()

    def initial_open():
        if args.project:
            future = window._request_project(args.project, False)
            if args.sample:
                window.watch(future, lambda _: window.import_sample(args.sample))
        elif args.sample:
            window._show_bytes(args.sample)
            window.docks['hex'].raise_()
    if args.project or args.sample:
        QTimer.singleShot(0, initial_open)
    return app.exec()


if __name__ == '__main__':
    raise SystemExit(main())
