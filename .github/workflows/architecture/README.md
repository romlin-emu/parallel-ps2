> This documentation is distributed with [Romlin’s parallel-ps2 fork](https://github.com/romlin-emu/parallel-ps2), derived from [libretro/parallel-ps2](https://github.com/libretro/parallel-ps2). Original project names, authors, and licenses are retained.

# CI Documentation

## Workflows

| Workflow | Runs on | Purpose |
| --- | --- | --- |
| `romlin_release.yml` | Pushes to `romlin`, all PRs, manual dispatch | Builds the Windows and Linux x86_64 libretro cores, runs the native smoke gate and packages zips with `.sha256` files. Calls `tests.yml` at the same revision. |
| `tests.yml` | Called by `romlin_release.yml`; pushes to `master` and `libretroization`; manual dispatch | Plain, ASan and UBSan runs of `tests/run-all.sh`, plus the `tests/ci/check.py` runner checks. |
| `aarch64_build_libretro.yml` | PRs; pushes to `master` and `libretroization`; manual dispatch | Cross builds of the libretro core for Android arm64-v8a and Linux aarch64 (webOS shape, GLES). |
| `linux_build_libretro.yml` | PRs to and pushes on `libretroization`; stars by the repository owner | Plain Linux x86_64 core build. |
| `lint_gamedb.yml` | PRs and pushes to `master` and `romlin` that touch the GameDB or its schema | Validates and format-checks `bin/resources/GameIndex.yaml`. |

## Romlin builds and releases

Only a successful `romlin` push or manual dispatch on that branch, in
`romlin-emu/parallel-ps2`, publishes the latest release; PRs and dispatches
on other refs only validate. PR validation cancels stale runs; publication
is not cancelled. Publication needs both core builds and every `tests.yml`
lane to pass. Archive names, contents and `.sha256` files retain the
existing contract. Linux uses a persistent ccache; Windows builds natively
without extra cache tooling.

## Removed from this fork

The inherited PCSX2 desktop packaging workflows (Linux AppImage, Windows
and macOS Qt builds and their matrices) and the upstream PCSX2 publication
and maintenance workflows (release tagging, release pipeline and
announcement, controller database updates, PR triage) were removed. This
tree is libretro-only: it has no `PCSX2_qt.sln`, Qt application target or
`unittests` CMake target, so the desktop jobs could not succeed, and the
upstream workflows were guarded by `github.repository == 'PCSX2/pcsx2'` and
never ran here. Their helper scripts under `.github/workflows/scripts` were
removed with them. Upstream history retains all of these files.

`.gitlab-ci.yml` is unchanged and retains its desktop, mobile and webOS jobs.
