# Plugin Architecture

The firmware fork supplies a small stable host. SD modules own plugin UI and
application protocols.

```text
CrossPoint firmware
  Plugins entry + ELF loader + ABI4 services + raw authenticated BLE
      -> /plugins/manager.so (loaded when Plugins opens)
          -> metadata discovery
          -> /plugins/terminal.so and future child modules (loaded on selection)
```

The firmware knows only `/plugins/manager.so`, the ABI, integrity format, and
host services. It contains no child names, child strings, PageWire magic or
opcodes, terminal document state, layout, or fonts.

## Lifecycle and memory

No plugin code runs during boot. Selecting **Plugins** loads `manager.so`.
Metadata for child rows is read without executing the child. Selecting a row
loads that `.so`; Back unloads it, and leaving Plugins unloads manager.

Terminal's PageWire parser, two fixed 12 KiB buffers, fonts, and UI therefore
exist only while Terminal is active. The ordinary book reader and the rest of
upstream CrossPoint never pass through plugin code.

## Independent updates

| Component | Update path | Firmware flash |
|---|---|---|
| Host/ABI/generic BLE | X4 Pro firmware update | Required |
| `manager.so` | SD card or Wi-Fi | No |
| Child `.so` modules | SD, Wi-Fi, or authenticated BLE | No |
| PageWire companion | APK/helper update | No |

The ZIP manifest describes child names, ABI, lengths, SHA-256 values, and update
policy. Android validates it, then manager streams eligible children directly
to `/plugins`. The reader independently validates the final file, embedded
trailer, ABI, ELF structure, and descriptor. An interrupted child transfer may
leave that child unavailable but cannot replace manager, firmware, OTA state,
partition table, bootloader, or recovery path.

## Generic BLE boundary

ABI4 exposes one authenticated raw packet service: lifecycle, connection and
pairing status, packet polling/sending, queue backpressure, maximum packet size,
drop counters, and active/idle link parameters. Packet contents belong entirely
to the active plugin. Terminal and manager happen to use PageWire v5, but a
future plugin may implement another bounded protocol without changing firmware.

## Failure model

Plugins are native code, not isolated processes. A defective plugin may crash
its activity and restart the reader. Plugins are absent from the boot path, so
normal firmware starts again and removing the offending `.so` prevents another
selection. This is failure containment, not a sandbox against malicious native
code.

Compatibility is based on explicit ABI4, not a firmware build ID. Compatible
host changes do not invalidate modules. An incompatible host change increments
the ABI so old modules are rejected before execution.

The narrow host boundary reduces conflicts when merging upstream CrossPoint.
See [Plugin Development](PLUGIN_DEVELOPMENT.md) for the author contract.
