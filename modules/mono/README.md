# How to build and run

1. Build Godot with the module enabled: `module_mono_enabled=yes`.
2. After building Godot, use it to generate the C# glue code:
   ```sh
   <godot_binary> --generate-mono-glue ./modules/mono/glue
   ```
3. Build the C# solutions:
   ```sh
   python misc/scripts/build_ciallo_assemblies.py --godot-output-dir ./bin
   ```

The paths specified in these examples assume the command is being run from
the Godot source root.

`misc/scripts/build_ciallo_assemblies.py` is the Ciallo build entry point used by
VS Code and release CI. It forwards build options to the upstream assembly builder,
selects the matching package version, and writes `GodotSharp/sdk.version` after a
successful build. Published builds use `GODOT_VERSION_STATUS=ciallo.g<git-hash>`;
local builds use `--local-development`. The upstream builder accepts the selected
version through `--package-version` for GodotSharp, Godot.NET.Sdk and source generators.

# Local C# development

The VS Code `Build .NET Assemblies` task builds the managed tools with
`--local-development`. The equivalent assembly build is:

```sh
python misc/scripts/build_ciallo_assemblies.py --godot-output-dir ./bin \
    --local-development
```

The editor output contains a fixed `GodotSharp/Tools/LocalDevelopment` directory:

- `Sdk/` contains the complete MSBuild SDK props and targets.
- `Godot.LocalDevelopment.props` references the matching API assemblies under
  `GodotSharp/Api` and the source generator beside the props file.
- `SdkPackageVersions.props` records the diagnostic package version and Godot constants.

A consuming project selects a local editor explicitly and stores its path in ignored
machine configuration. It reads that configuration before importing the SDK, imports
the local `Sdk.props`, then `Godot.LocalDevelopment.props`, and finally `Sdk.targets`.
The local version label is `<Godot version>-ciallo.local`; SDK selection uses the path.
Rebuilding in the same directory updates the next command-line build. Reload an IDE's
C# project or language service when it retains loaded assemblies or generators.

Published mode uses the team's SDK version through standard MSBuild resolution.
The editor preserves projects that use explicit `Sdk="Godot.NET.Sdk"` imports and
leaves their SDK selection to the project's configuration workflow.

# Double Precision Support (REAL_T_IS_DOUBLE)

Follow the above instructions but build Godot with the precision=double argument to scons

When building the NuGet packages, specify `--precision=double` - for example:
```sh
python misc/scripts/build_ciallo_assemblies.py --godot-output-dir ./bin \
    --local-development --precision=double
```
