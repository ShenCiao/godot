"""Build matching Ciallo .NET packages and install local SDK development files."""

from __future__ import annotations

import argparse
import os
import runpy
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

REPOSITORY_ROOT = Path(__file__).resolve().parents[2]


def get_package_version(repository_root: Path, status: str | None, local_development: bool) -> str | None:
    version = runpy.run_path(str(repository_root / "version.py"))
    status = "ciallo.local" if local_development else (status or version["status"])
    if not status.startswith("ciallo."):
        return None
    return "{major}.{minor}.{patch}-".format(**version) + status


def install_sdk_files(module_dir: Path, output_dir: Path, local_development: bool) -> None:
    versions_file = module_dir / "SdkPackageVersions.props"
    sdk_version = ET.parse(versions_file).findtext("./PropertyGroup/PackageVersion_Godot_NET_Sdk")
    if not sdk_version:
        raise ValueError("The generated SDK package version is missing.")

    godotsharp_dir = output_dir / "GodotSharp"
    tools_dir = godotsharp_dir / "Tools"
    tools_dir.mkdir(parents=True, exist_ok=True)
    target_dir = tools_dir / "LocalDevelopment"
    if target_dir.resolve() != tools_dir.resolve() / "LocalDevelopment":
        raise ValueError("The local SDK installation directory must not redirect to another location.")

    with tempfile.TemporaryDirectory(prefix=".ciallo-sdk-", dir=tools_dir) as temporary_dir:
        staged_dir = Path(temporary_dir) / "LocalDevelopment"
        if local_development:
            staged_dir.mkdir()
            sdk_source = module_dir / "editor" / "Godot.NET.Sdk"
            generator_dir = sdk_source / "Godot.SourceGenerators"
            for source in (
                sdk_source / "Godot.LocalDevelopment.props",
                versions_file,
                generator_dir / "Godot.SourceGenerators.props",
                generator_dir / "bin" / "Release" / "netstandard2.0" / "Godot.SourceGenerators.dll",
            ):
                shutil.copy2(source, staged_dir)
            shutil.copytree(sdk_source / "Godot.NET.Sdk" / "Sdk", staged_dir / "Sdk")
            shutil.copy2(versions_file, staged_dir / "Sdk")

        if target_dir.exists():
            shutil.rmtree(target_dir)
        if local_development:
            staged_dir.rename(target_dir)

    # Git hooks and release packaging read this without executing the editor.
    with (godotsharp_dir / "sdk.version").open("w", encoding="utf-8", newline="\n") as metadata:
        metadata.write(sdk_version + "\n")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        epilog="Additional arguments are forwarded to modules/mono/build_scripts/build_assemblies.py.",
        allow_abbrev=False,
    )
    parser.add_argument("--godot-output-dir", type=Path, required=True)
    parser.add_argument("--local-development", action="store_true", help="Install a directly importable local SDK.")
    args, build_args = parser.parse_known_args(argv)

    output_dir = args.godot_output_dir.resolve()
    module_dir = REPOSITORY_ROOT / "modules" / "mono"
    package_version = get_package_version(
        REPOSITORY_ROOT, os.environ.get("GODOT_VERSION_STATUS"), args.local_development
    )
    command = [
        sys.executable,
        str(module_dir / "build_scripts" / "build_assemblies.py"),
        "--godot-output-dir",
        str(output_dir),
        "--no-deprecated",
        *build_args,
    ]
    build_env = os.environ.copy()
    if package_version:
        command += ["--package-version", package_version]
        # The optional upstream Godot .NET preview retains its development label.
        # The explicit version only selects the matching GodotSharp package family.
        build_env["GODOT_VERSION_STATUS"] = "dev"
    result = subprocess.run(command, env=build_env)
    if result.returncode != 0:
        return result.returncode

    install_sdk_files(module_dir, output_dir, args.local_development)
    return 0


if __name__ == "__main__":
    sys.exit(main())
