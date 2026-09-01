#!/usr/bin/env python3
"""Build the SD plugin bundle against a specific CrossPoint firmware tree."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shlex
import shutil
import struct
import subprocess
import sys
import zipfile
from pathlib import Path


ABI_VERSION = 3
TRAILER_FORMAT = 1
TRAILER_MAGIC = b"X4PLUG01"
DEFAULT_ENVIRONMENT = "x4pro-ble-terminal"
LINKER_SCRIPT = Path(__file__).resolve().parents[1] / "linker" / "plugin_sections.ld"


def run(arguments: list[str], cwd: Path | None = None) -> None:
    subprocess.run(arguments, cwd=cwd, check=True)


def output(arguments: list[str], cwd: Path | None = None) -> str:
    return subprocess.check_output(arguments, cwd=cwd, text=True).strip()


def quiet_output(arguments: list[str], cwd: Path | None = None) -> str:
    """Capture tool output while hiding harmless binutils diagnostics."""
    return subprocess.check_output(
        arguments, cwd=cwd, text=True, stderr=subprocess.DEVNULL
    ).strip()


def platformio_command() -> list[str]:
    executable = shutil.which("pio")
    if executable:
        return [executable]
    fallback = Path.home() / ".platformio" / "penv" / "bin" / "pio"
    if fallback.is_file():
        return [str(fallback)]
    raise SystemExit("PlatformIO CLI was not found")


def load_compile_template(firmware: Path, environment: str) -> list[str]:
    database = firmware / "compile_commands.json"
    build_marker = f".pio/build/{environment}/"

    def find_template() -> list[str] | None:
        try:
            entries = json.loads(database.read_text(encoding="utf-8"))
        except FileNotFoundError:
            return None
        except (OSError, json.JSONDecodeError) as error:
            raise SystemExit(f"could not read {database}: {error}") from error
        for entry in entries:
            command = entry.get("arguments")
            if command is None and isinstance(entry.get("command"), str):
                command = shlex.split(entry["command"])
            if not isinstance(command, list):
                continue
            file_name = str(entry.get("file", "")).replace("\\", "/")
            command_text = " ".join(str(value) for value in command)
            if file_name.endswith("src/activities/Activity.cpp") and build_marker in command_text:
                return [str(value) for value in command]
        return None

    template = find_template()
    if template is not None:
        return template
    run(platformio_command() + ["run", "-e", environment, "-t", "compiledb"], firmware)
    template = find_template()
    if template is not None:
        return template
    raise SystemExit(
        f"{database} has no {environment} compile command; run "
        f"pio run -e {environment} -t compiledb in the firmware tree"
    )


def compile_arguments(template: list[str]) -> list[str]:
    cleaned: list[str] = []
    skip_next = False
    for argument in template:
        if skip_next:
            skip_next = False
            continue
        if argument == "-o":
            skip_next = True
            continue
        if argument.endswith("src/activities/Activity.cpp"):
            continue
        cleaned.append(argument)
    return cleaned


def compile_source(
    base: list[str], source: Path, destination: Path, include_dir: Path, firmware: Path
) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    arguments = list(base)
    arguments.extend(
        [
            "-fPIC",
            "-fvisibility=hidden",
            "-DENABLE_BLE_TERMINAL=1",
            "-DENABLE_PLUGIN_BLE_HOST=1",
            "-DCROSSPOINT_PLUGIN_BUILD=1",
            f"-I{include_dir}",
            "-o",
            str(destination),
            str(source),
        ]
    )
    run(arguments, firmware)


def tool(compiler: Path, suffix: str) -> Path:
    name = compiler.name
    if not name.endswith("g++"):
        raise SystemExit(f"unexpected compiler name: {compiler}")
    result = compiler.with_name(name[:-3] + suffix)
    if not result.is_file():
        raise SystemExit(f"toolchain program was not found: {result}")
    return result


def allowed_host_symbols(firmware: Path) -> set[str]:
    manifest = firmware / "src" / "plugins" / "PluginHostSymbols.inc"
    try:
        text = manifest.read_text(encoding="utf-8")
    except OSError as error:
        raise SystemExit(f"could not read firmware plugin ABI: {error}") from error
    return set(re.findall(r'PLUGIN_HOST_SYMBOL\(\d+,\s*"([^"]+)"\)', text))


def undefined_symbols(nm: Path, module: Path) -> set[str]:
    text = output([str(nm), "-u", str(module)])
    return {line.split()[-1] for line in text.splitlines() if line.split()}


def link_module(
    compiler: Path,
    objects: list[Path],
    destination: Path,
    allowed: set[str],
    require_metadata: bool,
) -> None:
    raw = destination.with_suffix(".raw.so")
    run(
        [
            str(compiler),
            "-shared",
            "-fPIC",
            "-static-libgcc",
            "-nostdlib",
            "-nostartfiles",
            "-Wl,--gc-sections",
            "-Wl,--strip-all",
            "-Wl,--strip-debug",
            "-Wl,--strip-discarded",
            "-Wl,--allow-shlib-undefined",
            f"-Wl,-T,{LINKER_SCRIPT}",
            "-o",
            str(raw),
            *(str(item) for item in objects),
        ]
    )

    nm = tool(compiler, "nm")
    unknown = sorted(undefined_symbols(nm, raw) - allowed)
    if unknown:
        formatted = "\n  ".join(unknown)
        raise SystemExit(
            "plugin uses symbols that are absent from the firmware host ABI:\n  "
            + formatted
        )

    shutil.copyfile(raw, destination)
    strip = tool(compiler, "strip")
    run(
        [
            str(strip),
            "--strip-unneeded",
            "--remove-section=.comment",
            "--remove-section=.got.loc",
            "--remove-section=.dynamic",
            "--remove-section=.xt.lit",
            "--remove-section=.xt.prop",
            "--remove-section=.xtensa.info",
            str(destination),
        ]
    )
    readelf = tool(compiler, "readelf")
    exports = quiet_output(
        [str(readelf), "--dyn-syms", "--wide", str(destination)]
    )
    for symbol in ("crosspoint_plugin_abi", "crosspoint_plugin_create"):
        if symbol not in exports:
            raise SystemExit(f"required export is missing from {destination.name}: {symbol}")
    section_table = quiet_output(
        [str(readelf), "--sections", "--wide", str(destination)]
    )
    section_names = set(re.findall(r"\]\s+(\.[^\s]+)", section_table))
    if require_metadata and ".crosspoint.plugin" not in section_names:
        raise SystemExit(
            f"required metadata section is missing from {destination.name}: "
            ".crosspoint.plugin"
        )
    unsupported = section_names & {
        ".comment",
        ".dynamic",
        ".got.loc",
        ".xt.lit",
        ".xt.prop",
        ".xtensa.info",
    }
    if unsupported:
        raise SystemExit(
            f"{destination.name} still contains sections that the firmware "
            f"loader does not place in memory: {', '.join(sorted(unsupported))}"
        )

    section_sizes = {
        match.group(1): int(match.group(2), 16)
        for match in re.finditer(
            r"\]\s+(\.[^\s]+)\s+\S+\s+\S+\s+\S+\s+([0-9a-fA-F]+)",
            section_table,
        )
    }
    misaligned = [
        name
        for name in (".data", ".rodata", ".data.rel.ro")
        if section_sizes.get(name, 0) % 4 != 0
    ]
    if misaligned:
        raise SystemExit(
            f"{destination.name} has sections whose sizes break the firmware "
            f"loader's four-byte runtime alignment: {', '.join(misaligned)}"
        )

    elf = destination.read_bytes()
    trailer = struct.pack(
        "<8sIII32s",
        TRAILER_MAGIC,
        TRAILER_FORMAT,
        ABI_VERSION,
        len(elf),
        hashlib.sha256(elf).digest(),
    )
    with destination.open("ab") as stream:
        stream.write(trailer)
    raw.unlink()


def create_bundle(modules: list[Path], destination: Path) -> None:
    manifest: dict[str, object] = {
        "format": 2,
        "bundleVersion": "0.2.0",
        "pluginAbi": ABI_VERSION,
        "modules": {},
    }
    module_manifest: dict[str, object] = manifest["modules"]  # type: ignore[assignment]
    for module in modules:
        contents = module.read_bytes()
        module_manifest[module.name] = {
            "bytes": len(contents),
            "sha256": hashlib.sha256(contents).hexdigest(),
            "pluginAbi": ABI_VERSION,
            "bleUpdatable": module.name != "manager.so",
        }

    with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for module in modules:
            archive.write(module, f"plugins/{module.name}")
        archive.writestr(
            "plugins/bundle.json",
            json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        )
        archive.writestr(
            "README.txt",
            "Extract the plugins folder into the root of the X4 Pro SD card.\n"
            "The installed firmware must support the package's plugin ABI.\n",
        )


def main() -> int:
    project = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--firmware",
        type=Path,
        default=project.parent / "crosspoint-reader",
        help="matching CrossPoint Reader checkout",
    )
    parser.add_argument("--environment", default=DEFAULT_ENVIRONMENT)
    parser.add_argument("--output", type=Path, default=project / "build")
    arguments = parser.parse_args()

    firmware = arguments.firmware.resolve()
    output_dir = arguments.output.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    template = compile_arguments(load_compile_template(firmware, arguments.environment))
    compiler = Path(template[0])
    allowed = allowed_host_symbols(firmware)

    sources = {
        "manager": [
            project / "src" / "manager" / "PluginManagerEntry.cpp",
            project / "src" / "manager" / "PluginsActivity.cpp",
        ],
        "terminal": [
            project / "src" / "terminal" / "TerminalPluginEntry.cpp",
            project / "src" / "terminal" / "BleTerminalActivity.cpp",
        ],
    }

    modules: list[Path] = []
    for module_name, module_sources in sources.items():
        objects: list[Path] = []
        for index, source in enumerate(module_sources):
            object_path = output_dir / "obj" / module_name / f"{index}_{source.name}.o"
            compile_source(template, source, object_path, project / "include", firmware)
            objects.append(object_path)
        module = output_dir / f"{module_name}.so"
        link_module(compiler, objects, module, allowed, module_name != "manager")
        modules.append(module)

    bundle = output_dir / "crosspoint-plugins.zip"
    create_bundle(modules, bundle)
    print(f"built {bundle}")
    for module in modules:
        print(f"  {module.name}: {module.stat().st_size} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
