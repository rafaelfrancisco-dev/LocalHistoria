# 11 · One-time clang-format of the tree

**Status: done (2026-09-18).** clang-format 21. Over-long raw JSON literals split into adjacent `R"(…)"` pieces (contents verified byte-identical); clang-format split long ordinary strings, sorted includes and added `// namespace` comments. No multi-statement lines remained apart from single-statement lambdas. `make format` / `make format-check` added.

## Goal
Apply the checked-in `.clang-format` (LLVM, IndentWidth 4, ColumnLimit 100) to all C++ so later plans edit
readable code. Behaviour-only-preserving; no logic changes in this commit.

## Evidence
Longest lines: `src/providers.cpp` 930, `src/main.cpp` 596, `src/status.cpp` 569, `src/store.cpp` 496,
`src/world.cpp` 407 characters. Dense one-liners e.g. `src/main.cpp:460`, `src/world.cpp:373`,
`src/store.cpp:165`, most of `tests/world_tests.cpp` and `tests/context_tests.cpp`.

## Steps
1. `brew install clang-format` (or use the one in the LLVM toolchain). Check `clang-format --version` ≥ 17.
2. Run `clang-format -i src/*.cpp include/localhistoria/*.h tests/*.cpp tools/*.cpp`.
3. clang-format will not split statements joined on one line by `;`. Manually break lines that still hold more
   than one statement (grep for `;[^ ]` and `;} ` patterns after formatting), especially in `world.cpp`
   `compileOperations`, `store.cpp` `eventLog`/`campaignContext`, `main.cpp` routes, and the tests. Keep
   the lambdas; just put one statement per line.
4. Build, run `make test` and `python3 tests/integration.py`. `git diff --stat` should touch only whitespace
   and line breaks: verify with `git diff -w --stat` being (near) empty apart from manual statement splits.
5. Add a `format` target to the `Makefile` (`clang-format -i …`) and a `format-check` target
   (`clang-format --dry-run --Werror …`) and mention both in README "Verification".
6. Commit alone: "Apply clang-format to the C++ sources (no behaviour change)".

## Acceptance
All suites pass; no line > 100 columns in `src`, `include`, `tests`, `tools` (`awk 'length > 100' …` prints
nothing); one statement per line.
