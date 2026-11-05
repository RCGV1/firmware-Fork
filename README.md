# Full Position Firmware

Custom Meshtastic firmware that honors configured position precision on public and unencrypted channels, including full precision (`32`). Precision `0` continues to disable sharing.

[Download firmware](https://rcgv1.github.io/firmware-Fork/) · [Releases](https://github.com/RCGV1/firmware-Fork/releases) · [Build status](https://github.com/RCGV1/firmware-Fork/actions/workflows/build.yml)

The updater checks upstream hourly and builds every supported radio board in upstream's release matrix. It also attempts all experimental `extra` variants. The download page offers stable, latest release (including alpha/beta), and bleeding edge from `develop`. Linux/macOS/Windows host applications and test environments are excluded from the radio firmware downloads.

Each build starts from an immutable upstream commit, applies `policy/position.patch`, runs the native tests, and builds all radio variants. A release is published only after all native tests, all upstream release-matrix boards, and every artifact checksum pass. Experimental variants are downloadable only when they build successfully; unavailable experimental boards are listed explicitly in the release notes and manifest. Failed supported-board updates leave previous downloads available. Upstream releases published after October 29, 2026 are retained alongside the latest stable and prerelease available at setup.

ZIPs include the upstream installation files and build provenance. Releases provide SHA-256 checksums, a board manifest, and a source tag pointing to the customized source. The `builds/*` branches preserve each source snapshot. Changes in upstream precision handling that prevent the patch from applying stop publication until reviewed.

This is an independent fork. Radio limits and other firmware behavior follow upstream. Full-precision positions sent on public channels can be read by anyone with access to that channel.

Upstream GitHub Actions workflows are omitted from generated firmware snapshots; this branch owns the build and publishing workflows. The updater uses the repository's built-in GitHub token and needs no personal access token.

The `distribution` branch contains this publishing service. Firmware source is on the release tags and `builds/*` branches; the initial development customization is on [`full-precision-public-channels`](https://github.com/RCGV1/firmware-Fork/tree/full-precision-public-channels).

## Download-page analytics

The page supports [Umami Cloud's free Hobby plan](https://umami.is/pricing). Create an account, add one website with domain `rcgv1.github.io`, set the repository Actions variable `UMAMI_WEBSITE_ID` to the public website UUID shown in its tracking code, then run `page.yml`. Until that variable is set, analytics is disabled. No password or API token belongs in this repository. The dashboard is at https://cloud.umami.is and stays private unless its owner enables sharing. The integration uses Umami's open-source tracker; Cloud hosting has its own free-plan limits.

The page records visits and a `download_click` event with `board`, `release` and `track` properties. In Umami, open the website's Events view, select `download_click`, and use Properties to break down or filter counts by board, release or track. Compare event totals with visitor counts to distinguish repeated clicks from distinct visitors. These are browser visits and download clicks, not completed transfers or firmware installations. Direct GitHub downloads are not included. Excluding automated browsers reduces automated activity but cannot prove that every visitor is human; blockers and privacy preferences can also reduce counts.

Open the page with `?analytics=off` to persistently exclude your own browser; `?analytics=on` clears that exclusion. Automated browsers, local previews, Do Not Track and Global Privacy Control are excluded. Query strings and full referring paths are not sent by this integration. Analytics errors never delay or prevent downloads.
