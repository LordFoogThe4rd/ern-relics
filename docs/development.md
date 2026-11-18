# Development

Run commands from the repository root in PowerShell 7.4 or newer on 64-bit Windows.
The [tools guide](../tools/README.md) describes the directory layout and entry points.

## Set up

Install Git and [uv](https://docs.astral.sh/uv/getting-started/installation/).
Use Visual Studio 2026, including the standalone Build Tools edition, with
MSVC toolset 14.51, compiler 19.51. Import [.vsconfig](../.vsconfig) into Visual
Studio Installer for the C++ build tools, CMake, Ninja, AddressSanitizer, and
Windows SDK 10.0.26100 components. Builds use CMake and Ninja from that installation,
requiring CMake 3.25 or newer and Ninja 1.11 or newer.
AddressSanitizer is needed only for the `asan` preset. Ordinary shell activation
requires the compiler, SDK, CMake, and Ninja; CMake rejects an `asan` build when
the selected compiler lacks its sanitizer runtime. Doctor reports ASan separately
while checking the full development environment.
Install [7-Zip](https://www.7-zip.org/) to extract LLVM. The scripts
find 7-Zip on `PATH` or in its standard `Program Files` directory.

Open a new terminal after installing prerequisites. Run these commands in order,
one at a time, and resolve failures before continuing:

```powershell
uv sync --locked
uv run --no-sync python -m tools.setup.llvm
uv run --no-sync python -m tools.setup.sources
./tools/setup/doctor.ps1
```

Python setup comes first because the LLVM and native-source commands use that
environment. Subsequent commands use `--no-sync` so they do not install dependencies
implicitly. Doctor can run before setup to report missing prerequisites.

Each command has one job:

- `uv sync --locked` prepares managed Python and locked Python packages in `.venv/`.
  The Python patch version is pinned in `.python-version` for local builds and CI.
- [LLVM setup](../tools/setup/llvm.py) installs pinned clang-tidy and builtin
  headers under `.tools/llvm/<version>/`. It checks the archive checksum and
  reuses a matching installation. It needs 7-Zip, with no administrator access
  or PATH changes. Builds use only this repository-managed LLVM installation.
- [Native-source setup](../tools/setup/sources.py) fetches the Git commits in
  `sources.lock.json` into `third_party/`. It checks origins and rejects local
  changes before switching commits. It needs Git and the prepared Python
  environment, without Visual Studio or LLVM.
- [Doctor](../tools/setup/doctor.ps1) checks the installation and reports repair steps.
  It uses `uv sync --check --locked --offline --no-python-downloads --no-cache`
  to check Python dependencies without changing the environment or lockfile,
  accessing the network, or writing to the persistent uv cache.
  If `.venv/` has not been initialized by uv, doctor reports it without creating
  environment files. It checks the repository's `.venv/` even when the caller
  has selected another uv environment.
  A small PowerShell bootstrap reports missing uv or Python. When the prepared
  interpreter exists, doctor invokes it directly with bytecode writes disabled
  to reuse the installers' LLVM, 7-Zip, and Git validation. It never runs either
  installer. Detailed dependency checks wait until Python has been prepared.

Local development and CI run the same setup commands. Rerun the specific command
when its dependencies change. Doctor checks tool versions, all required Visual
Studio components in one installation, Python packages against `uv.lock`, LLVM,
and native checkout origins, commits, and cleanliness. A successful result checks
dependencies only; run the full check below to verify compilation and tests.

If `uv` is installed but unavailable, run `Get-Command uv -All` and check whether
its install directory is in this process's `PATH`. A running terminal or parent
application can retain an older PATH even after the Windows user PATH changes.
Restart the parent application or invoke uv by its full path. Doctor recognizes
uv's default user installation when it is absent from PATH.

For a build without clang-tidy, omit LLVM setup and 7-Zip. The full check needs
the analyzer. Keep `.tools/` and `third_party/` outside distributed packages.

## Native build and tests

After installing dependencies, activate the Windows compiler and bundled CMake
and Ninja once per PowerShell session:

```powershell
./tools/setup/shell.ps1
```

This changes only the current process environment. It selects the x64 compiler
from the MSVC toolset pinned in `.vsconfig`, plus Windows SDK 10.0.26100, and keeps
the caller directory unchanged. Local builds, CI, and releases use this same
selection. The pin follows a stable toolset supplied by the hosted Windows runner
so CI does not install a separate compiler. Newer defaults do not override the pin. Servicing patches
within the selected toolset are allowed, so matching toolsets do not guarantee
identical package hashes.

After changing toolsets, reconfigure each existing build directory once with
`cmake --fresh --preset release`, replacing `release` with `debug` or `asan` as
needed. This clears CMake's cached compiler selection without replacing packages
in `dist/`. CMake rejects a compiler from another toolset.

Use CMake and CTest directly:

```powershell
cmake --list-presets=all
cmake --preset release
cmake --build --preset release
ctest --preset release
```

The `release`, `debug`, and `asan` presets use separate, reusable directories
under `.build/`. To configure, build, and test in one command:

```powershell
cmake --workflow --preset release
```

Native workflows compile the project and run CTest, including the synthetic host.
Packaged DLL validation and static analysis run through the combined checks below.

## Formatting and combined checks

Use the pinned tools directly for individual operations:

```powershell
uv run --no-sync ruff check .
uv run --no-sync ruff format .
uv run --no-sync clang-format -i src/relics/relic_validator.cpp
```

Pass the C++ files you want to format to clang-format. Use `--dry-run --Werror`
instead of `-i` to check their formatting. `uv check` runs Python type checking;
`uv format` runs Python formatting with its own Ruff version selection. The
commands above use the versions in `uv.lock`.

For all Python/C++ formatting followed by the full project check:

```powershell
uv run --no-sync python -m tools.format --fix
uv run --no-sync python -m tools.check
```

The full check adds Python lint and static analysis to the build, tests, and
packaged DLL verification. Use `--jobs <count>` to limit parallelism.

## Build a development package

Build a runnable development package:

```powershell
uv run --no-sync python -m tools.build.package
```

The build runs CTest and checks the packaged DLL in the synthetic DX12 host.
Output goes to `dist/ern-relics-<version>-dev/`. Reuse that directory while
iterating. Keep installed configuration, presets, and journals elsewhere, since a
rebuild replaces the package. Preserve versioned release packages and ZIPs.
Build commands always create development output ending in `-dev`. Debug builds
use `ern-relics-<version>-debug-dev/`. Only release commands create frozen
versioned packages. Release versions cannot use the reserved `-dev` suffix.

The Python package builder uses the activated Windows shell and the same
`.build/<preset>/` directory as native commands. It verifies the production DLL
before replacing development output. Full checks also finish static analysis
before publishing. It does not change the caller directory or environment.
Use `--package-directory <new-directory>` to write a separate checked candidate;
an existing candidate is never replaced. For AddressSanitizer, run
`cmake --workflow --preset asan` from the activated shell, or
`uv run --no-sync python -m tools.build.package --preset asan`.

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
LLVM analyzer and builtin headers. Native-source setup validates cached dependency
origins, commits, and clean working trees. Ordinary builds skip LLVM setup.
Local checks and native presets reuse `.build/`; CI supplies fresh runner paths.
CI also caches compiler results with
pinned [sccache](https://github.com/mozilla/sccache), using a 512 MiB disk cache
per job type. Each commit saves its own cache and can restore the latest cache
from the same job type. sccache checks source, compiler, headers, and options
before reusing an object. CI uses a stable build path on each fresh runner and
reports cache hits and misses in the setup action's final step.

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
uv run --no-sync python -m tools.build.package --build-directory $ernBuild
& (Join-Path $ernBuild 'ern-relics-host.exe')
```

That directory also keeps the test executables and `compile_commands.json` for
focused tests or editor tooling. Use temporary locations for one-off work.

For in-game testing, follow the [installation instructions](../README.md#install).
Back up saves and use an offline profile. The test helper launches a package
with a separate `ern-relics-test.sl2` save:

```powershell
uv run --no-sync python -m tools.game.backup_saves --save-directory '<save-directory>' --destination '<backup-directory>'
uv run --no-sync python -m tools.game.launch_test --package-directory '<package-directory>'
```

Pass `--me3-exe` and `--game-exe` if they are installed outside the default locations.
The helper copies the DLL into `output/game-test/installation/` and loads that
persistent installation. It creates the configuration on first use and preserves
later configuration edits, presets, and journals when refreshing the DLL. Profiles
and launcher logs also stay under `output/game-test/`, outside replaceable packages.
Use `--output-directory` to choose another persistent location, or `--check` to
prepare the installation and profile without launching the game.
For editing changes, exercise Apply and Undo, then save and reload to check
persistence. Other diagnostics are in [tools/](../tools/).

## Dependencies

Keep dependency changes with [sources.lock.json](../sources.lock.json) or
[uv.lock](../uv.lock). After changing Python dependencies, run `uv lock` and
`uv sync --locked`. Update [ThirdPartyNotices.txt](../ThirdPartyNotices.txt) when distributed
code or attribution changes.

To update the compiler, change the versioned MSVC component in `.vsconfig`, import
it into Visual Studio Installer, and activate a fresh PowerShell session. Select a
stable toolset available on the CI runner. Shell activation and CMake read this
same pin. Verify Release, clang-tidy, and AddressSanitizer before merging.

When changing the clang-format pin, update the matching LLVM archive checksum
in [the LLVM setup script](../tools/setup/llvm.py).

Keep game assets, saves, extracted data, and proprietary decoders out of version
control and distributed packages.

Store documentation screenshots as WebP files directly in `docs/screenshots/`.
This is the source asset check's only image exception. Record their source and
usage terms in [the screenshot notes](screenshots/README.md). Screenshots are not
included in mod packages.

## Repository protection

The public repository accepts issues and forks. PR creation is limited to
collaborators and authorized automation. Keep pull requests enabled for
Dependabot and Release Please.

Protect `main`, including administrators, with required PRs and the GitHub Actions
checks **Quick checks** and **Windows checks**. Require branches to be up to date
and conversations to be resolved. Block force pushes and branch deletion.
Require zero approving reviews so the sole maintainer can merge checked PRs.
Documentation-only changes can skip **Windows checks**; GitHub accepts skipped
required checks. Do not require the manually selected AddressSanitizer job.

Keep Dependabot alerts, security updates, secret scanning, and push protection
enabled. Require workflow approval for all external contributors. Keep default
workflow permissions read-only and allow GitHub Actions to create PRs for
Release Please.

GitHub leaves the optional non-provider pattern and validity-check settings
disabled on this personal repository. The settings audit checks the enabled
baseline above. [Validity checks](https://docs.github.com/en/code-security/how-tos/secure-your-secrets/customize-leak-detection/enable-validity-checks)
require an eligible organization repository with GitHub Secret Protection.

[Dependabot](../.github/dependabot.yml) checks Python dependencies and GitHub
Actions weekly, including the shared native setup action. Native commits in
`sources.lock.json`, tool versions in workflow inputs, and the LLVM archive pin
still need manual updates.

With an authenticated GitHub CLI account that has repository administration
access, verify the live settings:

```powershell
uv run --no-sync python -m tools.ci.check_repository
```

This command only reads settings. It needs network access and is separate from
ordinary CI, whose token has no administration permission. For a fork, pass
`--repository <owner>/<repo>`.

The `main` protection does not apply to release branches or version tags.
Release Please uses its existing write permissions to update its release PR and
create a tag and release after the maintainer merges that PR. It has no bypass
for `main`; its PR must pass the same required checks.

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

If the release PR is behind `main`, update its branch before running checks.
Branch protection requires both checks on the up-to-date branch before merging.

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
./tools/setup/shell.ps1
uv run --no-sync python -m tools.release.build
```

Packaging stages files inside its output directory, so the final moves stay on
the destination volume even when Windows TEMP is on a different drive. The
packaging regression test keeps its input in TEMP and its output in the checkout
to exercise that layout on hosted Windows runners.

The command runs the full check and creates the release package, ZIP, and checksum
in `dist/`. It owns a private fresh build and checked candidate, so concurrent
development rebuilds cannot change the bytes being frozen. An explicit
`--build-directory` overrides the fresh build directory. Existing release output
is never replaced. Use
`--output-directory <directory>` for a separate candidate.

If local publication fails after the full check, the build prints the path to
its retained checked candidate. After a hard stop, look for the candidate under
`dist/.release-*/package` from that build. Recover with the original checked
candidate, release version, and output directory.

```powershell
uv run --no-sync python -m tools.release.package --recover --package-directory '<checked-candidate>' --version '<version>' --output-directory '<release-output>'
```

Recovery verifies existing outputs against the checked candidate and publishes
only the missing artifacts. When the ZIP already exists, recovery computes the
checksum from that archive. A mismatch stops recovery. You can remove the retained
candidate directory after recovery succeeds. If the original candidate is unavailable,
`python -m tools.release.build --recover` builds and checks a fresh candidate.
The fresh package must match the published file bytes, which can vary across builds.

Complete in-game acceptance before publishing. Upload the ZIP to Nexus Mods.
Users can import the same ZIP into ME3.

## Release attestations and rebuilds

Release builds use separate checkouts for the selected source tag and the workflow
commit's tooling. Source compilation, tests, clang-tidy, and AddressSanitizer run
from the unmodified tagged checkout. The workflow's packager can be updated
without changing the tagged code. Before attesting, the source validator
checks both checkout commits and cleanliness, dependency pins and origins, ZIP
contents, the checksum, and equality with the explicit checked candidate.

Release rebuilds require the Python tooling and `tools/setup/shell.ps1` in the
tag. Tags using the old setup, including `v1.0.1` and `v1.0.2`, are rejected before
dependency installation. Published releases are unaffected.

The release publishes two GitHub attestations for the ZIP, checksum, and source
record. The standard build attestation identifies the GitHub-hosted workflow and
its commit. The custom predicate
`https://github.com/demondobf/ern-relics/attestations/release-source/v1` records the
actual source tag and commit separately from that tooling commit. The versioned
`.source.json` file contains the same claim. The release also includes downloadable
Sigstore bundles for offline verification. Neither kind of attestation is a claim
that the program is free of defects or has passed in-game acceptance.

For an existing release, select **Release > Run workflow**, choose the branch or
commit containing the release tooling, and enter its tag.
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
