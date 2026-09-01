# CrossPoint Plugins

Independently built SD-loaded plugins for the experimental Xteink X4 Pro
CrossPoint fork. Normal plugin development and updates do not require a firmware
reflash.

The complete system is split across:

- [`crosspoint-reader`](https://github.com/mekhontsev/crosspoint-reader): minimal
  firmware host and generic authenticated BLE service;
- this repository: manager, updater, Terminal, plugin SDK headers, and bundle;
- [`pagewire`](https://github.com/mekhontsev/pagewire): protocol plus the
  Android/Termux companion.

Use the single
[PageWire Terminal user guide](https://github.com/mekhontsev/pagewire/blob/main/docs/user-guide.md)
for installation, updates, controls, and recovery. See
[Plugin Architecture](ARCHITECTURE.md) and
[Plugin Development](PLUGIN_DEVELOPMENT.md) for implementation details.

> [!NOTE]
> This project is **AI vibe-coded** under maintainer direction, review, builds,
> and hardware testing.

The current **0.2.0** bundle uses plugin ABI **4** and contains:

- `manager.so`: lazy Plugins menu and Bluetooth child updater;
- `terminal.so`: PageWire v5 parser, continuous text cache, local layout,
  keyboard integration, and Terminal fonts.

The manager discovers child `.so` modules dynamically from their embedded
metadata. Neither manager nor firmware contains a hard-coded Terminal catalog.

## Install

Extract `crosspoint-plugins.zip` and copy its `plugins` directory to the SD card
root:

```text
/plugins/manager.so
/plugins/terminal.so
/plugins/bundle.json
```

After the manager is present, compatible child plugins can also be streamed
from a local ZIP using PageWire and **Plugins > Install via Bluetooth**.
`manager.so` remains an SD/Wi-Fi update so the updater cannot replace itself.

## Build

Place a compatible `crosspoint-reader` checkout beside this repository and run:

```sh
./bin/build
```

On Termux the wrapper uses the installed Ubuntu proot. Set
`CROSSPOINT_FIRMWARE_DIR` if the firmware checkout is elsewhere. The firmware
checkout supplies headers, compile flags, toolchain, and the host-symbol
allow-list; it does not build or link a firmware image.

Output: `build/crosspoint-plugins.zip`.

Every module carries ABI, ELF length, SHA-256, and metadata. The loader validates
them before listing/loading a child. Modules are relocated to PSRAM only when
selected, and BLE starts only while an owning plugin activity is open.

## License

Project code is MIT licensed. Generated IBM Plex Mono data derives from the SIL
Open Font License 1.1 font; see [FONT-LICENSE.txt](FONT-LICENSE.txt).
