# Project Context

This checkout is a custom Godot build for developing a paint&animation app like CSP or Krita.

## Rendering

Rendering work in this repository targets the RD renderer path; GLES3 compatibility does not need to be preserved.

Canvas item Z indexing is intentionally narrowed to 256 global buckets (`-128` to `127`) for this custom build for less memory allocation for nested canvas groups.

CanvasGroup compositing source means the image produced by a CanvasGroup from its children and then drawn by the group itself as a normal 2D texture.

CanvasGroup compositing source uses an associated-alpha color contract; its RGB is already multiplied by alpha because it is a composited render target.

CanvasGroup default drawing is ordinary CanvasItem textured-rect drawing, not a separate CanvasGroup material concept.

CanvasGroup compositing uses premultiplied alpha blending when drawing its compositing source back into the parent target.

CanvasItem clip-children masking is distinct from CanvasGroup compositing and keeps its own behavior.

CanvasGroup custom shaders access the compositing source through `TEXTURE` and `UV`, but the sampled color uses associated alpha rather than ordinary Sprite2D straight alpha.

CanvasGroup compositing source may be stored in a full render-target-sized buffer, but shader-facing UVs describe the group draw rect like an ordinary Sprite2D-style texture region.

CanvasGroup mipmap effects sample the compositing source through `TEXTURE` and `UV`, following ordinary 2D texture sampling semantics.

CanvasGroup mipmap effects are blur-oriented; when mipmaps are enabled, nearest texture filters are upgraded to linear mipmap filters for the CanvasGroup compositing source.

CanvasGroup compositing source is not exposed through `hint_screen_texture`; that hint keeps ordinary CanvasItem screen/backbuffer meaning.

CanvasGroup owner self-drawing is not supported in this custom build. A CanvasGroup is expected to let the engine-generated group rect draw its children texture; custom behavior should be expressed with a material/shader on the CanvasGroup, not with owner `_draw()` commands.
