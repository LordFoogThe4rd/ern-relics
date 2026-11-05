# Agent instructions

See the [development guide](docs/development.md) for setup and verification.

- Run `./tools/check.ps1 -Label dev` for code changes.
- For documentation-only changes, run `uv run --locked python -m tools.checks.layout`.
- Reuse `dist/ern-relics-<version>-dev/` for development builds. Preserve versioned
  release packages and ZIPs in `dist/`.
- Keep installed configuration, presets, and journals outside replaceable build output.
- Keep game assets, saves, extracted data, and proprietary decoders out of version
  control and distributed packages.
