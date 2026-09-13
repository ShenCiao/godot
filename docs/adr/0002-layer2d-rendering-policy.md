# Use Layer2D for Ciallo scene layers

Status: Accepted

Use `Layer2D : Node2D` for Ciallo's container layers to keep application-specific
properties separate from the upstream CanvasGroup scene API. Retain the Sprite2D
Layer API and reuse the existing RD compositor.

Keep layer identity independent of offscreen composition. RendererCanvasCull
decides composition from properties and sibling relationships so default layers
can draw directly while Clipping Bases and local Z boundaries remain correct.
Making sibling clipping and custom ordering mutually exclusive preserves the
compositor's requirement that clipping-stack draws remain contiguous.

Supported behavior and configuration are defined in the
[Layer rendering contract](../rendering/layer-system.md).
