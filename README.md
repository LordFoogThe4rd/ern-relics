# ERN Relics

A C++20 relic editor for Elden Ring Nightreign on 64-bit Windows.

## Build and test

Install Git, uv, Visual Studio C++ Build Tools with the components in
`.vsconfig`, and LLVM matching the clang-format pin in `pyproject.toml`.
Run commands from this directory in PowerShell 7.4 or newer.

```powershell
./tools/bootstrap.ps1
```

Open an x64 Visual Studio developer shell, then configure, build, and test:

```powershell
cmake --preset release -B output/build
cmake --build output/build
ctest --test-dir output/build --output-on-failure
```

Keep game assets, saves, extracted data, and proprietary decoders out of
version control and distributed packages. See [LICENSE](LICENSE) and
[third-party notices](ThirdPartyNotices.txt).
