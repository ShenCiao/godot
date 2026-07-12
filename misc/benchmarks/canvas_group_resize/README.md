# CanvasGroup Resize Benchmark

This is a manual GPU benchmark for the RenderingDevice CanvasGroup path. It is
not an automated test: frame times depend on the GPU, driver, and operating
system, while Godot's SceneTree tests use a dummy renderer and cannot exercise
the framebuffer allocation path measured here.

Run it from the repository root with a freshly built console binary:

```powershell
bin\godot.windows.editor.dev.x86_64.console.exe --path misc\benchmarks\canvas_group_resize
```

Pass benchmark options after `--`:

```powershell
bin\godot.windows.editor.dev.x86_64.console.exe --path misc\benchmarks\canvas_group_resize -- --depth=6
bin\godot.windows.editor.dev.x86_64.console.exe --path misc\benchmarks\canvas_group_resize -- --mipmaps
bin\godot.windows.editor.dev.x86_64.console.exe --path misc\benchmarks\canvas_group_resize -- --upgrade-mipmaps
bin\godot.windows.editor.dev.x86_64.console.exe --path misc\benchmarks\canvas_group_resize -- --screen-texture
bin\godot.windows.editor.dev.x86_64.console.exe --path misc\benchmarks\canvas_group_resize -- --hidden-root
```

The default run models two visible branches with four nested CanvasGroups and
100 strokes per branch while resizing the SubViewport for 240 frames. Compare
the `SCRIPT_BENCH result` timings between builds on the same machine. In the
default and mipmap modes, the pixel line should report opaque content inside
the drawing and zero alpha outside it. The screen-texture mode adds full-canvas
gradient items, so its outside alpha is also opaque; `screen_red` checks
sampling near the logical viewport edge when the backing texture has larger
bucketed capacity.
