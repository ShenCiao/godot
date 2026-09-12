# Ciallo custom godot

Bug fixes:
- Fix dialog placement on multi-monitor setups: `Window.initial_position` is applied only the first time a `Window` instance is shown, reopening it preserves the user's latest position and size, and an explicit `popup(Rect2i)` still takes precedence. Off-screen fallback centering now also respects each display's virtual desktop origin.
- Preserve captured Godot object references across C# editor assembly reloads, including internal controls such as `SpinBoxLineEdit`. See [C# delegate captures during editor reload](#c-delegate-captures-during-editor-reload).
- Align `CheckBox` and `CheckButton` icons with the largest StyleBox margins when `align_to_largest_stylebox` is enabled. This keeps icon placement stable across normal, hover, and pressed states; Ciallo enables the constant globally for consistent layer-button layout.

Features:
- Pen stylus subpixel coordinate
- Touch as individual event, separated from mouse event on Windows
- Add CGAL Arrangement2D
- Add `INSTANCE_TRANSFORM` canvas_item vertex shader builtin for reading the current MultiMesh instance transform. When a canvas_item shader reads `INSTANCE_TRANSFORM`, the 2D MultiMesh instance transform is not folded into `MODEL_MATRIX`; the shader owns how to apply or repurpose it. (This tortured me a whole freaking year.)
- Layer2D/Sprite2D layer system with nested layers, lazy direct rendering for default Layer2D nodes, automatic transparent composition for opacity, non-default blend modes, clipping masks, layer materials, or explicit always-composite mode, plus custom shaders. 

The layer rendering contract is documented in [docs/rendering/layer-system.md](docs/rendering/layer-system.md). The architectural decision behind the Ciallo scene-facing `Layer2D` node is recorded in [ADR 0002](docs/adr/0002-layer2d-rendering-policy.md).
- Spinbox add max display decimals
- Runtime `FileDialog` nodes always use the operating system's native file dialog.

## C# development

The `Ciallo Godot Release` workflow publishes an editor bundle and matching export
templates for the current `origin/ciallo` commit. The immutable release tag is
`v4.6.2-ciallo.g<9-character-git-hash>`. Each Release includes a gdvm v2 registry
(`registry.json`, `index.json`, `release.json`) with SHA-512 hashes and platform URLs.

Each bundle includes matching export templates, `GodotSharp/sdk.version` and the corresponding `Godot.NET.Sdk`,
`Godot.SourceGenerators`, `GodotSharp`, and `GodotSharpEditor` packages under
`GodotSharp/Tools/nupkgs`. These are standard NuGet packages and can be used as a
local package source with `dotnet nuget add source <bundle>/GodotSharp/Tools/nupkgs`.

Projects can use `<Project Sdk="Godot.NET.Sdk">` with the SDK version in the nearest
`global.json` under `msbuild-sdks.Godot.NET.Sdk`. The editor respects that version and
reports a mismatch with the selected editor. For projects that pin their SDK directly
in `.csproj`, the C# menu provides explicit SDK version synchronization.

Ciallo's Git hooks use gdvm to install the selected bundle and configure its local NuGet source.
The complete developer setup and dependency publication workflow is maintained in
[Ciallo's engine setup guide](https://github.com/CialloPaint/Ciallo/blob/main/docs/engine-setup.md).

### Local C++/C# workflow

1. Build the engine with the VS Code task `Build: Windows Debug Gen Glue`. Its .NET
   step uses `--local-development=ciallo`, derives `ciallo.g<current-git-hash>`, and
   installs the local development files in `bin/GodotSharp/Tools/LocalDevelopment`.
2. In the Ciallo checkout run `./engine.sh local <path-to-editor>` once, then
   `./engine.sh sync` after rebuilding managed assemblies. The hook-generated local
   SDK configuration selects this build without changing the team's root pin.
3. Open the project with the selected editor. `GodotTools` refreshes
   `.godot/mono/local_sdk.props` to reference the local API assemblies and generator.
4. Reload an already-open IDE to refresh SDK and assembly resolution.

### C# delegate captures during editor reload

C# closures preserve captured Godot object identity across editor assembly reloads. `DelegateUtils`
stores captured values in native Variant containers, which also keep captured `RefCounted` objects
alive while the project assembly unloads. Supported captures include internal objects such as
`SpinBox.GetLineEdit()` (`SpinBoxLineEdit`), nested Godot arrays and dictionaries, and C# arrays of
Godot objects. Method and type metadata remain binary data in this in-memory reload snapshot.

A freed direct Godot object capture causes that delegate to be rejected during saving or restoration.
Deliberately null captures remain valid, and unsupported managed capture types are rejected. This
avoids the failed object-encoding and subsequent empty-buffer decoding errors during closure reload.

**Remaining signal limitation:** `ManagedCallable` hashes can still change across assembly reloads.
An existing C# event subscription can continue firing while `IsConnected` returns `false` and
`-=` or `Disconnect` cannot remove it. Tool scripts that require reliable connection checks and
disconnection across reloads should retain named `Callable` connections, such as
`new Callable(this, MethodName.OnChanged)`, together with their lifecycle and idempotent binding logic.

## License

AGPLv3 since using CGAL.
