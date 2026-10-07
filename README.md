# Ciallo custom godot

Bug fixes:
- CanvasGroup offscreen rendering respects `Viewport.msaa_2d`, including nested groups and runtime MSAA changes.
- Prevent black rims at fractional offscreen layer boundaries by clearing the full covered pixel area when reusing pooled textures.
- Keep `MenuBar` popups aligned with their menu items under window content scaling, including forced native popups in embedded-window layouts.
- Fix dialog placement on multi-monitor setups: `Window.initial_position` is applied only the first time a `Window` instance is shown, reopening it preserves the user's latest position and size, and an explicit `popup(Rect2i)` still takes precedence. Off-screen fallback centering now also respects each display's virtual desktop origin.
- Preserve captured Godot object references across C# editor assembly reloads, including internal controls such as `SpinBoxLineEdit`. See [C# delegate captures during editor reload](#c-delegate-captures-during-editor-reload).
- Use consistent StyleBox margins for Button-family measurement and content placement. `align_to_largest_stylebox` selects either stable maximum margins or the current state's margins; state changes invalidate layout sizes before button signals run. Check and option icons reserve space using the same sizing rules, OptionButton caches content independently of state padding, and vertical icon layouts count text height once. See [Button's theme and property reference](doc/classes/Button.xml).

Features:
- Pen stylus subpixel coordinate
- Touch as individual event, separated from mouse event on Windows
- Add CGAL Arrangement2D and [ConstrainedTriangulation2D](modules/arrangement_2d/constrained_triangulation_2d.md), an immutable constrained Delaunay triangulation snapshot with graph export and exact-predicate point-location queries.
- Add `INSTANCE_TRANSFORM` canvas_item vertex shader builtin for reading the current MultiMesh instance transform. When a canvas_item shader reads `INSTANCE_TRANSFORM`, the 2D MultiMesh instance transform is not folded into `MODEL_MATRIX`; the shader owns how to apply or repurpose it. (This tortured me a whole freaking year.)
- [Layer2D/Sprite2D layers](docs/rendering/layer-system.md) with automatic composition, local Z ordering, opacity, blend modes, clipping masks, custom shaders and Viewport MSAA. Requires a RenderingDevice renderer (Forward+ or Mobile); offscreen buffers are pooled and reused across sequential groups and viewport resizes within allocated capacity.
- CanvasItem Z indices range from `-128` to `127`, using 256 buckets to reduce scratch memory for nested canvas groups. See [rendering constraints](CONTEXT.md#rendering).
- [Centroid and explicit fragment interpolation](docs/rendering/shader-interpolation.md) for precise shader input sampling with MSAA.
- Spinbox add max display decimals
- Runtime `FileDialog` nodes always use the operating system's native file dialog, with zero allocation of the custom file-browser node tree. This is a permanent merge requirement; see [Native File Dialogs](CONTEXT.md#native-file-dialogs).

## C# development

The `Ciallo Godot Release` workflow publishes an editor bundle and matching export
templates for the current `origin/ciallo` commit. The immutable release tag is
`v4.7.2-ciallo.g<9-character-git-hash>`. Each Release includes a gdvm v2 registry
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
   step runs `misc/scripts/build_ciallo_assemblies.py --local-development`, labels
   the SDK `4.7.2-ciallo.local`, and
   installs the local development files in `bin/GodotSharp/Tools/LocalDevelopment`.
2. In the Ciallo checkout run `./engine.sh local <path-to-editor>` once, then
   `./engine.sh sync` after rebuilding managed assemblies. The hook-generated local
   SDK configuration selects this build without changing the team's root pin.
3. Open the project with the selected editor. The C# project imports the local SDK,
   matching API assemblies and source generator directly. See the
   [local C# development guide](modules/mono/README.md#local-c-development) for the import contract.
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
