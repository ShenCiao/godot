#!/usr/bin/env python3
"""Publish editor bundles, optional export template archives, and a gdvm v2 registry."""

import argparse
import copy
import hashlib
import json
import re
import shutil
from pathlib import Path, PurePosixPath
from zipfile import ZIP_DEFLATED, ZipFile

PACKAGES = ("Godot.NET.Sdk", "Godot.SourceGenerators", "GodotSharp", "GodotSharpEditor")
PLATFORMS = {
    "windows-x86_64": ("windows-x86_64", "Godot.exe"),
    "linuxbsd-x86_64": ("linux-x86_64", "Godot_vCiallo.x86_64"),
    "macos-arm64": ("macos-arm64", "Godot_Ciallo"),
}


def assemble(editor_archive, destination, version, executable, template_metadata):
    with ZipFile(editor_archive) as editor:
        metadata = [name for name in editor.namelist() if name.endswith("GodotSharp/sdk.version")]
        if len(metadata) != 1 or editor.read(metadata[0]).decode().strip() != version:
            raise ValueError(f"SDK version mismatch in {editor_archive}")
        prefix = metadata[0][:-len("GodotSharp/sdk.version")]
        for package in PACKAGES:
            editor.getinfo(f"{prefix}GodotSharp/Tools/nupkgs/{package}.{version}.nupkg")
        binaries = [
            name for name in editor.namelist()
            if name.startswith(prefix) and "/" not in name[len(prefix):]
            and re.fullmatch(r"godot\..*\.editor\..*\.mono(?:\.exe)?", name[len(prefix):])
        ]
        if len(binaries) != 1:
            raise ValueError(f"Expected one editor executable in {editor_archive}: {binaries}")
        with ZipFile(destination, "w", compression=ZIP_DEFLATED) as bundle:
            for info in editor.infolist():
                if not info.filename.startswith(prefix):
                    continue
                name = info.filename[len(prefix):]
                if not name or info.is_dir() or name.endswith(".console.exe"):
                    continue
                if PurePosixPath(name).is_absolute() or ".." in PurePosixPath(name).parts or "\\" in name:
                    raise ValueError(f"Invalid archive path: {name}")
                output_info = copy.copy(info)
                output_info.filename = executable if info.filename == binaries[0] else name
                if output_info.filename == executable:
                    output_info.create_system = 3
                    output_info.external_attr = 0o100755 << 16
                output_info.compress_type = ZIP_DEFLATED
                with editor.open(info) as src, bundle.open(output_info, "w") as dst:
                    shutil.copyfileobj(src, dst)
            bundle.writestr("export-templates.json", json.dumps(template_metadata, indent=2) + "\n")


def publish_registry(packages, output, version, repository):
    if not re.fullmatch(r"\d+\.\d+\.\d+-ciallo\.g[0-9a-f]{9,40}", version):
        raise ValueError(f"Invalid SDK version: {version}")
    output.mkdir(parents=True, exist_ok=True)
    variants = {}
    for platform, (registry_platform, executable) in PLATFORMS.items():
        status = version.split("-", 1)[1]
        name = f"godot-{platform}-{status}.zip"
        templates_platform = "linux-x86_64" if platform == "linuxbsd-x86_64" else platform
        template_name = f"godot-export-templates-{templates_platform}-{status}.zip"
        template_archive = packages / template_name
        with ZipFile(template_archive) as templates:
            base = version.split("-", 1)[0]
            if templates.read("templates/version.txt").decode().strip() != f"{base}.{status}.mono":
                raise ValueError(f"Template version mismatch in {template_archive}")
        shutil.copyfile(template_archive, output / template_name)
        with template_archive.open("rb") as archive:
            template_digest = hashlib.file_digest(archive, "sha512").hexdigest()
        assemble(packages / name, output / name, version, executable, {
            "version": version,
            "sha512": template_digest,
            "url": f"https://github.com/{repository}/releases/download/v{version}/{template_name}",
        })
        with (output / name).open("rb") as archive:
            digest = hashlib.file_digest(archive, "sha512").hexdigest()
        variants[registry_platform] = {
            "sha512": digest,
            "size": (output / name).stat().st_size,
            "urls": [f"https://github.com/{repository}/releases/download/v{version}/{name}"],
        }
    files = {
        "registry.json": {"schema": 2, "name": "Ciallo Godot"},
        "index.json": {"schema": 2, "releases": [{
            "version": version, "variants": {"csharp": list(variants)}, "path": "release.json",
        }]},
        "release.json": {"schema": 2, "version": version, "variants": {"csharp": variants}},
    }
    for name, contents in files.items():
        (output / name).write_text(json.dumps(contents, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packages", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--repository", required=True)
    args = parser.parse_args()
    publish_registry(args.packages, args.output, args.version, args.repository)
