# Baymesh Firmware

Baymesh is a Meshtastic firmware fork for the Bay Area mesh with custom control, routing, and deployment behavior.

## Highlights

- MeshControl module on port 78 with HMAC-SHA256 authentication
- Replay protection using sequence numbers and minimum interval enforcement
- Position broadcast disabled by default
- Custom protobuf additions for Baymesh control features
- Extended hop limit: `HOP_MAX` raised to 64 (upstream caps at 7), allowing relay nodes to propagate packets further across the Bay Area mesh
- Separate broadcast hop limit: `lora.broadcast_hop_limit` controls broadcast flooding depth independently of the unicast hop limit, defaulting to 3 to contain flood traffic while keeping DM routing range high

## Compatibility

This firmware is intended for Baymesh deployments and is not a drop-in replacement for standard Meshtastic client workflows.

## Flashing Notes

- Use Baymesh-compatible clients and tooling after flashing
- Back up keys and settings before switching from stock Meshtastic firmware
- Some devices publish variant builds such as `-tft` or `-inkhud`; the flasher will surface those when available

## Project Links

- Firmware source: https://github.com/RCGV1/firmware-Fork/tree/baymesh-refactor
- Web flasher: https://github.com/baymesh/web-flasher
