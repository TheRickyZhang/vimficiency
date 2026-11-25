# CI/CD and Benchmark Dashboard

Hosted CI checks correctness. A self-hosted Linux runner measures performance on fixed
hardware so changes between commits are interpretable.

## Correctness CI

On pull requests and pushes to `main`, the correctness jobs use the GCC container, Ninja, and a
pinned CMake. Dependency lint checks module edges and in-class inline definitions. Lua lint
uses Neovim nightly's runtime types. Doc lint checks that frontmatter titles match chapter
headings. [CI workflow](../.github/workflows/bench.yml)

PRs that affect the native build, Lua FFI, or release tooling also build the library and journal
scorer and run both smoke tests on Linux x86_64, Linux arm64, and macOS arm64. Pushing a
`v*` tag publishes the release after all three builds pass. [Release workflow](../.github/workflows/release.yml)

| Build | What it verifies |
|---|---|
| Debug, state tracking on | Compiles every target, including assertions and `vimfy_explore`, and runs unit and approval tests. |
| Release, state tracking off | Runs the full correctness gate with a fixed FuzzTest seed. The C++ oracle uses pinned Neovim, while `VF_NVIM` selects nightly for the Lua/FFI suite's CmdAtom tests. |

Each job caches its whole build tree because ccache cannot reuse the C++ module compilations.
Keys include job, compiler version, ref, and SHA. On a miss the job tries the current ref,
then `main`. The cache records Git blob IDs so a changed file is rebuilt even when its commit
predates the cached build. A successful build is saved before tests run.
[Build cache script](../scripts/ci-build-cache.sh)

The hosted docs job publishes `web/docs/dist/` to `gh-pages/docs/` independently of the
benchmark runner. If a push races with a dashboard update, it reapplies the docs to the new
tip. Changes to the help source on `main` also trigger a PR regenerating
`doc/vimficiency.txt`. [Docs deploy](../.github/workflows/docs-deploy.yml) ·
[Vim help generation](script-usage.md)

## Running benchmarks

For a local comparison, run the benchmark binary in a **Release** tree. Debug timings can be
dominated by container access and differ substantially from the dashboard.

```bash
cmake -G Ninja -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DVIMF_DEBUG=OFF \
  -DVIMF_BUILD_TESTS=ON -DVIMF_TRACK_STATES=OFF
cmake --build build-release --target vimfy_benchmarks
./build-release/tests/vimfy_benchmarks --benchmark_filter='EditOpt.*'
```

### Published benchmarks

Pushes to `main` trigger `bench-main.yml`. It serializes runs on the machine labeled
`vimficiency-bench`. Concurrent local workloads can inflate a point. The runner switched from
Linux to a MacBook at `5c4a5be5` and back to Linux in November 2026. Account for those
hardware changes when comparing historical timings.
[Runner workflow](../.github/workflows/bench-main.yml)

The cache defaults to `~/.cache/vimficiency-bench/`. The detached `repo/` worktree builds the
requested SHA. The `gh-pages/` worktree records and publishes the result. A manual run uses
`flock`, writes to `logs/` and records its exit code, SHA, branch, and log path in
`last-status`. Under Actions, read the workflow log.

1. Build Release with tracking off and run the Edit, Motion, and Composition benchmark suites
   on fixed fixtures. The Actions summary compares them with the stored parent SHA when
   available.
2. On `main`, run `vimfy_explore` in a tracking-enabled tree, time Unit and Approval, and
   collect Property and Safety coverage.
3. Build the dashboard, ingest the results, prune histories to 100 entries, and push the
   updated assets and data to `gh-pages`. The benchmark publisher owns the dashboard root and
   datasets. The docs publisher owns `docs/`.
   [Benchmark publisher](../scripts/bench-local-run.sh)

### Re-running manually

To publish a missing point, run the following with the desired commit and branch name. This
**appends** to the shared timeline, including when the SHA already has an entry. Passing a
feature branch also publishes its measurements and skips the main-only extras. Use the Release
binary above for a private comparison.

```bash
scripts/bench-local-run.sh <sha> <branch>
```

## Self-hosted runner setup (one-time)

Steps to register the dev Linux machine as the runner backing
`bench-main.yml`. Run as your normal user; `svc.sh` needs `sudo`.

1. **Deps.** `ccache cmake ninja neovim jq bun gcc` as system packages (on Arch:
   `pacman -S ccache cmake ninja neovim jq bun gcc`). Nothing is installed by the
   workflow.

2. **Register the runner.** In the repo on GitHub: Settings → Actions →
   Runners → New self-hosted runner → Linux x64. The page gives a download +
   `./config.sh` command with a registration token (1hr expiry). When it
   asks for labels, enter `vimficiency-bench` so the workflow's
   `runs-on: [self-hosted, Linux, vimficiency-bench]` matches. Extract
   outside the repo (e.g. `~/actions-runner`).

3. **Install as a systemd service** so it auto-starts on boot:

   ```bash
   cd ~/actions-runner
   sudo ./svc.sh install $USER
   sudo ./svc.sh start
   sudo ./svc.sh status
   ```

   The service runs with the login user's `PATH` from systemd's default
   environment, not a shell profile; `/usr/bin` is on it, which is where the
   pacman deps live.

4. **Verify.** The runner appears as "Idle" in the GitHub Settings page.
   Trigger a test run from the Actions tab (`bench-main` → Run workflow).

### Registration lifetime

GitHub deletes a self-hosted runner that has not connected for 14 days;
this is not configurable. The clock resets whenever the service connects,
so a machine that boots at least once every two weeks stays registered.

After a longer gap, jobs sit at "Waiting for a runner" and
`gh api repos/TheRickyZhang/vimficiency/actions/runners` shows no runner.
The download and systemd service survive, so only the registration needs
repeating:

```bash
cd ~/actions-runner
sudo ./svc.sh stop
./config.sh remove
./config.sh --url https://github.com/TheRickyZhang/vimficiency --labels vimficiency-bench --unattended --token "$(gh api -X POST repos/TheRickyZhang/vimficiency/actions/runners/registration-token --jq .token)"
sudo ./svc.sh start
```

If `./config.sh remove` asks for a token, pass a fresh one from
`actions/runners/remove-token`. Registration is per repo URL, so a repo
rename or transfer is another reason to re-check that the runner is listed.

The queue limit is separate: a job waiting longer than 24h is cancelled,
so pushes made while the machine is off for over a day lose their point
(see "Re-running manually").

A first build fetches dependencies and warms the cache. Manual and service runs share the
cache when they use the same account.

## Data pipeline

`gh-pages/{edit,motion,composition}/data.json` stores benchmark runs by suite with commit
metadata, dates, and measurements. The corresponding `explore.json` keeps full explored states
for the latest run and summaries for older runs. Both ingestion scripts can read the legacy
JavaScript format. [Benchmark ingestion](../scripts/bench-data.ts) ·
[Explore ingestion](../scripts/explore-data.ts)

### Test timing data (`tests/data.json`) and coverage (`tests/coverage.json`)

On `main`, the local deploy runner records `--gtest_output=json` for all four
test binaries, but splits them by what their wall-time means:

- **Unit + Approval** are deterministic, so `scripts/convert-gtest-timing.ts`
  turns them into Google Benchmark-shaped data under the `Tests` suite —
  `Tests/Binaries/{Unit,Approval}` (per-binary total) and
  `Tests/Cases/{Unit,Approval}/<Suite>.<Case>` (per-case). Ingested into
  `tests/data.json` as a per-commit time series. The home page charts the two
  per-binary totals as one multi-line chart; each suite page shows its
  binary-total trend on top plus per-case charts (no per-suite total).
- **Property + Safety** run FuzzTest in unit mode, where the configured time budget
  makes elapsed time a poor performance measure (see `dev/decisions.md`). They are run
  with `FUZZTEST_FUZZ_FOR=0` purely to enumerate cases, and
  `scripts/convert-gtest-coverage.ts` writes a latest-snapshot
  `tests/coverage.json` (suites → cases + pass status + counts). Their dashboard
  pages show coverage, not a duration trend.

The historical `tests/data.json` was migrated to this shape once via
`scripts/migrate-tests-data.ts` (drops `Tests/Total/All`, `Tests/Suites/*`, and
property/safety timing; upgrades legacy unit-only entries). That script is
single-use and may be deleted.

Unrelated to the dashboard but in the same area: the fuzz binaries themselves
are fast now because `tests/patches/fuzztest-watchdog.patch` (applied via the
`PATCH_COMMAND` on the pinned `fuzztest` `FetchContent_Declare`) defuses the 1s
per-test watchdog teardown. `scripts/vimfy_tests` exposes `--for <dur>` to set
each FUZZ_TEST's `FUZZTEST_FUZZ_FOR` budget, which now shortens runs for real.

## Benchmark dashboard

The React dashboard's TanStack Router loaders read the published JSON directly. For local
development, datasets live under `web/dashboard/public/{edit,motion,composition,tests}/`.
[Route loaders](../web/dashboard/src/router.ts) ·
[Dashboard scripts and dependencies](../web/dashboard/package.json)

```bash
cd web/dashboard
bun install --frozen-lockfile
bun run dev
```

Use `bun run build` to type-check and create `dist/`, and `bun run preview` to inspect the
production build. The publisher installs the dashboard entry point as both `index.html` and
`404.html` for client-side routes. Static assets go under `assets/`.
