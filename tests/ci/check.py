#!/usr/bin/env python3
"""Exercise runner discovery, sanitizer propagation and failure aggregation offline."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

runner = Path(__file__).resolve().parents[1] / 'run-all.sh'
with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp)
    shutil.copyfile(runner, root / 'run-all.sh')
    for name in ('new-suite', 'faultstress', 'spsc', 'mtvuring', 'pgscache', 'stackframe'):
        (root / name).mkdir()
        (root / name / 'build.sh').write_text(
            'echo "fixture:$SANITIZER:$SANFLAGS:$UBSAN_OPTIONS"\n')
    env = dict(os.environ, SANITIZER='', SANFLAGS='', UBSAN_OPTIONS='', GITHUB_ACTIONS='')
    def run():
        return subprocess.run(['sh', str(root / 'run-all.sh')], env=env,
                              text=True, capture_output=True)
    result = run()
    assert result.returncode == 0, result.stdout + result.stderr
    assert result.stdout.count('fixture:') == 6, result.stdout
    env['SANITIZER'] = 'undefined'
    result = run()
    assert result.returncode == 0, result.stdout + result.stderr
    assert result.stdout.count('fixture:') == 6, result.stdout
    env['GITHUB_ACTIONS'] = 'true'
    result = run()
    assert result.returncode == 0, result.stdout + result.stderr
    assert result.stdout.count('fixture:') == 1, result.stdout
    assert 'fixture:undefined:-fsanitize=undefined -fno-omit-frame-pointer:halt_on_error=1' in result.stdout
    assert 'passed: new-suite' in result.stdout
    (root / 'bad').mkdir()
    (root / 'bad' / 'build.sh').write_text('exit 3\n')
    result = run()
    assert result.returncode == 1 and 'FAILED: bad' in result.stdout, result.stdout
    assert 'passed: new-suite' in result.stdout
print('suite runner: discovery, sanitizer environment, consolidation and failure propagation passed')

# A diagnostic must fail the runner, even with UBSan's default recoverable build.
with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp)
    shutil.copyfile(runner, root / 'run-all.sh')
    (root / 'overflow').mkdir()
    (root / 'overflow' / 'bad.c').write_text(
        '#include <limits.h>\nint main(void) { volatile int n = INT_MAX; return (n + 1) == 0; }\n')
    for flags in ('$SANFLAGS', '-fsanitize=$SANITIZER'):
        (root / 'overflow' / 'build.sh').write_text(
            'set -e\ncd "$(dirname "$0")"\n'
            f'cc -O1 {flags} bad.c -o bad\n./bad\n')
        result = subprocess.run(['sh', str(root / 'run-all.sh')],
                                env=dict(os.environ, SANITIZER='undefined',
                                         SANFLAGS='', UBSAN_OPTIONS=''),
                                text=True, capture_output=True)
        assert result.returncode == 1, result.stdout + result.stderr
        assert 'runtime error: signed integer overflow' in result.stderr, result.stderr
        assert 'FAILED: overflow' in result.stdout, result.stdout
print('UBSan: both suite flag conventions detect overflow and fail the runner')
