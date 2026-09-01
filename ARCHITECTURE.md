# Plugin Architecture

The plugin system keeps the experimental X4 Pro work separate from the
CrossPoint Reader application code. The firmware fork supplies a small generic
host, while independently built SD-card modules supply the Plugins menu and its
activities.

## System boundary

```text
upstream CrossPoint Reader
          │
          │ small, mergeable fork delta
          ▼
firmware: Plugins entry + loader + host ABI + BLE transport
          │
          │ lazy native loading from /plugins
          ▼
manager.so ── discovers child metadata ──> terminal.so, future child .so files
          ▲
          │ authenticated PageWire Protocol
          │
PageWire client ── validates crosspoint-plugins.zip
```

The firmware does not contain a catalog of plugin names or UI strings. It knows
only the fixed manager path, `/plugins/manager.so`, and a versioned host ABI.
The manager discovers child `.so` files dynamically from metadata embedded in
their ELF images.

## Lazy lifecycle

No plugin code runs during boot. Selecting **Plugins** loads `manager.so` from
the SD card. The firmware can read child titles, versions, and ordering metadata
without relocating or executing those modules. A child is loaded only after its
row is selected. Back unloads the child; leaving Plugins unloads the manager.

Terminal therefore allocates its activity, fonts, frame cache, and BLE state
only while **Plugins > Terminal** is open. Ordinary reading and the rest of the
upstream UI do not pass through plugin code.

## Independent updates

| Component | Update path | Requires firmware rebuild |
|---|---|---|
| Firmware host and plugin ABI | Normal X4 Pro firmware update | Yes |
| `manager.so` | SD card or Wi-Fi file transfer | No |
| Child plugins such as `terminal.so` | SD card, Wi-Fi, or authenticated BLE | No |
| PageWire client | Normal application update | No |

The first manager installation must be copied to `/plugins/manager.so`. Once it
is present, **Install via Bluetooth** accepts compatible child modules from
PageWire. The updater creates `/plugins` if necessary, but deliberately
cannot replace `manager.so` while that manager is running.

Each build produces a ZIP manifest containing the bundle version, plugin ABI,
module sizes, SHA-256 digests, and BLE-update policy. PageWire validates
the manifest before transfer. The reader then independently validates the
received size and digest, embedded trailer, ABI, ELF structure, and plugin
metadata before listing or loading the module. An interrupted direct transfer
may leave that child unavailable, but it does not touch the manager, firmware,
OTA state, partition table, bootloader, or recovery path; retransferring or
copying the child restores it.

## Failure and recovery model

Plugins are native code, not isolated processes. A defective plugin can crash
its activity and cause the firmware watchdog or crash handler to restart the
reader. This is not treated as a brick: plugins are absent from the boot path,
the normal firmware starts again within seconds, and removing the offending
`.so` prevents it from being selected again.

The host ABI exposes a deliberately small symbol allow-list, and the build
rejects unresolved calls outside it. Plugin installation is confined to child
files below `/plugins`. These boundaries prevent ordinary plugin and transfer
failures from modifying persistent firmware or boot state.

This design is failure containment, not a security sandbox. A deliberately
malicious native module or arbitrary memory-corruption exploit is outside the
guarantee. Only install bundles from a trusted source.

## Upstream maintenance

The fork changes only the code needed to expose **Plugins**, validate/load
modules, provide the host ABI, and activate the shared BLE transport on demand.
Plugin activities, fonts, menu strings, package manifests, and build tooling
remain in this repository. Keeping that boundary narrow reduces conflicts when
merging later upstream CrossPoint changes and avoids reflashing the X4 Pro for
normal plugin development.

Compatibility is based on the explicit plugin ABI, not a firmware build ID.
Firmware changes that preserve ABI 3 can continue using the same modules. An
incompatible future host increments the ABI so old modules are rejected before
execution.
