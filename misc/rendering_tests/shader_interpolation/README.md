# Shader interpolation regression

Run with a built RenderingDevice editor and a real GPU:

```powershell
bin/godot.windows.editor.x86_64.mono.console.exe --path misc/rendering_tests/shader_interpolation --rendering-driver vulkan --script check.gd
bin/godot.windows.editor.x86_64.mono.console.exe --path misc/rendering_tests/shader_interpolation --rendering-driver d3d12 --script check.gd
bin/godot.windows.editor.x86_64.mono.console.exe --path misc/rendering_tests/shader_interpolation --rendering-driver vulkan --script validation.gd
```

On macOS, use the corresponding editor executable and `--rendering-driver metal`.
The rendering checks require Forward+ or Mobile and must run with a real GPU;
headless dummy rendering cannot validate them.

`check.gd` checks the centroid qualifier and all three explicit interpolation
functions, scalar/vector overloads, swizzles, array elements, offset displacement,
different sample indices, and partial coverage at disabled/4x/8x MSAA. It prints
`INTERPOLATION_REGRESSION checks=... failures=0` on success.

`validation.gd` checks invalid interpolants and stage usage, plus a valid helper
that accesses a global varying before the vertex function declaration. It prints
`EXPECT_SHADER_ERROR` before each deliberately rejected shader and finishes with
`INTERPOLATION_VALIDATION checks=... failures=0`. Expected shader diagnostics are
part of this negative test; a nonzero process exit indicates an actual failure.
