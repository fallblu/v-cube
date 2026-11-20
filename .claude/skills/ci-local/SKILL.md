---
name: ci-local
description: Run the checks a pull request runs in CI, locally - clang-format check, a -Werror debug build with ctest, and optionally the ASan + UBSan build. Use before pushing.
disable-model-invocation: true
argument-hint: "[asan]"
---

Run the pull-request CI checks from `.github/workflows/ci.yml` locally, from
the repository root. These builds use their own folders (`build/ci`,
`build/ci-asan`), so the user's `build/debug` cache isn't touched.

Arguments: $ARGUMENTS

## Steps

Run each step even if an earlier one fails, so the report covers everything.

1. **Format** (the CI `Format` job; clang-format must be 21.1.8):
   ```bash
   clang-format --version
   git ls-files -z '*.cpp' '*.hpp' | xargs -0 clang-format --dry-run --Werror
   ```
   If the version isn't 21.1.8, say so: other versions format some
   constructs differently, so the result may not match CI.

2. **Build and test with warnings as errors** (the `Linux GCC` job):
   ```bash
   cmake --preset debug -B build/ci -DVCUBE_WARNINGS_AS_ERRORS=ON -DVCUBE_USE_CCACHE=ON
   cmake --build build/ci
   QT_QPA_PLATFORM=offscreen ctest --test-dir build/ci --output-on-failure
   ```

3. **Only if the arguments include `asan`** (the `Linux ASan + UBSan` job):
   ```bash
   cmake --preset asan -B build/ci-asan -DVCUBE_WARNINGS_AS_ERRORS=ON -DVCUBE_USE_CCACHE=ON
   cmake --build build/ci-asan
   QT_QPA_PLATFORM=offscreen ctest --test-dir build/ci-asan --output-on-failure
   ```

Builds can take several minutes. Use a long timeout, or run them in the
background.

## Report

Give a short table with each step marked pass or fail. For each failure, show:
- the files a format check flagged, and the command that fixes them
  (`clang-format -i <files>`)
- the first compiler error or warning, with `file:line`
- the names of the failing tests, with the assertion output

Don't fix anything unless the user asks. Remind them that MSVC, Clang and
Qt 6.5 run only in CI.
