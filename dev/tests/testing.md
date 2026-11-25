# Testing Guide

## Where new tests belong

Choose the suite by the evidence you need. The same motion can have a unit test for a known
regression and a property test comparing generated cases with Neovim.

| Suite | Location | What it checks |
|---|---|---|
| Unit | `tests/Unit/<feature>/` | A concrete expected result or named regression. |
| Approval | `tests/Approval/` | Reviewed snapshots of production C++ text formats and diagnostics. |
| Property | `tests/Property/` | Semantic invariants over structured inputs, including oracle conformance, optimizer replay, and effort identities. |
| Safety | `tests/Safety/` | Clean failure or bounded behavior for malformed inputs and resource limits at public boundaries. |
| Lua | `tests/lua/` | Plugin lifecycle, session storage, views, and the Lua/FFI boundary in real Neovim. |
| Benchmarks | `tests/Benchmarks/` | Timing and search counters on fixed fixtures. |
| Debug | `tests/Debug/` | Scratch investigations and noisy traces. |

Place a VimCore movement regression in `tests/Unit/Commands/`, an edit primitive beside
[EditPrimitiveTest](../../tests/Unit/Misc/EditPrimitiveTest.cpp), or an optimizer regression
under its optimizer in `tests/Unit/`.

Shared infrastructure lives in `tests/Utils/`. Use subject-and-role filenames such as
`ParserBoundarySafety.cpp`. Give fixture classes a `Test` suffix. A fixture with the same name
as a production type can violate the C++ one-definition rule and silently corrupt memory. Run
`scripts/check-test-fixture-names.sh` to check for collisions.

## Running tests

From the repo root, configure once and build after C++ changes. The wrapper accepts `unit`,
`approval`, `property`, `safety`, and `lua`. The default runs all five. Supply a gtest filter
or a Lua filename to run a focused check.

```bash
cmake -G Ninja -B build -DVIMF_BUILD_TESTS=ON
cmake --build build -j
scripts/vimfy_tests unit 'EditPrimitiveTest.*'
scripts/vimfy_tests approval 'VimDiffApproval.*'
scripts/vimfy_tests lua tests/lua/session/store_invariants.lua
scripts/vimfy_tests all seed
```

For approval changes, inspect the `.received.*` files in `tests/Approval/fixtures/` before
running `scripts/vimfy_tests approval 'VimDiffApproval.*' --approve`. On failure the helper
copies **all** outstanding received fixtures to their approved counterparts before rerunning
the filter. Use `--gtest_list_tests` on a binary under `build/tests/` to find a suite name.

The optional editor helper builds the narrow target and runs the test at the cursor through
the same wrapper.

```lua
local tests = dofile("/path/to/vimficiency/dev/editor/vimfy-test-helper.lua")
vim.keymap.set("n", "<leader>tt", tests.run_gtest_here)
```

## Generated tests and seeds

Property and safety use Google FuzzTest. Let the domain generate the semantic parts of a case
directly, such as lines, cursor, command, and goal. That lets FuzzTest shrink the actual
failure. An inner random loop hides those values. Print the converted case when a check fails.
See [the diff round-trip property](../../tests/Property/DiffStateProperty.cpp).

Local runs explore with a fresh `FUZZTEST_PRNG_SEED`. CI and CTest use a fixed seed. The seed
determines the input stream, while a time budget can change how far the run gets. Use
`--for 500ms`, for example, to set the per-property budget. A bare positive integer in the
wrapper's filter slot sets that budget in seconds.

```bash
scripts/vimfy_tests property 'InterpreterMatchesOracle.*' --for 500ms
FUZZTEST_PRNG_SEED="$printed_seed" scripts/vimfy_tests property 'InterpreterMatchesOracle.*'
```

Set `printed_seed` to the seed from the failing run. The wrapper prints a fixed seed on
failure and leaves exploratory seed output visible. Reduce the case in `tests/Debug/` and pin
it as a unit regression or a `.WithSeeds(...)` corpus input. Explicit corpus seeds also cover
rare inputs that should run every time. For an instrumented Clang build, campaign budgets, and
reproducer files, see [Coverage-guided fuzzing](fuzzing.md).

## NeovimOracle

Compare Vimficiency and embedded Neovim side by side in `tests/Debug/Debug.cpp`. Compare
buffer, cursor, and mode at the first divergent command. The C++ oracle API takes and returns
zero-based positions. Fixtures can reuse an oracle across cases. `simulate()` manages
subprocess restarts automatically. [Oracle implementation](../../tests/Utils/NeovimOracle.cpp)

Use `OracleReplay::matches` for an exact replay and
`OptimizerResultChecks::expectTopResultsReplay` to check a bucket of optimizer results against
the goal. For search traces, configure with `-DVIMF_TRACK_STATES=ON` and run the focused repro
through `build/tests/vimfy_debug`.

## Scoring signature

`ScoringSignature` hashes the effort model and the best costs, outcomes, and stop reasons of
reference search windows. Stats compare scores within one `SCORING_VERSION`. An intended
change to those costs requires updating both the version and the printed `SCORING_SIGNATURE`
in `src/Versions.cppm`. Investigate unexpected changes before updating the signature.

The effort section covers built-in configs, key modifiers, sequence bindings, printable Insert
input, and count penalties. Add a search window when a component can determine the winning
cost there and existing windows miss it. Keep cases below the node caps and require agreement
between Debug and Release. The test pins costs and outcomes while allowing equivalent winning
sequences. [Scoring signature test](../../tests/Unit/Journal/ScoringSignatureTest.cpp)

## Lua tests

The main batch shares one headless Neovim and resets plugin state between files. The harness
runs `simulate/integration.lua` and `capture/atom_stream.lua` in separate processes for their
async window and real-input behavior. Shared Explore flow helpers live in
`tests/lua/explore/_helpers.lua`. [Lua harness](../../tests/lua/run.sh)

`tests/lua/proto_schemas.lua` hashes the messages and enums reachable from each stored format.
For an additive change, update the printed `*_SCHEMA` in `src/Versions.cppm`. Also bump the
corresponding `*_FORMAT` when a field changes meaning.

## Benchmarks

Benchmark fixtures use a separate `SeedManager`. Fixed seeds are the default.
`VIMFY_SEED_MODE=random` records seeds in `tests/.last_seeds.txt`, and
`VIMFY_SEED_MODE=replay` reuses them. For comparable timings, run a Release build and let
Google Benchmark control the measured iterations.
[Build and dashboard publishing](../ci-and-benchmarks.md)

```bash
./build-release/tests/vimfy_benchmarks --benchmark_repetitions=9 \
  --benchmark_enable_random_interleaving=true --benchmark_out=bench.json --benchmark_out_format=json
```
