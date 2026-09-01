# CrossPoint Plugins

SD-loaded plugins for the experimental Xteink X4 Pro fork of CrossPoint
Reader. This repository is intentionally separate from the firmware: plugins
build independently and can be updated without reflashing the reader.

The complete system consists of:

- [`crosspoint-reader`](https://github.com/mekhontsev/crosspoint-reader) — the
  firmware fork and plugin host;
- this repository — the plugin sources and SD-card bundle;
- [`pagewire`](https://github.com/mekhontsev/pagewire) — the
  Android/Termux client and protocol specification.

See the shared
[PageWire and X4 Terminal user guide](https://github.com/mekhontsev/pagewire/blob/main/docs/user-guide.md)
for installation, controls, recovery, and current limitations. This software is
experimental, supports the **Xteink X4 Pro only**, and is not an official
CrossPoint or Xteink release.

See [Plugin Architecture](ARCHITECTURE.md) for the lazy-loading boundary,
independent update paths, integrity checks, failure containment, and the small
delta maintained against upstream firmware.

> [!NOTE]
> This project is **AI vibe-coded**: implementation and documentation are
> developed with AI coding agents under maintainer direction, review, builds,
> and hardware testing.

The current bundle contains:

- `manager.so` — the lazily loaded **Plugins** menu;
- `terminal.so` — the Bluetooth tmux terminal and its IBM Plex Mono fonts.

The current source version is **0.2.0** and uses plugin ABI **3**. A plugin is
accepted only when its ABI matches the installed firmware host.

## Install

For the first installation, extract `crosspoint-plugins.zip` into the root of
the reader's SD card. It creates:

```text
/plugins/manager.so
/plugins/terminal.so
/plugins/bundle.json
```

Once a compatible manager is installed, child plugins can also be updated over
BLE: select the ZIP in PageWire, then choose **Plugins > Install via
Bluetooth** on the reader. `manager.so` deliberately remains an SD-card update
so the updater cannot replace itself during a transfer.

## Build

Place a compatible `crosspoint-reader` checkout next to this repository, then
run:

```sh
./bin/build
```

On Termux the wrapper runs the build inside the installed Ubuntu proot. Set
`CROSSPOINT_FIRMWARE_DIR` when the firmware checkout is elsewhere. The build
uses the firmware's PlatformIO compile database and toolchain but never builds
or links a firmware image. All module `.cpp` files are owned by this repository;
the firmware checkout supplies headers, the compiler configuration, and the
host ABI manifest only.

The result is `build/crosspoint-plugins.zip`.

Every `.so` carries its ABI version, ELF length, and SHA-256 digest. The loader
requires a matching ABI and valid digest, so plugins can work across compatible
firmware revisions. Plugin code is loaded into PSRAM only after **Plugins** is
selected; Terminal starts Bluetooth only after its own row is selected.

The manager discovers child `.so` files dynamically from their embedded title,
version, and ordering metadata. The firmware reads this metadata without loading
or executing the plugin, and the manager does not contain a catalog of plugin
names.

## Licensing

Project code is MIT licensed. Generated IBM Plex Mono font data is derived
from the SIL Open Font License 1.1 font; see `FONT-LICENSE.txt`.
