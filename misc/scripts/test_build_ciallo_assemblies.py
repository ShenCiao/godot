"""Check Ciallo SDK build and installation contracts without running engine tests."""

from __future__ import annotations

import runpy
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

import build_ciallo_assemblies as builder


class CialloAssemblyBuildTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        (self.root / "version.py").write_text('major = 4\nminor = 7\npatch = 2\nstatus = "stable"\n')

    def make_sdk_sources(self):
        module_dir = self.root / "modules" / "mono"
        files = {
            "SdkPackageVersions.props": "<Project><PropertyGroup>"
            "<PackageVersion_Godot_NET_Sdk>4.7.2-ciallo.local</PackageVersion_Godot_NET_Sdk>"
            "</PropertyGroup></Project>",
            "editor/Godot.NET.Sdk/Godot.LocalDevelopment.props": "local imports",
            "editor/Godot.NET.Sdk/Godot.SourceGenerators/Godot.SourceGenerators.props": "generator imports",
            "editor/Godot.NET.Sdk/Godot.SourceGenerators/bin/Release/netstandard2.0/Godot.SourceGenerators.dll": (
                "generator"
            ),
            "editor/Godot.NET.Sdk/Godot.NET.Sdk/Sdk/Sdk.props": "sdk props",
            "editor/Godot.NET.Sdk/Godot.NET.Sdk/Sdk/Sdk.targets": "sdk targets",
        }
        for name, content in files.items():
            path = module_dir / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
        return module_dir

    def test_versions_preserve_hashes_and_local_mode_overrides_release_environment(self):
        for status in ("ciallo.g123456789", "ciallo.g6332e44d9", "ciallo.gabcdefabc"):
            with self.subTest(status=status):
                self.assertEqual(builder.get_package_version(self.root, status, False), "4.7.2-" + status)
                self.assertEqual(builder.get_package_version(self.root, status, True), "4.7.2-ciallo.local")
        for status in (None, "stable", "beta3", "dev2"):
            with self.subTest(status=status):
                self.assertIsNone(builder.get_package_version(self.root, status, False))

    def test_upstream_generator_uses_exact_override_for_all_matching_packages(self):
        source = builder.REPOSITORY_ROOT / "modules/mono/build_scripts/build_assemblies.py"
        generate = runpy.run_path(str(source))["generate_sdk_package_versions"]
        module_dir = self.make_sdk_sources()
        generate.__globals__["__file__"] = str(module_dir / "build_scripts" / "build_assemblies.py")
        info = dict(major=4, minor=7, patch=2, status="dev", docs_branch="4.7")
        with patch.dict(
            "sys.modules",
            {
                "methods": SimpleNamespace(get_version_info=lambda _: info),
                "version": SimpleNamespace(major=4, minor=7, patch=2),
            },
        ):
            for version in ("4.7.2-ciallo.g123456789", "4.7.2-ciallo.g6332e44d9", "4.7.2-ciallo.local"):
                with self.subTest(version=version):
                    generate(version)
                    props = ET.parse(module_dir / "SdkPackageVersions.props")
                    for name in ("GodotSharp", "Godot_NET_Sdk", "Godot_SourceGenerators"):
                        self.assertEqual(props.findtext("./PropertyGroup/PackageVersion_" + name), version)
                    self.assertEqual(props.findtext("./PropertyGroup/PackageVersion_GodotDotNet"), "4.7.2-dev")
                    self.assertIn("GODOT4_7_2", props.findtext("./PropertyGroup/_GodotVersionConstants"))

    def test_install_replaces_stale_sdk_files_and_keeps_api_assemblies(self):
        module_dir = self.make_sdk_sources()
        output_dir = self.root / "bin"
        target = output_dir / "GodotSharp" / "Tools" / "LocalDevelopment"
        target.mkdir(parents=True)
        (target / "obsolete.props").write_text("stale")
        api = output_dir / "GodotSharp" / "Api" / "Debug" / "GodotSharp.dll"
        api.parent.mkdir(parents=True)
        api.write_text("matching API")

        builder.install_sdk_files(module_dir, output_dir, True)
        self.assertFalse((target / "obsolete.props").exists())
        self.assertEqual((target / "Godot.SourceGenerators.dll").read_text(), "generator")
        self.assertEqual((target / "Sdk" / "Sdk.props").read_text(), "sdk props")
        self.assertEqual(
            (target / "Sdk" / "SdkPackageVersions.props").read_bytes(),
            (module_dir / "SdkPackageVersions.props").read_bytes(),
        )
        self.assertEqual((output_dir / "GodotSharp" / "sdk.version").read_text(), "4.7.2-ciallo.local\n")

        builder.install_sdk_files(module_dir, output_dir, False)
        self.assertFalse(target.exists())
        self.assertEqual(api.read_text(), "matching API")

    def test_incomplete_build_preserves_previous_local_sdk(self):
        module_dir = self.make_sdk_sources()
        output_dir = self.root / "bin"
        target = output_dir / "GodotSharp" / "Tools" / "LocalDevelopment"
        target.mkdir(parents=True)
        (target / "Sdk.props").write_text("previous working SDK")
        generator = module_dir / "editor/Godot.NET.Sdk/Godot.SourceGenerators/bin/Release/netstandard2.0"
        (generator / "Godot.SourceGenerators.dll").unlink()
        with self.assertRaises(FileNotFoundError):
            builder.install_sdk_files(module_dir, output_dir, True)
        self.assertEqual((target / "Sdk.props").read_text(), "previous working SDK")

    def test_cli_forwards_build_options_and_propagates_failure_without_installing(self):
        with (
            patch.object(builder, "REPOSITORY_ROOT", self.root),
            patch.dict(builder.os.environ, {"GODOT_VERSION_STATUS": "ciallo.g123456789"}),
            patch.object(builder.subprocess, "run", return_value=subprocess.CompletedProcess([], 7)) as run,
            patch.object(builder, "install_sdk_files") as install,
        ):
            code = builder.main([
                "--godot-output-dir",
                str(self.root / "bin"),
                "--local-development",
                "--godot-platform=windows",
                "--precision=double",
                "--werror",
            ])
            self.assertEqual(code, 7)
            install.assert_not_called()
            command = run.call_args.args[0]
            self.assertIn("--godot-platform=windows", command)
            self.assertIn("--precision=double", command)
            self.assertIn("--werror", command)
            self.assertNotIn("--local-development", command)
            self.assertEqual(command[-2:], ["--package-version", "4.7.2-ciallo.local"])
            self.assertEqual(run.call_args.kwargs["env"]["GODOT_VERSION_STATUS"], "dev")


if __name__ == "__main__":
    unittest.main()
