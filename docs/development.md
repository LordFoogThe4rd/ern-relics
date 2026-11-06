# Development

Run commands from the repository root in PowerShell 7.4 or newer on 64-bit Windows.

## Set up

Install Git and [uv](https://docs.astral.sh/uv/getting-started/installation/). Import
[.vsconfig](../.vsconfig) into the Visual Studio Installer for the C++ build tools,
CMake 3.25 or newer, Ninja, AddressSanitizer, and Windows SDK components.

Install clang-tidy matching the clang-format version in
[pyproject.toml](../pyproject.toml). The scripts look for it in Visual Studio and
on `PATH`, so a standalone [LLVM installation](https://github.com/llvm/llvm-project/releases)
works too.

```powershell
./tools/bootstrap.ps1
```

Bootstrap prepares the pinned native dependencies and Python tools. uv manages
Python, and the build scripts initialize the native toolchain.

## Build and check

Build a runnable development package:

```powershell
./tools/build.ps1 -Label dev
```

The build runs CTest and checks the packaged DLL in the synthetic DX12 host.
Output goes to `dist/ern-relics-<version>-dev/`. Reuse that directory while
iterating. Keep installed configuration, presets, and journals elsewhere, since a
rebuild replaces the package. Preserve versioned release packages and ZIPs.
Both `build.ps1` and `check.ps1` default to the `dev` label. An explicit empty
label creates an unlabelled package but refuses to replace an existing versioned
release directory.

Before finishing a code change, format the source and run the full check:

```powershell
./tools/format.ps1 -Fix
./tools/check.ps1 -Label dev
```

This adds lint, formatting checks, and static analysis to the build and tests.
Use `-Jobs <count>` to limit parallelism. For AddressSanitizer, run
`./tools/build.ps1 -Preset asan`.

The [CI workflow](../.github/workflows/ci.yml) runs for pull requests, pushes to
`main`, and manual runs. Pushes to other branches rely on PR checks to avoid
duplicate builds. New Windows jobs cancel older jobs on the same PR or branch,
including `main`. Documentation-only runs leave existing Windows checks running.

- Every run checks Python lint, Python and C++ formatting, repository layout, and
  documentation links on Linux.
- Changes limited to root Markdown files or Markdown under `docs/` skip Windows.
  All other changes run the Release build, compiler warnings, CTest, synthetic
  asset regressions, and the packaged DLL's DX12 host checks on Windows.
- For clang-tidy and AddressSanitizer, select **CI > Run workflow** and leave
  **Include clang-tidy and AddressSanitizer checks** enabled. The two Windows
  jobs run in parallel. Disable that option to run ordinary push checks manually.

Change selection compares the complete push or PR diff, including deleted files
and both sides of renames. Missing history and manual runs select Windows checks.
Unknown paths also select Windows checks. CI tests this selection with local Git
fixtures through `uv run --locked python -m tests.tooling.ci_changes_tests`.

The [shared native setup](../.github/actions/setup-native/action.yml) caches uv
packages, native dependency checkouts keyed by `sources.lock.json`, and the pinned
LLVM analyzer and builtin headers. Bootstrap still validates cached dependency
origins, commits, and clean working trees. Ordinary builds use
`./tools/bootstrap.ps1 -SkipAnalysis` and do not install LLVM. Builds and tests
always run from fresh output directories. CI also caches compiler results with
pinned [sccache](https://github.com/mozilla/sccache), using a 512 MiB disk cache
per job type. Each commit saves its own cache and can restore the latest cache
from the same job type. sccache checks source, compiler, headers, and options
before reusing an object. CI uses a stable build path on each fresh runner and
reports cache hits and misses in the setup action's final step. Local bootstrap
and `check.ps1` keep their full checks by default.

The production DLL under `<build-directory>/runtime/` and the integration DLL
beside the host share compiled runtime objects. UI tests link the same UI library
as the DLLs. The integration DLL alone adds test fixture exports. CTest checks
`compile_commands.json` to reject any source compiled more than once:

```powershell
uv run --locked python -m tools.checks.compilation --build-directory '<build-directory>'
```

To use a locally installed sccache, set `CMAKE_C_COMPILER_LAUNCHER` and
`CMAKE_CXX_COMPILER_LAUNCHER` to `sccache` before configuring. Debug and sanitizer
builds embed compiler debug information so objects can be cached with MSVC.

For documentation-only changes:

```powershell
uv run --locked python -m tools.checks.layout
```

Use the [architecture overview](architecture.md) to find the relevant code. Add
new `.cpp` files to `CMakeLists.txt` or `tests/CMakeLists.txt`, and place tests in
the matching group under `tests/`. Check visual changes locally.

## Try the editor

To preview the overlay without Nightreign, keep a build directory and launch the
synthetic host:

```powershell
$ernBuild = Join-Path $env:TEMP 'ern-relics-dev'
./tools/build.ps1 -Label dev -BuildDirectory $ernBuild
& (Join-Path $ernBuild 'ern-relics-host.exe')
```

That directory also keeps the test executables and `compile_commands.json` for
focused tests or editor tooling. Use temporary locations for one-off work.

For in-game testing, follow the [installation instructions](../README.md#install).
Back up saves and use an offline profile. The test helper launches a package
with a separate `ern-relics-test.sl2` save:

```powershell
./tools/game/launch-test.ps1 -PackageDirectory '<package-directory>'
```

Pass `-Me3Exe` and `-GameExe` if they are installed outside the default locations.
For editing changes, exercise Apply and Undo, then save and reload to check
persistence. Other diagnostics are in [tools/](../tools/).

## Dependencies

Keep dependency changes with [sources.lock.json](../sources.lock.json) or
[uv.lock](../uv.lock). After changing Python dependencies, run `uv lock` and rerun
bootstrap. Update [ThirdPartyNotices.txt](../ThirdPartyNotices.txt) when distributed
code or attribution changes.

When changing the clang-format pin, update the matching LLVM installer checksum
in [the CI setup script](../tools/ci/setup-llvm.ps1).

Keep game assets, saves, extracted data, and proprietary decoders out of version
control and distributed packages.

## Releases

Use [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/):
`fix:` increments the patch version, `feat:` increments the minor version, and
`feat!:` or a `BREAKING CHANGE:` footer increments the major version. Use `docs:`,
`chore:`, or `ci:` for changes that do not require a release.

[Release Please](https://github.com/googleapis/release-please-action) is configured
in [release-please-config.json](../release-please-config.json) and maintains a
release PR for `main`. In the repository's **Settings > Actions > General >
Workflow permissions**, enable **Allow GitHub Actions to create and approve pull
requests**. Keep the default workflow permissions read-only. The release job
requests its own write permissions and uses `GITHUB_TOKEN`, so no personal access
token is required.

GitHub requires approval before running CI on PRs created or updated with
`GITHUB_TOKEN`. On the release PR, select **Approve workflows to run** before
merging. You can also run **CI > Run workflow** on the release PR's branch. See
[GitHub's workflow trigger rules](https://docs.github.com/en/actions/concepts/security/github_token#when-github_token-triggers-workflow-runs).

Merging the release PR creates a `v<version>` tag and GitHub Release. The
[Release workflow](../.github/workflows/release.yml) runs on pushes to `main` or
manual runs. When it creates a release, it builds and verifies the exact
release commit, including clang-tidy and AddressSanitizer, then attaches
`ern-relics-<version>.zip` and its `.zip.sha256` checksum. Ordinary pushes only
update the release PR. Packaging runs when Release Please creates a release or
when a manual run selects an existing release tag.
New pushes do not cancel an active release run. CI has read-only
repository access and runs independently; release jobs have write access.

To build a release locally:

```powershell
./tools/release.ps1
```

Packaging stages files inside its output directory, so the final moves stay on
the destination volume even when Windows TEMP is on a different drive. The
packaging regression test keeps its input in TEMP and its output in the checkout
to exercise that layout on hosted Windows runners.

The command runs the full check and creates the release package, ZIP, and checksum
in `dist/`. Existing release output is never replaced. Use
`-OutputDirectory <directory>` for a separate candidate.

Complete in-game acceptance before publishing. Upload the ZIP to Nexus Mods.
Users can import the same ZIP into ME3.

## Release attestations and rebuilds

Release builds use separate checkouts for the selected source tag and the workflow
commit's tooling. Source compilation, tests, clang-tidy, and AddressSanitizer run
from the unmodified tagged checkout. Sanitizer compilation bypasses sccache so
historical tags using shared MSVC `/Zi` PDBs keep their original compiler flags
without cache-wrapper collisions. The workflow's packager can repair old release
tooling without changing the tagged code. Before attesting, the source validator
checks both checkout commits and cleanliness, dependency pins and origins, ZIP
contents, the checksum, and equality with the checked production package.

The release publishes two GitHub attestations for the ZIP, checksum, and source
record. The standard build attestation identifies the GitHub-hosted workflow and
its commit. The custom predicate
`https://github.com/demondobf/ern-relics/attestations/release-source/v1` records the
actual source tag and commit separately from that tooling commit. The versioned
`.source.json` file contains the same claim. The release also includes downloadable
Sigstore bundles for offline verification. Neither kind of attestation is a claim
that the program is free of defects or has passed in-game acceptance.

For an existing release, select **Release > Run workflow**, choose the branch or
commit containing the repaired tooling, and enter its tag, such as `v1.0.1`.
Enable **Replace existing assets** only when intentionally replacing that release's
files. The tag is never moved. New bytes and attestations must pass verification
before upload. A CI rebuild may change ZIP and DLL hashes; use the newly published
checksum. Candidate artifacts are retained for 14 days, including after an upload
failure. Repository attestations require a public repository or an eligible
GitHub Enterprise Cloud plan for private repositories.

After downloading a release, verify its build identity with the GitHub CLI:

```powershell
gh attestation verify ern-relics-1.0.1.zip --repo demondobf/ern-relics --signer-workflow demondobf/ern-relics/.github/workflows/release.yml --deny-self-hosted-runners
```

To inspect the signed source identity as JSON:

```powershell
gh attestation verify ern-relics-1.0.1.zip --repo demondobf/ern-relics --signer-workflow demondobf/ern-relics/.github/workflows/release.yml --predicate-type https://github.com/demondobf/ern-relics/attestations/release-source/v1 --format json
```

Compare `verificationResult.statement.predicate.sourceCommit` with the release
tag, and `toolingCommit` with the workflow revision you trust. Add
`--signer-digest <tooling-commit>` to require that exact workflow commit. The CLI's
`--source-ref` and `--source-digest` options refer to the workflow's execution
context; they do not replace checking the separately attested application tag.
For offline bundle verification, add
`--bundle ern-relics-1.0.1.build-provenance.sigstore.json` to the first command.
See [GitHub's attestation documentation](https://docs.github.com/en/actions/concepts/security/artifact-attestations)
for the trust model and CLI requirements.
