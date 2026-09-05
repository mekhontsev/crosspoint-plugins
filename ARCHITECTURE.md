# Plugin Architecture

The firmware fork supplies a small stable host. SD modules own plugin UI and
application protocols.

```text
CrossPoint firmware
  Plugins entry + ELF loader + ABI5 services + raw authenticated BLE
      -> /plugins/manager.so (loaded when Plugins opens)
          -> metadata discovery
          -> /plugins/terminal.so and future child modules (loaded on selection)
          -> /plugins/debugger.so and other background services (loaded on request)
```

The firmware knows only `/plugins/manager.so`, the ABI, integrity format, and
host services. It contains no child names, child strings, PageWire magic or
opcodes, terminal document state, layout, or fonts.

Small plugin settings use a generic ABI5 internal-NVS service: one bounded
opaque blob per plugin-selected ID. The firmware never interprets settings.
Terminal stores its font selection; reads and unchanged writes do not wear SD
or trigger repeated flash writes.

## Lifecycle and memory

No plugin code runs during boot. Selecting **Plugins** loads `manager.so`.
Metadata for child rows is read without executing the child. Selecting a row
loads that `.so`; Back unloads it, and leaving Plugins unloads manager.
Terminal's Back/Home action returns directly to the firmware home menu, unloading
the child, manager and any cached background service.

Terminal's PageWire parser, three fixed 12 KiB buffers, fonts, and UI therefore
exist only while Terminal is active. The ordinary book reader and the rest of
upstream CrossPoint never pass through plugin code.

On unload, Terminal clears shared glyph caches before unregistering its fonts,
so no cached compressed-font pointer survives the module's lifetime.

The buffers have separate roles: displayed text, acknowledged delta base, and
staging. Incoming revisions can advance the delta base without changing the
history currently being read. The render task acknowledges only the revision
it actually rendered; a mutex-protected handoff delivers that acknowledgement
from the activity loop.

## Idle power

Terminal does not continuously redraw, send keepalive requests, or override the
firmware's loop delay. Its receive-related wake hold expires five seconds after
the last accepted packet; an unsent command expires after ten seconds. Ordinary
CPU downclocking and auto-sleep then apply. BLE is stopped on exit.

The generic host configures ESP32-S3 Bluetooth modem sleep with the main crystal
when BLE starts, without changing the global CPU or board sleep configuration.
The plugin requests a slower connection after five seconds without a transfer.
The host verifies applied parameters and retries failures with bounded backoff;
an idle or uncooperative peer does not trigger endless parameter requests.

Maintaining a radio connection still has a cost. Equality with ordinary reading
must not be claimed from code inspection; see the shared
[power validation checklist](https://github.com/mekhontsev/pagewire/blob/main/docs/power-testing.md).

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

`bundle.json` is used by the companion when opening a ZIP; it is not a runtime
catalog or an external integrity database on the SD card. The embedded `.so`
trailer and descriptor remain authoritative for direct SD/Wi-Fi installation.
SHA-256 checks detect corruption, not a malicious publisher; use trusted modules.

## Generic BLE boundary

ABI5 exposes a shared authenticated BLE transport: lifecycle, connection and
pairing status, packet polling/sending, queue backpressure, maximum packet size,
drop counters, and active/idle link parameters. Packet contents belong entirely
to the active plugin. Terminal and manager happen to use PageWire v5, but a
future plugin may implement another bounded protocol without changing firmware.

The second logical channel routes opaque requests by module name. It shares the
same radio connection and indication backpressure. A background service runs
briefly on the main activity task while the foreground plugin keeps its UI;
only one service module is cached at a time. Debugger owns diagnostic operations,
and Terminal opts in through a named state provider. The host knows neither
schema. There are no diagnostic threads, periodic polls or unsolicited log streams.

## Failure model

Plugins are native code, not isolated processes. A defective plugin may crash
its activity and restart the reader. Plugins are absent from the boot path, so
normal firmware starts again and removing the offending `.so` prevents another
selection. This is failure containment, not a sandbox against malicious native
code.

Compatibility is based on explicit ABI5, not a firmware build ID. Compatible
host changes do not invalidate modules. An incompatible host change increments
the ABI so old modules are rejected before execution.

The narrow host boundary reduces conflicts when merging upstream CrossPoint.
See [Plugin Development](PLUGIN_DEVELOPMENT.md) for the author contract.
