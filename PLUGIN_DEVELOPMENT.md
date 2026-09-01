# Plugin Development

CrossPoint plugins are native C++ ELF modules loaded from `/plugins` on X4 Pro.
They build independently against the firmware's public plugin ABI. The current
ABI is **4**.

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

## Host services

The ABI allow-list provides:

| Area | Facilities |
|---|---|
| Activity/UI | lifecycle, child activities, home return, screen geometry, text measurement/drawing, shapes, framebuffer refresh, theme metrics, fonts |
| Input | mapped hardware buttons, touch rectangles, long presses, labels |
| Keyboard | firmware-owned text keyboard and asynchronous result API |
| Plugin manager | metadata listing, child creation, streamed install |
| Generic BLE | authenticated raw packet lifecycle, poll/send, backpressure, MTU-sized cap, status, pairing code, drop count, link activity hint |
| Runtime | bounded logging, time, heap/PSRAM information, allowed allocation/C runtime and FreeRTOS primitives |

[`PluginHostSymbols.inc`](https://github.com/mekhontsev/crosspoint-reader/blob/main/src/plugins/PluginHostSymbols.inc)
is authoritative. A declaration in a private firmware header is not an exported
service. General storage, Wi-Fi, networking, and raw NimBLE APIs are not exposed.

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
