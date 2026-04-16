# Baymesh Firmware Fork — AGENTS.md

This is the **Baymesh** fork of [meshtastic/firmware](https://github.com/meshtastic/firmware), maintained at `RCGV1/firmware-Fork` on the `baymesh-refactor` branch. It extends upstream Meshtastic with Bay Area-specific features: a 64-hop routing range, a separate broadcast hop limit, and the MeshControl remote-administration module.

---

## Active Branch

Always work on **`baymesh-refactor`**. Upstream changes are merged from `upstream/develop` as needed.

---

## Key Differences vs Upstream

### 1. Extended Hop Limits (`src/mesh/MeshTypes.h`)

| Constant                            | Baymesh | Upstream |
| ----------------------------------- | ------- | -------- |
| `HOP_MAX`                           | **64**  | 7        |
| `HOP_RELIABLE` (default unicast)    | **64**  | 3        |
| `HOP_BROADCAST` (default broadcast) | **3**   | 3        |

- `HOP_MAX` and `HOP_RELIABLE` are raised to 64 so relay nodes can propagate directed messages across the full Bay Area mesh.
- `HOP_BROADCAST` stays at 3 to contain flood traffic.
- The packet header field was widened to 7 bits in the LoRa layer (`RadioLibInterface.cpp`).

### 2. Separate Broadcast Hop Limit (`src/mesh/Default.cpp`, `src/mesh/Router.cpp`)

A new `Default::getConfiguredOrDefaultBroadcastHopLimit()` function reads `config.lora.broadcast_hop_limit` (a Baymesh proto field). Broadcast packets use this limit independently of the unicast `hop_limit`. Default is `HOP_BROADCAST` (3).

`NodeDB.cpp` initialises `config.lora.hop_limit = HOP_RELIABLE` at first boot so new nodes default to 64 hops.

### 3. MeshControl Module (`src/modules/MeshControlModule.{h,cpp}`)

A new ProtobufModule on **port 78** (`meshtastic_PortNum_MESH_CONTROL_APP`) that lets a trusted administrator remotely reconfigure nodes over the mesh.

**Security model:**

- Shared 32-byte `control_key` between controller and nodes.
- Authentication: `HMAC-SHA256(control_key, canonical_packet_bytes)`, first 16 bytes embedded in the `hmac` field.
- Replay protection: monotonically increasing `seq_num` (use a Unix timestamp).
- Accept policies (via `Config.MeshControlConfig.accept_policy`):
  - `DISABLED` — reject all packets.
  - `PROMPT` — send a ClientNotification; user approves via the app.
  - `AUTO` — apply validated settings immediately.
- Fine-grained `allow_*` flags restrict which setting categories can change.

**Remotely settable fields** (via `MeshControlSettings`):

- `modem_preset`, `override_frequency`, `channel_num`
- `hop_limit`, `broadcast_hop_limit`
- `position_broadcast_secs`, `device_telemetry_interval`, `node_info_broadcast_secs`

### 4. Flood / Relay Semantics (`src/mesh/FloodingRouter.cpp`, `NextHopRouter.cpp`)

- `relay_node` and `next_hop` are `uint32_t` (was `uint8_t` upstream) to handle full node numbers.
- `NO_RELAY_NODE` / `NO_NEXT_HOP_PREFERENCE` constants added.
- Broadcast packets clear `relay_node` on send so downstream nodes continue flooding.
- DM flood-fallback (last retransmit with `next_hop == 0`) also clears `relay_node` to avoid every intermediate node refusing relay.
- `stopRetransmission` threshold and PKI retry slot fixes included.

### 5. No Congestion Scaling (`src/mesh/Default.cpp`)

`getConfiguredOrDefaultMsScaled()` does **not** scale intervals by node count. Upstream scales broadcast intervals based on the number of online nodes; Baymesh removes this to keep timing predictable on the Bay Area mesh.

### 6. Position Broadcast Disabled by Default

Position broadcasting is off by default. This is intentional for Baymesh deployments; nodes should opt in if they want to share position.

### 7. AudioModule Guard (`src/modules/esp32/AudioModule.cpp`)

AudioModule includes are guarded with `#if !MESHTASTIC_EXCLUDE_AUDIO` to avoid compile errors on builds that don't include audio support.

---

## Protobuf Fork — Critical Rules

### The fork uses a custom protobuf submodule

```
.gitmodules:  url = https://github.com/RCGV1/protobufs-fork.git
              path = protobufs
```

The `protobufs/` submodule **points to `RCGV1/protobufs-fork`**, not `meshtastic/protobufs`. This fork adds:

- `meshtastic/mesh_control.proto` — `MeshControlPacket`, `MeshControlSettings`, `MeshControlConfig`
- `meshtastic/portnums.proto` — port 78 (`MESH_CONTROL_APP`)
- `meshtastic/config.proto` — `broadcast_hop_limit` field in `LoRaConfig`, `MeshControlConfig` in module config

### DO NOT edit generated files

All files under `src/mesh/generated/` are **auto-generated**. Never edit them by hand:

```
src/mesh/generated/meshtastic/mesh_control.pb.{h,cpp}
src/mesh/generated/meshtastic/portnums.pb.{h,cpp}
src/mesh/generated/meshtastic/config.pb.{h,cpp}
... (all *.pb.h / *.pb.cpp)
```

To regenerate after changing a `.proto` file in the `protobufs/` submodule:

```bash
# Requires nanopb 0.4.9 in the repo root (nanopb-0.4.9/)
# Download from https://jpa.kapsi.fi/nanopb/download/
bash bin/regen-protos.sh
```

The script runs `protoc` with the nanopb plugin against `protobufs/meshtastic/*.proto` and writes output to `src/mesh/generated/`.

### Proto changes must go through the fork

1. Edit `.proto` files in `~/protobufs-fork/` (the standalone fork repo at `RCGV1/protobufs-fork`).
2. Commit and push the proto fork.
3. Update the submodule in this repo: `git -C protobufs pull && git add protobufs && git commit`.
4. Run `bin/regen-protos.sh` to regenerate the C files.
5. Commit the regenerated files.

---

## CI / Build Pipeline

### Workflow: `.github/workflows/build-and-publish.yml`

- Triggers on push to `baymesh-refactor` or `workflow_dispatch`.
- Version format: `2.7.21.<7-char-sha>` (e.g. `2.7.21.43ca8b2`).
- Resolves the build matrix from `.github/baymesh-targets.json` (206 board targets) using `bin/generate_ci_matrix.py`.
- Builds firmware in parallel; publishes even if some boards fail.
- Artifacts published to `baymesh/bayme.sh-firmware-pages` (GitHub Pages, `gh-pages` branch).
- Publishes a `latest.json` manifest listing firmware filenames and types.
- Release ZIP is **not** published (exceeds GitHub's 100 MB file size limit).

### Workflow: `.github/workflows/publish-firmware.yml`

Secondary workflow called by the zone repo to orchestrate publishing.

### Board target list: `.github/baymesh-targets.json`

Contains the ~206 board names that should be built for Baymesh. Adding a new board: add its PlatformIO environment name here. The CI will fail loudly if a board name is not found in the upstream matrix.

---

## Flasher Infrastructure

### flasher.bayme.sh

The web flasher lives at `flasher.bayme.sh` and is a fork of `meshtastic/web-flasher` deployed from `baymesh/web-flasher`. It reads firmware from the GitHub Pages output of this repo's CI builds.

### Repos involved

| Repo                                       | Purpose                                         |
| ------------------------------------------ | ----------------------------------------------- |
| `RCGV1/firmware-Fork` (`baymesh-refactor`) | This repo — firmware source                     |
| `RCGV1/protobufs-fork`                     | Custom protobufs (submodule)                    |
| `baymesh/bayme.sh-firmware-pages`          | CI artifact host (GitHub Pages, `gh-pages`)     |
| `baymesh/web-flasher`                      | Web flasher UI                                  |
| `bayme.sh-zone` / `baymesh-zone`           | DNS (DNSControl via Porkbun) + CI orchestration |

### DNS (`dnsconfig.js` in zone repos)

- `flasher.bayme.sh` → CNAME to `baymesh.github.io` (web flasher GitHub Pages)
- `firmware.bayme.sh` → CNAME to `baymesh.github.io` (firmware pages)
- DNS managed by DNSControl against Porkbun API.
- Required secrets: `PORKBUN_API_KEY`, `PORKBUN_SECRET_KEY`.

---

## Development Workflow

### Merging upstream

```bash
git fetch upstream
git merge upstream/develop
# Resolve conflicts — MeshTypes.h, Default.cpp, and the protobufs submodule
# pointer need special attention
```

### Building locally

```bash
# Install PlatformIO
pip install platformio

# Build a single board (example)
pio run -e tlora-v2-1-1_6

# Build all Baymesh targets (slow)
python bin/generate_ci_matrix.py all --level extra
```

### Regenerating protobufs

```bash
# Ensure nanopb-0.4.9/ is present in the repo root
bash bin/regen-protos.sh
git add src/mesh/generated/
git commit -m "regen: update generated protobufs"
```

### Testing MeshControl

Test scripts in the repo root:

- `test_comprehensive_mc.py` — full MeshControl integration test
- `test_full_meshcontrol.py` — end-to-end packet flow
- `test_mc_with_key.py` — HMAC key verification

---

## Files Modified vs Upstream (Summary)

| File                                              | Change                                                             |
| ------------------------------------------------- | ------------------------------------------------------------------ |
| `src/mesh/MeshTypes.h`                            | `HOP_MAX`=64, `HOP_RELIABLE`=64, `HOP_BROADCAST`=3                 |
| `src/mesh/Default.cpp` / `Default.h`              | `getConfiguredOrDefaultBroadcastHopLimit()`, no congestion scaling |
| `src/mesh/FloodingRouter.cpp`                     | relay semantics, broadcast/DM flood-fallback fixes                 |
| `src/mesh/NextHopRouter.cpp` / `.h`               | `next_hop`/`relay_node` as `uint32_t`, broadcast hop limit         |
| `src/mesh/Router.cpp` / `.h`                      | broadcasts use `getConfiguredOrDefaultBroadcastHopLimit`           |
| `src/mesh/RadioInterface.cpp` / `.h`              | 7-bit hop field, `HOP_MAX` guard                                   |
| `src/mesh/NodeDB.cpp`                             | default `hop_limit = HOP_RELIABLE` at init                         |
| `src/mesh/MeshService.cpp`                        | relay_node support                                                 |
| `src/mesh/PacketHistory.cpp` / `.h`               | packet dedup with upgrade detection                                |
| `src/mesh/PacketCache.cpp`                        | packet cache fixes                                                 |
| `src/mesh/ReliableRouter.cpp`                     | stopRetransmission threshold, PKI retry                            |
| `src/modules/MeshControlModule.cpp` / `.h`        | **new** — MeshControl module (port 78)                             |
| `src/modules/Modules.cpp`                         | registers MeshControlModule                                        |
| `src/modules/AdminModule.cpp`                     | approve/reject pending MeshControl settings                        |
| `src/modules/NodeInfoModule.cpp` / `.h`           | node info changes                                                  |
| `src/modules/TrafficManagementModule.cpp`         | traffic mgmt                                                       |
| `src/modules/esp32/AudioModule.cpp` / `.h`        | `MESHTASTIC_EXCLUDE_AUDIO` guard                                   |
| `src/mesh/generated/meshtastic/mesh_control.pb.*` | **generated** — do not edit                                        |
| `src/mesh/generated/meshtastic/portnums.pb.*`     | **generated** — do not edit                                        |
| `src/mesh/generated/meshtastic/config.pb.*`       | **generated** — do not edit                                        |
| `protobufs` (submodule)                           | points to `RCGV1/protobufs-fork`                                   |
| `.gitmodules`                                     | submodule URL override                                             |
| `.github/baymesh-targets.json`                    | 206-board target list                                              |
| `.github/workflows/build-and-publish.yml`         | Baymesh CI pipeline                                                |
| `.github/workflows/publish-firmware.yml`          | publish workflow                                                   |
| `bin/regen-protos.sh`                             | proto regeneration script                                          |
| `docs/baymesh-release-notes.md`                   | release notes                                                      |
