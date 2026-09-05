# Debugger module protocol

`debugger.so` is an ABI5 background service, not a selectable activity. The
generic host routes requests to it by module name `debugger`; the host does not
parse the payload below. For end-user commands and installation, see the
[shared diagnostic guide](https://github.com/mekhontsev/pagewire/blob/main/docs/user-guide.md#on-demand-diagnostics-over-bluetooth).

## Framing

Use the [generic service envelope](PLUGIN_DEVELOPMENT.md#background-service-modules)
on the auxiliary BLE channel. Integers below are unsigned little-endian.
Each module request starts with a one-byte operation. A module reply starts
with a one-byte application status: `0` success, `1` invalid request,
`2` unavailable state/file, `3` failed operation. This byte is distinct from
the outer host transport status; a host failure has no module reply.

No commands are retried automatically. A timeout after a mutation means its
outcome is unknown, not that it was rolled back. Read state before retrying.

## Operations

| Operation | Request after the opcode | Successful response after application status |
|---|---|---|
| `1` Status | Empty | UTF-8 JSON: `abi`, `uptime_ms`, `free_heap`, `free_psram`, `ble_status`, `dropped_packets` |
| `2` Provider | One-byte ID length, 1–15 ID bytes, nonempty provider request | Provider-defined body; provider supplies the application status too |
| `3` SD read | Four-byte offset, absolute UTF-8 path without NUL | Four-byte total file length, up to 192 bytes of data |
| `4` Settings read | 1–15 bytes of settings ID | Opaque saved blob, up to 128 bytes |
| `5` Recent logs | Four-byte offset | Four-byte snapshot length, up to 192 bytes of data |

Settings/provider IDs use `a-z`, `0-9`, `_`, `-`. SD paths are shorter than
128 bytes, must be absolute, and cannot contain dot segments, controls or
backslashes. SD reads use the host's read-only HalStorage service; files are
not held open across requests. A changing file is not an atomic snapshot.

Log offset zero captures the existing 16-entry firmware log ring into a fixed
4096-byte module buffer. Subsequent offsets read that snapshot until another
offset-zero request or module unload. The snapshot is module-wide, not per-client;
do not interleave log downloads. Build log level and ring overwrite still apply.

Status memory values are bytes and uptime is milliseconds. BLE states are
`0` stopped, `1` starting, `2` advertising, `3` pairing, `4` connected, `5` error.
Counters and uptime are runtime observations, not lifetime telemetry.

## Terminal provider

The Terminal activity registers provider ID `terminal` while it is open:

| Provider opcode | Extra request bytes | Result |
|---|---|---|
| `1` | None | JSON: `generation`, `revision`, `window_start`, `buffer_bytes`, `view_start`, `view_end`, `font`, `follow`, `keyboard`, `pending` |
| `2` | None | Queue a current-document refresh; success confirms queueing, not display completion |
| `3` | One-byte font index 0–8 | Set size `8 + 2 * index` and save it through the generic NVS service |

`window_start` is an absolute document byte offset; `view_start` and `view_end`
are relative to the displayed buffer. `follow`, `keyboard` and `pending` are
boolean flags encoded as 0/1. Font-save failure reports status `3`; the local
font may already have changed even though persistence failed.

## Lifetime and limits

Requests run cooperatively on the main activity task, including while a child
keyboard is open. The service shares the foreground BLE connection, has no
worker, timer, periodic sampling or unsolicited log output. Leaving Plugins
unloads it. The reference client rejects diagnostics during plugin installation.

This is not a CPU debugger: it cannot stop execution, inspect arbitrary
addresses, or recover a blocked main task. There are no arbitrary memory,
flash, bootloader, raw NVS-write or SD-write operations. After a crash/restart,
reopen Terminal and read `/crash_report.txt` if the normal crash handler saved it.
Native modules share the firmware address space and must be trusted.
