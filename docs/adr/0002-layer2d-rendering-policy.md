# Use Layer2D for Ciallo scene layers

Status: Accepted

## Decision

Ciallo's scene-facing layer role is provided by a new native `Layer2D` node that inherits `Node2D`. `Layer2D` does not inherit `CanvasGroup` and does not allocate an offscreen group in its constructor.

`Layer2D` uses an automatic rendering mode. A layer with default settings may render as an ordinary `Node2D`. A layer enters the existing transparent CanvasGroup path when its result needs layer semantics that cannot be preserved by direct child rendering, including non-unit layer opacity, non-default blending, clipping masks, a material on the layer node, or an explicit request to preserve exact CanvasGroup behavior. An explicit always-composite mode is retained as an escape hatch.

The native node updates the existing low-level canvas item state. C# supplies layer properties and does not replace a node with `Node2D` or `CanvasGroup`, and `IsDefault` is not a rendering authority. Toggling between direct and composite modes updates the existing CanvasItem RID and keeps the scene subtree intact.

`CanvasGroup` remains the upstream scene type for an explicitly composited container. Its scene-facing code and public properties should track the upstream Godot API; Ciallo layer properties are implemented on `Layer2D`. The current `Sprite2D` Layer API remains supported during the migration and is not removed or moved in the first conversion.

The migration reuses the existing RD Layer backend. It does not add a second renderer implementation or require new renderer algorithms. The Compatibility/GLES3 renderer is permanently unsupported for this product and requires no fallback behavior.

For Ciallo's current data model, ordinary `Normal` is treated as the default layer blend selection so that default layers remain eligible for direct rendering. Add, Multiply, clipping, non-unit opacity, layer materials, and explicit compatibility requests may force compositing. Content that depends on CanvasGroup-specific shader behavior must use the always-composite escape hatch.

## Consequences

Default folder and shape layers avoid CanvasGroup scratch buffers and extra compositing passes while preserving the existing behavior for layers that need isolation. Existing `Sprite2D` callers continue to work during the staged migration. The rendering backend remains a custom RD feature, but scene-level changes are isolated to `Layer2D` instead of modifying the upstream `CanvasGroup` contract.
