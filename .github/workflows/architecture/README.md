> This documentation is distributed with [Romlin’s parallel-ps2 fork](https://github.com/romlin-emu/parallel-ps2), derived from [libretro/parallel-ps2](https://github.com/libretro/parallel-ps2). Original project names, authors, and licenses are retained.

# CI Documentation

## Workflows

| Workflow | Runs on | Purpose |
| --- | --- | --- |
| `romlin_release.yml` | Pushes to `romlin`, all PRs, manual dispatch | Builds the Windows and Linux x86_64 libretro cores, runs the native smoke gate and packages zips with `.sha256` files. Calls `tests.yml` at the same revision and publishes only when it and both builds pass. |
| `tests.yml` | Called by `romlin_release.yml`; manual dispatch | Plain, ASan and UBSan lanes of `tests/run-all.sh`, each split into 5 parallel shards (15 jobs), plus the `tests/ci/check.py` runner checks in every job. A `what ran` job summarises the result. |
| `lint_gamedb.yml` | PRs and pushes to `romlin` that touch the GameDB or its schema | Validates and format-checks `bin/resources/GameIndex.yaml` with pinned `ajv-cli` and `prettier`. |

`dependabot.yml` keeps the pinned GitHub Actions current.

## Romlin builds and releases

Only a successful `romlin` push or manual dispatch on that branch, in
`romlin-emu/parallel-ps2`, publishes the latest release; PRs and dispatches
on other refs only validate. PR validation cancels stale runs; publication
is not cancelled. Publication needs both core builds and every `tests.yml`
shard to pass. Archive names, contents, `.sha256` files and the
`core-YYYYMMDD-<10 sha chars>` tag format are a contract with Romlin and
must not change. Linux uses ccache: every run restores the newest cache,
but only pushes to `romlin` save one, so PRs cannot evict it. Windows builds
natively without extra cache tooling.

## Sharded tests

`tests/run-all.sh` accepts `SHARD=i SHARDS=n` (i from 0). It drops suites
another lane already covers, then assigns the rest greedily by approximate
runtime so the slowest (`smaprx`, `pgs`, `spsc`, `stackframe`) are spread
out. Every suite runs in exactly one shard; new suites are discovered
automatically and count as small until added to `suite_weight`. Without the
variables it runs everything, as it does locally. `LIST=1` prints a shard's
suites without running them. Five shards per lane keep each job near the
cost of its heaviest single suite; raising `SHARDS` further would not make
the longest job shorter. To change the count, edit `shard:` and `SHARDS` in
`tests.yml` together.

## Removed from this fork

The Android and Linux aarch64 cross builds (`aarch64_build_libretro.yml`)
and the legacy Linux `libretroization` build and star-triggered
(`linux_build_libretro.yml`) were removed: Romlin ships only the Windows and
Linux x86_64 cores, and `romlin_release.yml` builds that. `tests.yml` no
longer runs on pushes to `master` or `libretroization`, and the GameDB lint
no longer filters on `master`.

The inherited PCSX2 desktop packaging workflows (Linux AppImage, Windows
and macOS Qt builds and their matrices) and the upstream PCSX2 publication
and maintenance workflows (release tagging, release pipeline and
announcement, controller database updates, PR triage) were removed earlier.
This tree is libretro-only: it has no `PCSX2_qt.sln`, Qt application target
or `unittests` CMake target, so the desktop jobs could not succeed, and the
upstream workflows were guarded by `github.repository == 'PCSX2/pcsx2'` and
never ran here. Their helper scripts under `.github/workflows/scripts` were
removed with them. Upstream history retains all of these files.

`.gitlab-ci.yml` is unchanged and retains its desktop, mobile and webOS jobs.
