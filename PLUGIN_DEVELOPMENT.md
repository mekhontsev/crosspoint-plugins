# Plugin Development

CrossPoint plugins are native C++ ELF modules loaded from `/plugins` on X4 Pro.
They build independently against the firmware's public plugin ABI. The current
ABI is **5**.

A bad native plugin can crash and restart its activity. It is not loaded during
boot; remove its `.so` to disable it. This mechanism is not a security sandbox.

## Module contract

Child names are 1-32 lowercase ASCII letters, digits, `-`, or `_`. `manager` is
reserved. A child exports its ABI, embedded descriptor, and activity factory:

```cpp
#include <Memory.h>
#include <crosspoint/PluginAbi.h>

#include "ExampleActivity.h"

extern "C" __attribute__((visibility("default"))) uint32_t
crosspoint_plugin_abi() {
  return crosspoint_plugin::ABI_VERSION;
}

extern "C" __attribute__((used, section(".crosspoint.plugin"),
                          visibility("default")))
const crosspoint_plugin::PluginDescriptorV3 crosspoint_plugin_metadata_v3 = {
    "Example", "0.1.0", 20, 0,
};

extern "C" __attribute__((visibility("default"))) Activity*
crosspoint_plugin_create(GfxRenderer* renderer,
                         MappedInputManager* input) {
  if (!renderer || !input) return nullptr;
  auto activity = makeUniqueNoThrow<ExampleActivity>(*renderer, *input);
  return activity.release();
}
```

The host owns the returned activity. Allocate fallibly and release all
activity-owned resources from the normal lifecycle. Plugin titles and messages
belong in the plugin, not firmware.

The factory runs on the main task with the host render lock held, so registering
fonts is safe. Keep it short: do not take another render lock, wait for rendering,
or start background work there. Start asynchronous services from `onEnter`.

Before unloading embedded fonts, clear `renderer.getFontCacheManager()` when
non-null, then unregister the font IDs. Decompressed glyph caches retain
module-data pointers; removing a font ID alone does not invalidate them.

## Host services

The ABI allow-list provides:

| Area | Facilities |
|---|---|
| Activity/UI | lifecycle, child activities, home return, screen geometry, text measurement/drawing, shapes, framebuffer refresh, theme metrics, fonts |
| Input | mapped hardware buttons, touch rectangles, long presses, labels |
| Keyboard | firmware-owned text keyboard and asynchronous result API |
| Plugin manager | metadata listing, child creation, streamed install |
| Generic BLE | authenticated raw packet lifecycle, poll/send, backpressure, MTU-sized cap, status, pairing code, drop count, link activity hint |
| Settings | plugin-owned blobs in internal NVS; no SD access and no firmware knowledge of the contents |
| Diagnostics | named plugin state providers, bounded read-only SD chunks, consistent recent-log snapshots |
| Runtime | bounded logging, time, heap/PSRAM information, allowed allocation/C runtime and FreeRTOS primitives |

[`PluginHostSymbols.inc`](https://github.com/mekhontsev/crosspoint-reader/blob/main/src/plugins/PluginHostSymbols.inc)
is authoritative. A declaration in a private firmware header is not an exported
service. Filesystem writes, Wi-Fi, networking, and raw NimBLE APIs are not exposed.

## Background service modules

A service is an independently built `.so` with normal ABI/trailer/SHA validation,
metadata flag `PLUGIN_FLAG_SERVICE`, `crosspoint_plugin_abi`, and the C export:

```cpp
size_t crosspoint_plugin_request_v5(const uint8_t* request, size_t length,
                                  uint8_t* response, size_t capacity);
```

It has no Activity factory and is not a selectable menu row. The host lazily loads
the named module on its first request. One service module is cached at a time;
requesting another unloads the first, and leaving Plugins unloads it. Installing
a replacement also invalidates its loaded copy. The host contains no service or
application names. Add service source files and the appropriate required entry
point to the independent build script when creating a module.

Calls run on the main activity task, including while a keyboard owns the screen.
Handlers must be bounded and return promptly. They must not start threads, keep
host callbacks, access the renderer, block on render completion, or perform
periodic work. Static state belongs to the `.so` and disappears at unload. This
is cooperative concurrency, not a debugger that can stop/resume CPU execution.

The generic BLE service uses authenticated write RX UUID
`6f2c8f10-7f7a-4f1e-a2a6-8e8b7b3f6d14` and indication TX UUID
`6f2c8f10-7f7a-4f1e-a2a6-8e8b7b3f6d15`, in the same existing service and
connection as foreground RX/TX. It borrows the foreground plugin's BLE lifetime;
it neither starts another connection nor keeps ordinary reading awake.

Each request is at most 244 bytes: four-byte little-endian request ID, one-byte
module-name length, 1–32 lowercase ASCII module-name bytes (without `.so`), then
nonempty opaque payload. Allowed name characters are `a-z`, `0-9`, `_`, `-`.
The reply echoes the ID, then a transport status byte and opaque module response.
Transport status is 0 success, 1 invalid request, 2 unavailable/incompatible/busy
module, or 3 invalid handler result. The response capacity passed to the handler
is 239 bytes. The current client requires ATT MTU >= 247. There is one shared
indication in flight, a two-packet service RX queue, and no automatic RPC retry.
Commands should be idempotent: a lost response does not prove an action failed.

An activity can opt into state inspection with `crosspoint_plugin_state_register_v5`
and a unique ID of 1–15 lowercase ASCII letters, digits, `_` or `-`. Four providers
are supported. Its callback receives an opaque request and bounded output buffer;
the provider owns its schema and validates any mutation. Unregister with the same
ID/context in `onExit`, before destroying the object or unloading its module.
Do not expose secrets, arbitrary addresses or raw pointer lifetimes. Providers
must synchronize their own data with any render or worker task.

`crosspoint_plugin_file_read_v5` accepts an absolute SD path shorter than 128 bytes,
byte offset, and a buffer of at most 224 bytes. Dot segments, control characters
and backslashes are rejected. It reports total file size and copied bytes,
including successful zero-byte EOF; all I/O uses HalStorage with `O_RDONLY`.
It never writes to SD and does not hold a file between calls. A file may change
between chunks, so do not treat a multi-request read as an atomic filesystem snapshot.

`crosspoint_plugin_logs_copy_v5` copies the existing 16-entry log ring into a
caller-owned 4096-byte buffer under a short critical section, without allocation.
Use `LOG_ERR`, `LOG_INF`, and `LOG_DBG` with a descriptive origin in plugins.
The build log level still applies. `debugger.so` keeps a stable snapshot in its
own memory while the CLI downloads chunks; it never streams logs unsolicited.

These APIs are not a sandbox: native modules share the firmware address space.
The supplied debugger exposes only status, logs, bounded read-only files, saved
settings reads, and operations explicitly implemented by a named provider.
The supplied module's payload schema is documented separately in
[Debugger protocol](DEBUGGER.md); it is not part of PageWire or the firmware ABI.

## Small persistent settings

ABI5 adds `crosspoint_plugin_settings_read_v5` and
`crosspoint_plugin_settings_write_v5`. Each plugin chooses a stable unique ID
of 1-15 lowercase ASCII letters, digits, `_` or `-`, and owns one opaque blob
of 1-128 bytes. Keep the blob format and default values inside the plugin.

```cpp
uint8_t setting = 3;
uint8_t stored = 0;
size_t length = 0;
if (crosspoint_plugin_settings_read_v5("example", &stored, sizeof(stored), &length)
    && length == sizeof(stored) && stored < 9) {
  setting = stored;
}
// Only after a user changes the setting:
bool saved = crosspoint_plugin_settings_write_v5("example", &setting, sizeof(setting));
```

Call these services from the main activity task, never a render task or BLE
callback. Reads return 0 with length 0 if absent, invalid, or too large for the
buffer; retain your defaults. Writes return 0 on failure and skip unchanged
blobs. Do not retry continuously or write on every frame; debounce frequently
changing values. Reads do not create default records.

Storage uses the existing internal NVS partition under the `plugins` namespace,
survives module unload and normal restart, and never erases NVS or changes the
partition table on an error. It is shared finite storage, not a per-plugin
security boundary; do not store secrets or bulk data here.

## Generic BLE API

Include the plugin-owned wrapper:

```cpp
#include <crosspoint/PluginBle.h>

class ExampleActivity final : public Activity {
 public:
  ExampleActivity(GfxRenderer& renderer, MappedInputManager& input)
      : Activity("Example", renderer, input) {}

  void onEnter() override {
    Activity::onEnter();
    ble_.start();
  }

  void loop() override {
    crosspoint_plugin::PluginBle::IncomingPacket packet{};
    while (ble_.poll(packet)) {
      // Validate your protocol's magic/version/type/length before use.
    }
    if (ble_.readyToSend()) {
      // ble_.send(packetBytes, packetLength);
    }
  }

  void onExit() override {
    ble_.stop();
    Activity::onExit();
  }

 private:
  crosspoint_plugin::PluginBle ble_{};
};
```

Only one active activity should own BLE. Poll regularly, obey `readyToSend()`,
keep packets at or below `maxPacketBytes()`, use `setTransferActive()` during
bursts, and stop in `onExit`. The firmware singleton outlives plugin activities
because NimBLE shutdown is asynchronous.

The firmware never interprets packet bytes. A plugin may define any bounded
application protocol. Terminal uses the plugin-owned
`include/pagewire/PageWireProtocol.h` and
[`PageWire v5 specification`](https://github.com/mekhontsev/pagewire/blob/main/docs/pagewire-protocol.md);
other authors do not need PageWire.

## Build and install

Terminal embeds nine generated sizes of IBM Plex Mono. Regenerate them from
the font's complete Unicode cmap with the sibling firmware checkout's EpdFont
converter:

```sh
python3 -m pip install -r ../crosspoint-reader/lib/EpdFont/scripts/requirements.txt
python3 scripts/generate-terminal-fonts.py
```

This is a development-time step only. The generated headers are committed, so
normal plugin builds do not need the TTF or Python font tooling.

1. Add sources under `src/<module>/`.
2. Add the module to the `sources` map in `scripts/build.py`.
3. Put reusable plugin-owned headers under `include/`.
4. Run `./bin/build` against a compatible sibling firmware checkout.

The build links modules independently, rejects unknown host symbols, appends the
ABI/length/SHA-256 trailer, and creates `build/crosspoint-plugins.zip`. Maximum
ELF payload is 2 MiB and at most 12 child modules are listed.

Copy a child to `/plugins/<module>.so` for direct testing, or install an eligible
child from the generated ZIP over the manager's Bluetooth updater. Manager
itself is replaced only through SD/Wi-Fi.

## Review checklist

- Fixed metadata fields and module name fit their limits.
- Exported entry points use C linkage and default visibility.
- Allocation failure is handled and large persistent buffers use PSRAM.
- `onExit` stops BLE/tasks/timers before destruction.
- Every external packet, offset, length, and enum is validated.
- UI and input follow firmware activity lifecycle.
- Plugin strings remain outside firmware.
- `./bin/build` reports no unknown host symbols.
