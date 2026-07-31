# Layer Rendering

## Supported Configuration

Layer rendering does not use mipmaps. `CanvasGroup.use_mipmaps` is retained for serialization compatibility, but `true` has no rendering effect and emits a one-time warning.

## Layer Opacity

`self_modulate.a` is the Layer Opacity for both `CanvasGroup` and `Sprite2D` Layers. It is supplied through the standard CanvasItem `COLOR` input. The default shader honors it, while a custom shader may replace `COLOR` and alter or bypass the resulting opacity without a separate opt-out mode.

`modulate` retains ordinary inherited CanvasItem modulation and is not interpreted as Layer Opacity. Supported Node2D Layer rendering requires `CanvasGroup.modulate` to remain `Color(1, 1, 1, 1)`; other values are not validated or corrected and have undefined behavior. This restriction does not apply to `self_modulate` or ordinary Control GUI rendering.

## Blend Selection

`DEFAULT` means that the Layer supplies no runtime blend override. The renderer follows ordinary Godot CanvasItem material resolution: `use_parent_material` selects the material owner, a `ShaderMaterial` supplies its canvas shader, and a `CanvasItemMaterial` supplies its generated canvas shader. A missing or invalid material uses the default canvas shader, whose blend mode is `blend_mix`.

The resolved canvas shader's `render_mode blend_*` declaration supplies the effective Layer Blend Mode for `DEFAULT`. The Layer compositor maps that semantic mode to its associated-alpha blend equation; `blend_mix`, for example, maps to Normal rather than applying straight-alpha factors to an already associated Layer Source.

`DEFAULT` preserves every resolved canvas shader blend declaration. `blend_premul_alpha` selects Normal composition and declares that a `Sprite2D` shader's output is already associated. `blend_sub` uses the associated-alpha Subtract equation. `blend_disabled` disables blending, so the Layer Source replaces the destination, including its alpha.

The runtime Layer Blend Mode values are `DEFAULT`, `NORMAL`, `ADD`, and `MULTIPLY`. A non-`DEFAULT` value stored on the Layer node takes precedence over the resolved shader mode without changing the Material resource or recompiling the shader. The renderer selects or caches the matching pipeline variant.

Writing `COLOR` changes the source value supplied to blending and may override opacity, but it does not replace the source/destination blend equation selected by the effective Layer Blend Mode.

## Blend Equations

Layer blending uses associated-alpha colors. `Cs` and `Cd` are the associated source and destination RGB values, while `As` and `Ad` are their alpha values.

```text
Normal:   Co = Cs + Cd * (1 - As)
Add:      Co = Cs + Cd
Subtract: Co = Cd - Cs
Multiply: Co = Cs * Cd + Cs * (1 - Ad) + Cd * (1 - As)

Normal, Add, Subtract, Multiply:
Ao = As + Ad * (1 - As)

Replace:
Co = Cs
Ao = As
```

Multiply uses a dual-source fragment output. The secondary RGB output is `Cs + (1 - As)`; the pipeline uses `ONE_MINUS_DST_ALPHA` for the source factor and `SRC1_COLOR` for the destination factor. Vulkan rendering therefore requires the core `dualSrcBlend` feature. The Metal backend uses its existing Source1 blend-factor support.

## Scene API

`Sprite2D` and `CanvasGroup` each expose `layer_blend_mode` directly, with `set_layer_blend_mode()` and `get_layer_blend_mode()`. The property is not placed in an additional Inspector group. Each class exposes its own `LayerBlendMode` enum aliases backed by the same `RenderingServer::CanvasItemLayerBlendMode` values.

The low-level API is `RenderingServer.canvas_item_set_layer_blend_mode(item, mode)`. The renderer stores the selected mode once on the canvas item. Other `CanvasItem` types do not expose the scene-level Layer Blend Mode API.

`Sprite2D` and `CanvasGroup` also expose the direct boolean property `clipping_mask`, with `set_clipping_mask()` and `is_clipping_mask()`. It defaults to `false`. The low-level API is `RenderingServer.canvas_item_set_clipping_mask(item, enabled)`. A Clipping Base is inferred from direct sibling Layer order and has no separate property.

The Layer Clipping Mask API is independent of `RenderingServer::CANVAS_GROUP_MODE_CLIP_ONLY`.

## Alpha Representation

The Layer compositor receives every Layer Source in associated-alpha form.

`Sprite2D` shaders retain ordinary Godot straight-alpha `COLOR` semantics. After custom fragment code, the renderer premultiplies the final RGB by the final alpha before supplying the result to the Layer compositor. A `Sprite2D` shader that declares `render_mode blend_premul_alpha` is treated as already producing associated color and is not premultiplied again.

`CanvasGroup` shaders retain their associated-alpha `COLOR` contract and are not premultiplied again. A runtime Layer Blend Mode override changes only the blend equation; it never changes the shader-facing alpha representation or the required normalization.

A `CanvasGroup` custom shader reads the resolved Layer Source through `TEXTURE` and `UV`. `UV` describes the group draw rect even when the scratch texture uses full render-target dimensions. `hint_screen_texture` retains ordinary CanvasItem screen/backbuffer semantics and never aliases the Layer Source.

## Clipping Coverage

A Clipping Base's coverage is its fragment shader's final `COLOR.a` before the Layer Blend Mode combines it with lower Layers. If a custom shader overwrites the opacity supplied through `COLOR`, its resulting alpha is authoritative for clipping. A Clipped Layer may use its own final alpha while modifying color inside the stack, but the Clipping Stack's alpha remains the Base coverage.

For a Clipped Layer, `Cd` and `Ad` are the current Clipping Stack values and `Ad` remains unchanged:

```text
Normal:   Co = Cs * Ad + Cd * (1 - As)
Add:      Co = Cs * Ad + Cd
Subtract: Co = Cd - Cs * Ad
Multiply: Co = Cd * (Cs + 1 - As)
Replace:  Co = Cs * Ad
Ao = Ad
```

## Nested Clipping Stacks

A Clipping Stack contains only direct sibling Layers. It never selects a Base across a `CanvasGroup` boundary. A Composite `CanvasGroup` completely resolves its child Layers and their Clipping Stacks into one Layer Source before that source participates in the parent's Layer Order or Clipping Stack.

If the `CanvasGroup` is a Clipped Layer, the outer Base coverage constrains the group's entire resolved source. If the `CanvasGroup` is a Clipping Base, the resolved source's final alpha becomes the outer stack's Clipping Coverage.

Sequential Clipping Stacks at the same nesting depth reuse one full-target scratch texture. Each simultaneously nested stack depth requires another scratch texture.

The Base is drawn into a transparent scratch target with Normal composition. Clipped Layers are then drawn with the alpha-preserving equations above. The resolved scratch texture is composited once into its parent using the Base's effective Layer Blend Mode and a pass-through unshaded material. Custom Base shaders and Layer Opacity are therefore evaluated exactly once.

## Content Ownership

`CanvasGroup` Layer content is rendered through the existing child `CanvasItem` traversal using ordinary canvas rendering, including content materials and internal blending. Direct `Node2D` children determine Layer classification; `Control` draws may contribute to the source without affecting that classification. The `CanvasGroup` owner issues no drawing commands and carries the Layer's composition properties and material.

A valid Leaf `CanvasGroup` combines all of its Layer Content into one Layer Source. The CanvasGroup's Layer Opacity, Layer Blend Mode, and clipping relationship apply to that combined result rather than to individual content items.

## Layer Classification

`Sprite2D` nodes are always Leaf Layers. A `Sprite2D` Layer Source is the node's own draw, not its descendant subtree. Descendant `Node2D` nodes are permitted in the scene tree, but their ordering, opacity, blending, and clipping relationships are outside the supported Layer semantics.

`Control` nodes do not participate in Layer classification. Existing CanvasGroup traversal is retained: a `Control` draw inside a `CanvasGroup` subtree contributes to that CanvasGroup's Layer Source and is consequently affected by the Layer's opacity, blend mode, and clipping relationship.

A `CanvasGroup` is a Composite Layer when it has no direct `Node2D` children or every direct `Node2D` child is `Sprite2D` or `CanvasGroup`. It is a Leaf Layer when it has at least one direct `Node2D` child and none of its direct `Node2D` children are `Sprite2D` or `CanvasGroup`. `Control` and non-CanvasItem helper children do not affect classification.

A `CanvasGroup` that mixes `Sprite2D` or `CanvasGroup` child Layers with other direct `Node2D` content is invalid. The renderer reports an error and continues without crashing; rendered output and all Layer behavior for that subtree are unspecified.

Classification is reevaluated whenever direct children change; entering a mixed configuration reports the same error.

## Layer Ordering

A Composite `CanvasGroup` composites child Layers strictly by direct scene-tree sibling index. Each child Layer must use `z_index = 0`, `z_as_relative = true`, `show_behind_parent = false`, and `top_level = false`. A Composite `CanvasGroup` that owns child Layers must use `y_sort_enabled = false`.

Violating one of these requirements emits one clear error. The renderer does not correct the serialized property value; rendered output is undefined, but the invalid configuration must not crash the engine. Any other mechanism that changes relative ordering within the Layer tree also has undefined Layer behavior without additional validation requirements.

Ordinary Layer Content in a Leaf `CanvasGroup` retains normal canvas z/y ordering within that Layer Source. A root `CanvasGroup` may use canvas ordering to position the complete Layer tree relative to rendering outside that tree.
