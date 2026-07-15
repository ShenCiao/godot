# Ciallo custom godot

Main changes:
- Pen stylus subpixel coordinate
- Touch as individual event, separated from mouse event on Windows
- Add CGAL Arrangement2D
- Add `INSTANCE_TRANSFORM` canvas_item vertex shader builtin for reading the current MultiMesh instance transform. When a canvas_item shader reads `INSTANCE_TRANSFORM`, the 2D MultiMesh instance transform is not folded into `MODEL_MATRIX`; the shader owns how to apply or repurpose it. (This tortured me a whole freaking year.)
- Allow nested CanvasGroup nodes, cut children's Z-Index, treat children content as a texture in shader replacing back buffer access.
(Note: This part is almost fully vibed. Although I know what is modified in rendering, I'm not capable to maintain/modify code manually)
- Spinbox add max display decimals
- Fix dialog placement on multi-monitor setups: `Window.initial_position` is applied only the first time a `Window` instance is shown, reopening it preserves the user's latest position and size, and an explicit `popup(Rect2i)` still takes precedence. Off-screen fallback centering now also respects each display's virtual desktop origin.

# License
AGPLv3 since using CGAL.
