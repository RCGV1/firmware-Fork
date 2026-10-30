# Full Position Firmware

Custom Meshtastic firmware that honors configured position precision on public and unencrypted channels, including full precision (`32`). Precision `0` continues to disable sharing.

[Download firmware](https://rcgv1.github.io/firmware-Fork/) · [Releases](https://github.com/RCGV1/firmware-Fork/releases) · [Build status](https://github.com/RCGV1/firmware-Fork/actions/workflows/build.yml)

The updater checks upstream hourly and builds every radio board variant, including upstream's extra variants. The download page offers stable, latest release (including alpha/beta), and bleeding edge from `develop`. Linux/macOS/Windows host applications and test environments are excluded from the radio firmware downloads.

Each build starts from an immutable upstream commit, applies `policy/position.patch`, runs the native tests, and builds all radio variants. A release is published only after every required build and checksum check succeeds. Failed updates leave previous downloads available. Upstream releases published after October 29, 2026 are retained alongside the latest stable and prerelease available at setup.

ZIPs include the upstream installation files and build provenance. Releases provide SHA-256 checksums, a board manifest, and a source tag pointing to the customized source. The `builds/*` branches preserve each source snapshot. Changes in upstream precision handling that prevent the patch from applying stop publication until reviewed.

This is an independent fork. Radio limits and other firmware behavior follow upstream. Full-precision positions sent on public channels can be read by anyone with access to that channel.

Upstream GitHub Actions workflows are omitted from generated firmware snapshots; this branch owns the build and publishing workflows. The updater uses the repository's built-in GitHub token and needs no personal access token.

The `distribution` branch contains this publishing service. Firmware source is on the release tags and `builds/*` branches; the initial development customization is on [`full-precision-public-channels`](https://github.com/RCGV1/firmware-Fork/tree/full-precision-public-channels).
