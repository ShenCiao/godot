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
