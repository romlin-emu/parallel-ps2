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
    env = dict(os.environ, SANITIZER='', SANFLAGS='', UBSAN_OPTIONS='', GITHUB_ACTIONS='',
               SHARD='', SHARDS='', LIST='')
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
                                         SANFLAGS='', UBSAN_OPTIONS='',
                                         SHARD='', SHARDS='', LIST=''),
                                text=True, capture_output=True)
        assert result.returncode == 1, result.stdout + result.stderr
        assert 'runtime error: signed integer overflow' in result.stderr, result.stderr
        assert 'FAILED: overflow' in result.stdout, result.stdout
print('UBSan: both suite flag conventions detect overflow and fail the runner')

# Sharding: each suite runs exactly once across the shards, the default runs
# everything, and a failure in any shard fails that shard.
def shard_env(**extra):
    env = dict(os.environ, SANITIZER='', SANFLAGS='', UBSAN_OPTIONS='',
               GITHUB_ACTIONS='', SHARD='', SHARDS='', LIST='')
    env.update(extra)
    return env

def run_runner(script, env):
    return subprocess.run(['sh', str(script)], env=env, text=True, capture_output=True)

def ran_suites(result):
    return [line.split(': ', 1)[1].split() for line in result.stdout.splitlines()
            if line.startswith('passed: ')]

names = ['alpha', 'bravo', 'charlie', 'delta', 'echo', 'foxtrot', 'faultstress', 'stackframe']
with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp)
    shutil.copyfile(runner, root / 'run-all.sh')
    for name in names:
        (root / name).mkdir()
        (root / name / 'build.sh').write_text('echo "fixture:$0"\n')
    script = root / 'run-all.sh'

    def listing(shard, shards, **extra):
        result = run_runner(script, shard_env(SHARD=str(shard), SHARDS=str(shards),
                                              LIST='1', **extra))
        assert result.returncode == 0, result.stdout + result.stderr
        return result.stdout.split()

    full = run_runner(script, shard_env(LIST='1')).stdout.split()
    assert sorted(full) == sorted(names), full
    for shards in range(1, len(names) + 3):
        parts = [listing(i, shards) for i in range(shards)]
        flat = [name for part in parts for name in part]
        assert sorted(flat) == sorted(names), (shards, parts)
        assert listing(0, shards) == parts[0], 'assignment must be deterministic'
    assert run_runner(script, shard_env()).stdout.count('fixture:') == len(names)

    # Lanes that another lane covers are dropped before the split, once.
    ci = dict(GITHUB_ACTIONS='true', SANITIZER='address')
    flat = [name for i in range(3) for name in listing(i, 3, **ci)]
    assert sorted(flat) == ['alpha', 'bravo', 'charlie', 'delta', 'echo', 'foxtrot'], flat
    covered = sum(run_runner(script, shard_env(SHARD=str(i), SHARDS='3', **ci))
                  .stdout.count('covered in plain lane: faultstress') for i in range(3))
    assert covered == 1, covered

    # Executing shards runs each suite exactly once, and a failure surfaces
    # in the one shard that owns the suite.
    (root / 'bad').mkdir()
    (root / 'bad' / 'build.sh').write_text('exit 3\n')
    results = [run_runner(script, shard_env(SHARD=str(i), SHARDS='3')) for i in range(3)]
    ran = [name for r in results for names_ in ran_suites(r) for name in names_]
    assert sorted(ran) == sorted(names), ran
    codes = [r.returncode for r in results]
    assert sorted(codes) == [0, 0, 1], codes
    failing = [r for r in results if r.returncode == 1]
    assert len(failing) == 1 and 'FAILED: bad' in failing[0].stdout, failing[0].stdout

    for bad in ({'SHARDS': '0'}, {'SHARDS': '2', 'SHARD': '2'}, {'SHARDS': 'x'},
                {'SHARDS': '2', 'SHARD': '-1'}):
        env = shard_env()
        env.update(bad)
        assert run_runner(script, env).returncode == 2, bad
print('suite runner: shards cover each suite exactly once and propagate failure')

# The repository's own suites, not fixtures: any shard count partitions them.
repo_runner = runner
repo_full = run_runner(repo_runner, shard_env(LIST='1')).stdout.split()
assert repo_full and len(set(repo_full)) == len(repo_full)
for shards in (2, 5, 8):
    flat = [name for i in range(shards)
            for name in run_runner(repo_runner, shard_env(SHARD=str(i), SHARDS=str(shards),
                                                          LIST='1')).stdout.split()]
    assert sorted(flat) == sorted(repo_full), shards
print('suite runner: repository suites partition cleanly across shard counts')
