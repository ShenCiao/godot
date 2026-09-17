# Layer2D rendering regression

Run from the engine repository root with a built RenderingDevice editor and a real GPU:

```powershell
bin/godot.windows.editor.dev.x86_64.mono.console.exe --path misc/rendering_tests/layer_2d --rendering-driver d3d12
```

The project checks GPU pixels for Base coverage, opacity and materials, local Z,
onion-skin settings, clipping conflicts and recovery, nested clipping, and
Sprite2D interoperability. It also checks that 10,000 default layers draw directly.

Success prints `LAYER_REGRESSION checks=... failures=0` and exits with code 0.
Invalid-input cases deliberately emit two orphan warnings and two ordering
warnings, each announced by an `expect ... warning` line. Failures exit nonzero.

Run the same blend/opacity/clipping suite with a 4x multisampled target:

```powershell
bin/godot.windows.editor.x86_64.mono.console.exe --path misc/rendering_tests/layer_2d --rendering-driver d3d12 -- --msaa-4x
```

For focused offscreen MSAA checks:

```powershell
bin/godot.windows.editor.x86_64.mono.console.exe --path misc/rendering_tests/layer_2d --rendering-driver vulkan --script msaa.gd
bin/godot.windows.editor.x86_64.mono.console.exe --path misc/rendering_tests/layer_2d --rendering-driver d3d12 --script msaa.gd
```

`msaa.gd` compares direct geometry with Layer2D, CanvasGroup and clipping Base
coverage. It checks nested parent continuation, empty and moved groups, sequential
scratch reuse, disabled/2x/4x/8x transitions, HDR/LDR, capacity growth/shrink, and a
shared World2D drawn by two Viewports with different sample counts. Success prints
`LAYER_MSAA_REGRESSION checks=... failures=0` and exits with code zero. It also uses
centroid interpolation to check partial-coverage fragment evaluation. On macOS,
run with the matching editor binary and `--rendering-driver metal`.
