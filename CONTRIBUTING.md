# Contributing to GraphVis

GraphVis is a scientific plotting program. A figure it draws may end up in
somebody's paper, so the standard here is a little unusual: **a change is not
finished until something measures it.**

## The one rule worth reading

*A check that cannot run is not a check that passed, and silence is not a pass.*

Most of the defects found in this project were found by a test that measured the
wrong thing, or by a guard that matched a name rather than a statement. Several
were found only because a check refused to accept two identical pictures. When
you add a check, make it fail first — a check you have never seen fail is a
check you have not yet written.

## Before you open a pull request

Run what the project runs. None of this needs a full build except the last.

```bash
# 1. The Python suites: analysis operations, the de-renderer, and the
#    reachability guards that read the C++ sources as text. About a minute.
cd services/python && python -m pytest -q

# 2. The passive audit: static, read-only, and it self-tests first. A pass
#    where the self-test does not say N/N is a pass you cannot rely on.
python tools/passive_audit.py --selftest
python tools/passive_audit.py

# 3. The generated help must match the code it is generated from.
python tools/make_help.py --check
```

The engine sweep, the regression and property checks, and the 440-figure gallery
comparison need a built backend. `BUILD-AND-CHECK.bat` (Windows) runs the whole
sequence and writes `build-reports/REPORT.md`. On Linux, the release workflow in
`.github/workflows/linux-release.yml` is the reference recipe.

## What a good change looks like

- **It says why, in the code.** Comments here explain the defect a line exists to
  prevent, not what the line does. If you fix something subtle, leave the next
  person the reasoning — including what you tried that did not work.
- **It carries its own check.** A fix without a test that fails before it is a
  fix that comes back.
- **It does not widen a count to make a check pass.** If a census starts
  reporting 7 where it reported 6, explain the seventh.
- **The 440-figure gallery stays byte-identical** unless the change is *meant* to
  alter a figure, in which case say which figures and why.

## Building

`docs/INSTALL_AND_BUILD.md` is the full account. In short: CMake, a C++17
compiler, Qt 6 and VTK through vcpkg (`vcpkg.json` pins the baseline), and a
Rust toolchain for the native core (`rust-toolchain.toml` pins the version).
`native/Cargo.lock` is committed and the CI refuses to build without it.

## Licence

GraphVis is GPLv3. By contributing you agree your work is released under the
same terms. That is deliberate: it keeps the program free for everyone, and it
means nobody — including the original author — can take it closed.
