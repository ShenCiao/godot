# Layer Rendering

## Scope and API

The Layer renderer requires RenderingDevice. Compatibility/GLES3 is permanently
outside the product support boundary. Layer sources have one mip level;
`CanvasGroup.use_mipmaps` remains serialized and emits a warning when enabled.

| Node | Rendering role |
| --- | --- |
| `Layer2D` | Container Layer; children supply its content, including Control draws. |
| `Sprite2D` | Leaf Layer; its source consists of its own drawing commands. |
| `CanvasGroup` | Explicit offscreen container with the upstream scene API. |

Layer2D and Sprite2D expose `layer_blend_mode` and `clipping_mask`, backed by
`RenderingServer.canvas_item_set_layer_blend_mode()` and
`RenderingServer.canvas_item_set_clipping_mask()`. Both classes' blend enums use
`RenderingServer::CanvasItemLayerBlendMode` values. See the
[Layer2D API reference](../../doc/classes/Layer2D.xml) for individual properties.

## Automatic Composition

Layer2D retains its Layer identity in both direct and composite rendering.
Before generating the drawing list, RendererCanvasCull resolves sibling clipping
relationships and selects composition for non-default `self_modulate`, a
non-default blend override, an effective material, an active clipping relationship
(including being the Base), `COMPOSITE_MODE_ALWAYS`, or a required local ordering
boundary. Default layers otherwise draw into the current target.

A composited child contains its own internal ordering. Additional ordering that
would escape through ordinary Node2D children activates the enclosing Layer2D's
composition. The RD backend obtains textures from the existing scratch-buffer pool.

Changes take effect on the next render. `is_composite_active()` reports the most
recent visible canvas traversal, including when multiple viewports share a canvas.

## Viewport MSAA

Layer2D, CanvasGroup and clipping-stack offscreen targets use the active Viewport's
`msaa_2d` setting. A canvas shared by multiple Viewports uses each Viewport's own
sample count and buffer pool. The ProjectSettings default does not override an
explicit SubViewport setting.

With MSAA enabled, each pooled slot has a multisample color attachment and an
ordinary resolved texture. Both use the Viewport's color format, including HDR.
The standard RenderingDevice render pass resolves the color before a group is
sampled for composition. The source exposed as `TEXTURE` remains a single-sample,
associated-alpha texture. Nested passes preserve the parent's multisample content
until drawing resumes. Empty and reused slots are cleared to transparent.

Changing `msaa_2d` releases pooled attachments, including currently unused slots;
they are allocated again on demand. Disabling MSAA uses the single-sample path.
Viewport growth and HDR changes recreate matching attachments, while ordinary
resizes within existing capacity reuse storage. Sequential groups reuse slots;
simultaneously nested groups require separate multisample attachments. GPU memory
and resolve bandwidth therefore depend on Viewport size, sample count and nesting.

Every group is resolved at its composition boundary. MSAA antialiases the source
geometry; the group material samples its resolved image. This preserves the
isolated-layer semantics, rather than retaining per-sample coverage through the
entire layer hierarchy.

## Opacity, Materials and Blending

`self_modulate.a` supplies Layer Opacity through `COLOR`. For composited Layer2D,
it applies to the combined child result. `modulate` retains ordinary inherited
CanvasItem modulation. Custom shaders may replace `COLOR` and its alpha.

Material ownership follows `use_parent_material`. `DEFAULT` follows the resolved
shader's `render_mode blend_*`; the default shader uses Normal blending. Explicit
`NORMAL`, `ADD` and `MULTIPLY` overrides take precedence without changing the
material. Ciallo maps its ordinary Normal selection to `DEFAULT`.

The compositor uses associated-alpha color. Sprite2D shaders produce straight-alpha
`COLOR`, which the renderer premultiplies after fragment execution; declaring
`blend_premul_alpha` supplies associated output directly. CanvasGroup and composited
Layer2D shaders receive associated-alpha sources through `TEXTURE` and `UV`.
`UV` covers the group draw rectangle; `hint_screen_texture` uses the ordinary
screen/backbuffer. Blend overrides preserve these shader-facing conventions.

`Cs` and `Cd` are associated RGB; `As` and `Ad` are source and destination alpha:

```text
Normal:   Co = Cs + Cd * (1 - As)
Add:      Co = Cs + Cd
Subtract: Co = Cd - Cs
Multiply: Co = Cs * Cd + Cs * (1 - Ad) + Cd * (1 - As)
          Ao = As + Ad * (1 - As)  [all four modes]
Replace:  Co = Cs; Ao = As

Inside a clipping stack:
Normal:   Co = Cs * Ad + Cd * (1 - As)
Add:      Co = Cs * Ad + Cd
Subtract: Co = Cd - Cs * Ad
Multiply: Co = Cd * (Cs + 1 - As)
Replace:  Co = Cs * Ad
          Ao = Ad                [all modes]
```

`blend_mix` and `blend_premul_alpha` select Normal, `blend_sub` selects Subtract,
and `blend_disabled` selects Replace. Multiply uses a secondary RGB output of
`Cs + (1 - As)`, with `ONE_MINUS_DST_ALPHA` and `SRC1_COLOR` blend factors.
Vulkan requires `dualSrcBlend`; Metal uses Source1 blend-factor support.

## Clipping and Local Ordering

A clipping stack consists of direct sibling Layers. Its Base is the nearest
preceding non-clipped Layer in increasing sibling-index order (back to front).
Ciallo displays the reverse order, so the Base appears below its clipped layers.
Layer2D boundaries preserve this relationship during direct rendering too.
Missing Bases warn and give zero coverage. Hidden or viewport-masked Bases retain
their Base role with zero coverage.

The Base's final shader alpha defines coverage. The Base draws with Normal
composition into a transparent target; clipped layers preserve that coverage.
The result composites into its parent with the Base's effective blend mode.
Base shaders and opacity are evaluated once. Nested groups resolve internally
before supplying an outer Base or clipped source. Sequential stacks reuse scratch
textures; simultaneously nested stacks need separate textures.

Layer2D contains child Z ordering within its subtree using this fork's transparent
CanvasGroup traversal. The complete result participates in its parent's ordering
at the Layer2D node's Z. Equal Z retains sibling order; negative onion-skin Z is
supported. Ordinary content retains canvas ordering within the group.

Clipping requires direct sibling Layers to use `z_index = 0`,
`z_as_relative = true`, `show_behind_parent = false`, and `top_level = false`,
with `y_sort_enabled = false` on their parent. Structural sibling properties,
including hidden Layers, determine the constraint.

For a shared sibling group, conflicting Z, absolute Z, behind-parent drawing or
Y sorting takes priority: clipping is suspended with one warning while the
conflict persists. Stored clipping properties remain intact and clipping resumes
when the conflict clears. Groups are evaluated independently; a group's own Z in
its parent leaves its internal clipping available. The Ciallo GUI must prevent
users from creating these conflicting combinations.

## Verification

Run the [GPU regression project](../../misc/rendering_tests/layer_2d/README.md)
to check pixels, state transitions, 10,000-layer direct rendering, and offscreen
MSAA across nested groups, clipping stacks and independent Viewports.
Architecture rationale is recorded in [ADR 0002](../adr/0002-layer2d-rendering-policy.md).
