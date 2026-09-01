# Plugin Development

CrossPoint plugins are native C++ ELF modules loaded from the X4 Pro SD card.
They are built independently from the firmware, but against a specific version
of the firmware's public plugin ABI. The current ABI version is **3**.

This is an experimental native extension mechanism, not a security sandbox. A
bad plugin can crash and restart the reader. An ordinary activity crash does not
put the module in the boot path: the normal firmware starts again, and removing
the offending `.so` from the SD card prevents it from being selected again.

## Module contract

A child module is stored as `/plugins/<module>.so`. Module names are 1 to 32
characters and may contain lowercase ASCII letters, digits, `-`, and `_` only.
`manager` is reserved for `/plugins/manager.so`.

Every child module exports the ABI reader, embedded menu metadata, and an
activity factory:

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
    "Example",  // menu title, up to 47 bytes plus NUL
    "0.1.0",    // display version, up to 15 bytes plus NUL
    20,         // lower values appear first
    0,
};

extern "C" __attribute__((visibility("default"))) Activity*
crosspoint_plugin_create(GfxRenderer* renderer,
                         MappedInputManager* mappedInput) {
  if (!renderer || !mappedInput) return nullptr;
  auto activity = makeUniqueNoThrow<ExampleActivity>(*renderer, *mappedInput);
  return activity.release();
}
```

The host takes ownership of the returned `Activity`. Allocate fallibly, return
`nullptr` on failure, and release every resource acquired by the activity when
it exits. The manager module has the same ABI and factory exports, but is the
one reserved module that does not carry child menu metadata.

Plugin-specific titles, messages, and other UI strings belong in the plugin.
The firmware does not need to know them. The current plugins are English-only;
the host's existing localization support remains available for shared firmware
strings.

## Available firmware services

The ABI deliberately exposes a small part of the firmware rather than every
symbol in its headers. Current plugins can use:

| Service | Available facilities |
|---|---|
| Activity lifecycle | `onEnter`, `loop`, `onExit`, redraw requests, child activities, and return to the main menu |
| Display and UI | screen size, text measurement and drawing, lines and rectangles, framebuffer display, theme metrics, centered and wrapped text, and plugin-registered fonts |
| Input | mapped hardware buttons, touch rectangles and long presses, and button-label mapping |
| Text input | the firmware-owned keyboard through the stable `crosspoint_plugin_*_keyboard_v2` functions |
| Localization | lookup of existing firmware strings through `I18n` |
| BLE | the shared authenticated PageWire GATT transport described below |
| Plugin management | dynamic child discovery, child activity creation, streamed install, and update status functions; primarily used by `manager.so` |
| Diagnostics and runtime | logging, time, free heap/PSRAM queries, allocation, a limited C/C++ runtime, and the small FreeRTOS subset needed by current modules |

[`PluginHostSymbols.inc`](https://github.com/mekhontsev/crosspoint-reader/blob/main/src/plugins/PluginHostSymbols.inc)
is the authoritative allow-list. A declaration being present in a firmware
header does not make its implementation part of the plugin ABI. In particular,
general storage, Wi-Fi, networking, and arbitrary NimBLE APIs are not currently
exported. The plugin build checks every unresolved symbol and fails before
packaging if a module calls anything outside the allow-list.

When a genuinely reusable host service is missing, add the smallest stable
entry point to the firmware ABI, add it to the allow-list, and increment the ABI
if the change is incompatible. Do not make a plugin depend on unrelated private
firmware internals.

## Using BLE and PageWire

Plugins share the firmware's lazily started BLE transport. It provides secure
pairing and the PageWire GATT service, so a plugin does not need to initialize
NimBLE, define characteristics, or implement authentication. The public C++
surface currently includes transport lifecycle and status, incoming packet
polling, backpressure checks, and PageWire actions, commands, frame requests,
frame status, and viewport messages.

A minimal activity owns the transport only while it is open:

```cpp
#include "ble/BleTerminalTransport.h"

class ExampleActivity final : public Activity {
 public:
  ExampleActivity(GfxRenderer& renderer, MappedInputManager& input)
      : Activity("Example", renderer, input),
        transport_(ble_terminal::sharedTransport()) {}

  void onEnter() override {
    Activity::onEnter();
    transport_.start();
  }

  void loop() override {
    ble_terminal::BleTerminalTransport::IncomingPacket packet;
    while (transport_.poll(packet)) {
      // Validate packet length, PageWire magic, version, type, and payload
      // before using packet.bytes.
    }
  }

  void onExit() override {
    transport_.stop();
    Activity::onExit();
  }

 private:
  ble_terminal::BleTerminalTransport& transport_;
};
```

Use `ble_terminal::MAGIC_0`, `MAGIC_1`, and `PROTOCOL_VERSION` when validating
PageWire envelopes; do not duplicate their byte values in a plugin. Follow the
[PageWire protocol specification](https://github.com/mekhontsev/pagewire/blob/main/docs/pagewire-protocol.md)
for packet formats and flow control.

Only one active activity should own the shared transport at a time. Poll it
regularly, respect `readyToSend()` before sending, keep transfers bounded, and
call `stop()` from `onExit`. The singleton itself intentionally outlives plugin
activities because shutdown of the underlying BLE host task is asynchronous.

This API is for the existing authenticated PageWire service. A new wire feature
normally requires coordinated protocol support in the plugin and its PageWire
client; it does not require a new BLE stack inside the plugin.

## Add and build a plugin

1. Add the activity and entry-point sources under `src/<module>/`.
2. Add the module and its source files to the `sources` map in
   [`scripts/build.py`](scripts/build.py).
3. Keep module-owned public helpers under `include/` and use firmware headers
   only for services that are part of the host allow-list.
4. Build against a compatible sibling firmware checkout:

   ```sh
   ./bin/build
   ```

The build uses the firmware's PlatformIO compile database and toolchain. It
links each module independently, validates its undefined symbols and required
metadata, appends the ABI/length/SHA-256 integrity trailer, and creates
`build/crosspoint-plugins.zip`.

The firmware independently checks the size, digest, trailer, ABI, ELF layout,
and metadata before listing or loading a child. The maximum ELF payload is 2
MiB, up to 12 children are listed, and runtime memory still needs to fit in the
reader's available PSRAM and heap.

## Install and recover

For direct testing, copy the generated child `.so` to `/plugins/<module>.so` on
the SD card. A compatible child can also be delivered in the generated ZIP over
PageWire using **Plugins > Install via Bluetooth**. The manager cannot update
itself while it is running and must be replaced through the SD card or Wi-Fi
file transfer.

If selection of a new child restarts the reader, remove that child's `.so`,
then inspect the crash report and rebuild. A compatible ABI and valid digest
prove package integrity and a shared contract; they cannot prove that native
plugin logic is bug-free.

## Review checklist

- The module name and descriptor fields fit their fixed-size limits.
- The entry points use C linkage and default visibility.
- Allocation failures return safely; long-lived buffers use PSRAM where
  appropriate.
- `onExit` stops BLE, tasks, timers, and other activity-owned work before
  destruction.
- Incoming packets and externally supplied lengths are fully validated.
- Rendering and input follow the firmware activity lifecycle.
- Plugin UI text remains in the plugin rather than the firmware.
- `./bin/build` completes without unknown host symbols.
