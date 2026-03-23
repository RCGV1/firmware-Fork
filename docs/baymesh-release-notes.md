# Baymesh Firmware

Baymesh is a Meshtastic firmware fork for the Bay Area mesh with custom control, routing, and deployment behavior.

## Highlights

- MeshControl module on port 78 with HMAC-SHA256 authentication
- Replay protection using sequence numbers and minimum interval enforcement
- Position broadcast disabled by default
- Custom protobuf additions for Baymesh control features

## Compatibility

This firmware is intended for Baymesh deployments and is not a drop-in replacement for standard Meshtastic client workflows.

## Flashing Notes

- Use Baymesh-compatible clients and tooling after flashing
- Back up keys and settings before switching from stock Meshtastic firmware
- Some devices publish variant builds such as `-tft` or `-inkhud`; the flasher will surface those when available

## Project Links

- Firmware source: https://github.com/RCGV1/firmware-Fork/tree/baymesh-refactor
- Web flasher: https://github.com/baymesh/web-flasher
