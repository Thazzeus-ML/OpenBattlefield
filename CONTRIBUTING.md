# Contributing to OpenBattlefield

OpenBattlefield is an open-source, human-contributed, AI-assisted engine research project. Working,
verified systems take precedence over architectural sophistication; correctness over performance
claims; a small functioning implementation over a large collection of incomplete modules.

## Where to start

* Read `Docs/architecture.md`, then `Docs/status.md` for what is and is not verified.
* Build and run the tests (`Docs/building.md`). No Unreal installation is needed for the core.
* Good first contributions: the first in-engine build report, dirty-region-limited remeshing,
  greedy meshing, a sparse chunk representation benchmarked against the dense baseline.
* Discussion and coordination also happen on Overflow:
  https://overflow.nitjsefni.eu/repositories/new

## Ground rules

1. **Core stays engine free.** Nothing under `Source/OpenBattlefieldCore` may include an Unreal
   header (except the guarded module file). CI compiles it standalone with `-Werror`.
2. **Determinism is a contract.** Authoritative code uses integer arithmetic, canonical ordering and
   canonical serialization. If you change the generator or any serialized format, bump the relevant
   version constant and update the pinned hash in the same commit (`Docs/determinism.md`).
3. **Tests accompany behaviour.** Add a test case for new behaviour and a regression test for every
   bug fix. Tests are dependency free (`Tests/Core/ObTest.h`).
4. **Performance claims come with data.** Use the benchmark harness, same machine, same preset,
   baseline and candidate commits named, `Tools/bench_compare.py` output in the PR. Never present
   hypothetical or cross-machine numbers as measurements.
5. **Never claim what was not run.** If you could not compile or run something (for example the
   Unreal modules), say so in the PR and in `Docs/status.md`.
6. **Thin Unreal layers.** New logic goes into an engine-agnostic library first.
7. **Server authority.** Nothing accepts client-supplied outcomes, costs or affected sets.

## Workflow

* Branch from `main`, keep PRs focused, fill in the PR template.
* Format with `clang-format` (config in repo). CI enforces it for `Source/`, `Tests/`, `Benchmarks/`.
* Commit messages: imperative subject line, body explains why.
* All contributions are licensed under Apache-2.0 (see `LICENSE`). By submitting a contribution you
  agree to license it under the same terms. Do not contribute code you do not have the right to
  license, including Unreal Engine source.

## AI-assisted contributions

AI assistance is welcome and expected. The human submitting a PR is responsible for having read,
understood, built and tested the change, and for the accuracy of every claim in it. Mark generated
code you have not personally verified as such.

## Reporting security issues

See `SECURITY.md`.
