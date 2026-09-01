# CrossPoint Plugins

SD-loaded plugins for the Xteink X4 Pro build of CrossPoint Reader. This
repository is intentionally separate from the firmware fork: firmware and
plugins build independently and are distributed as separate artifacts.

The current bundle contains:

- `manager.so` — the lazily loaded **Plugins** menu;
- `terminal.so` — the Bluetooth tmux terminal and its IBM Plex Mono fonts.

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

The result is `build/crosspoint-plugins.zip`. Extract it into the root of the
reader's SD card; this creates:

```text
/plugins/manager.so
/plugins/terminal.so
/plugins/bundle.json
```

Every `.so` carries its ABI version, ELF length, and SHA-256 digest. The loader
requires a matching ABI and valid digest, so plugins can work across compatible
firmware revisions. Plugin code is loaded into PSRAM only after **Plugins** is
selected; Terminal starts Bluetooth only after its own row is selected.

The manager discovers child `.so` files dynamically from their embedded title,
version, and ordering metadata. The firmware reads this metadata without loading
or executing the plugin, and the manager does not contain a catalog of plugin
names.
Choose **Install via Bluetooth** to install the updatable child modules from a
local bundle selected in a compatible client. `manager.so` is deliberately not
replaceable over Bluetooth; update it by copying a new bundle to the SD card.

## Licensing

Project code is MIT licensed. Generated IBM Plex Mono font data is derived
from the SIL Open Font License 1.1 font; see `FONT-LICENSE.txt`.
