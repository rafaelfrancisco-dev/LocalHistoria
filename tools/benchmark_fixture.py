"""Consistent SQLite snapshots for disposable benchmark databases."""
from contextlib import closing
import pathlib
import sqlite3
import time


def backup(source, destination, timeout=30):
    source, destination = pathlib.Path(source).resolve(), pathlib.Path(destination).resolve()
    if source == destination or destination.exists():
        raise ValueError('Snapshot destination must be a new file')
    destination.parent.mkdir(parents=True, exist_ok=True)
    deadline = time.monotonic() + timeout

    def progress(*_):
        if time.monotonic() > deadline:
            raise TimeoutError('SQLite snapshot exceeded deadline')

    try:
        # Normal read-only mode includes committed WAL frames. Never use immutable on a live DB.
        with closing(sqlite3.connect(source.as_uri() + '?mode=ro', uri=True)) as src:
            src.execute('BEGIN')
            tables = [r[0] for r in src.execute("SELECT name FROM sqlite_master WHERE type='table'")]
            counts = {t: src.execute('SELECT count(*) FROM "' + t.replace('"', '""') + '"').fetchone()[0] for t in tables}
            with closing(sqlite3.connect(destination)) as dst:
                src.backup(dst, pages=256, progress=progress, sleep=.05)
                if dst.execute('PRAGMA integrity_check').fetchone()[0] != 'ok':
                    raise RuntimeError('Snapshot integrity check failed')
                for table, count in counts.items():
                    copied = dst.execute('SELECT count(*) FROM "' + table.replace('"', '""') + '"').fetchone()[0]
                    if copied != count:
                        raise RuntimeError('Snapshot row count mismatch: ' + table)
                dst.execute('PRAGMA wal_checkpoint(TRUNCATE)')
                dst.execute('PRAGMA journal_mode=DELETE')
            return counts
    except Exception:
        destination.unlink(missing_ok=True)
        raise
