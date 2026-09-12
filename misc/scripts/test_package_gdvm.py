"""Focused archive contract checks; run with python misc/scripts/test_package_gdvm.py."""

import json
import tempfile
import unittest
from pathlib import Path
from zipfile import ZipFile

from package_gdvm import PACKAGES, PLATFORMS, assemble, publish_registry


class BundleContract(unittest.TestCase):
    def test_all_platform_bundles_and_registry(self):
        version = "4.6.2-ciallo.g123456789"
        status = version.split("-", 1)[1]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for platform in PLATFORMS:
                prefix = "bin/" if platform.startswith("windows") else ""
                binary = f"godot.{platform.split('-')[0]}.editor.x86_64.mono"
                if platform.startswith("windows"):
                    binary += ".exe"
                with ZipFile(root / f"godot-{platform}-{status}.zip", "w") as archive:
                    archive.writestr(prefix + binary, "editor")
                    if platform.startswith("windows"):
                        archive.writestr(prefix + binary.replace(".exe", ".console.exe"), "console wrapper")
                    archive.writestr(prefix + "GodotSharp/sdk.version", version)
                    for package in PACKAGES:
                        archive.writestr(prefix + f"GodotSharp/Tools/nupkgs/{package}.{version}.nupkg", package)
                templates_platform = "linux-x86_64" if platform == "linuxbsd-x86_64" else platform
                with ZipFile(root / f"godot-export-templates-{templates_platform}-{status}.zip", "w") as archive:
                    archive.writestr("templates/version.txt", "4.6.2." + status + ".mono")
                    archive.writestr("templates/template", "export binary")
            output = root / "release"
            publish_registry(root, output, version, "CialloPaint/godot")
            registry = json.loads((output / "release.json").read_text())
            for platform, (registry_platform, executable) in PLATFORMS.items():
                path = output / f"godot-{platform}-{status}.zip"
                with ZipFile(path) as archive:
                    self.assertEqual(archive.read(executable), b"editor")
                    self.assertTrue(archive.getinfo(executable).external_attr >> 16 & 0o111)
                    self.assertEqual(archive.read("templates/template"), b"export binary")
                    self.assertFalse(any(name.endswith(".console.exe") for name in archive.namelist()))
                    for package in PACKAGES:
                        self.assertEqual(archive.read(f"GodotSharp/Tools/nupkgs/{package}.{version}.nupkg"), package.encode())
                self.assertEqual(registry["variants"]["csharp"][registry_platform]["size"], path.stat().st_size)
                self.assertEqual(len(registry["variants"]["csharp"][registry_platform]["sha512"]), 128)

    def test_rejects_mismatched_sdk_before_publication(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with ZipFile(root / "editor.zip", "w") as archive:
                archive.writestr("GodotSharp/sdk.version", "wrong-version")
            with ZipFile(root / "templates.zip", "w"):
                pass
            with self.assertRaisesRegex(ValueError, "SDK version mismatch"):
                assemble(root / "editor.zip", root / "templates.zip", root / "out.zip", "4.6.2-ciallo.g123456789", "Godot.exe")


if __name__ == "__main__":
    unittest.main()
