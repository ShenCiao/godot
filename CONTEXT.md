# Project Context

This checkout is a custom Godot build for developing a paint&animation app like CSP or Krita.

## Language

**Layer**:
An ordered compositing unit that produces a **Layer Source** and owns the properties applied to that result.

**Layer Source**:
The image produced by a **Layer** before its **Layer Blend Mode** combines it with lower Layers.

**Composite Layer**:
A **Layer** whose **Layer Source** combines zero or more ordered child Layers.

**Leaf Layer**:
A **Layer** whose **Layer Source** is not composed from child Layers.

**Layer Content**:
A drawable element included in a **Leaf Layer**'s source rather than independently ordered as a Layer.

**Layer Order**:
The authoritative ordering used to composite sibling Layers and resolve **Clipping Stacks**.

**Layer Blend Mode**:
The rule used to composite a **Layer**'s combined result with the accumulated Layers below it. Its result is defined when either input is transparent or partially transparent.

**Layer Opacity**:
A scalar coverage applied once to a **Layer**'s combined result.

**Clipping Base**:
A lower **Layer** whose effective alpha bounds the visible coverage of a **Clipped Layer**.

**Clipped Layer**:
An upper sibling **Layer** whose result may alter color only within its **Clipping Base**'s coverage.

**Orphaned Clipped Layer**:
A **Clipped Layer** with no eligible lower sibling **Clipping Base**.

**Clipping Coverage**:
The final alpha of a **Clipping Base**'s **Layer Source** after all source transformations, but before blending with lower Layers.

**Clipping Stack**:
A **Clipping Base** and its **Clipped Layers** composited as one unit whose alpha remains the Base's **Clipping Coverage**.

## Relationships

- Every **Layer** is either a **Composite Layer** or a **Leaf Layer**.
- A **Composite Layer** owns zero or more ordered child Layers.
- A **Leaf Layer** has no supported child Layer relationships.
- **Layer Content** belongs to a **Leaf Layer** and contributes to its single **Layer Source**.
- Both **Composite Layers** and **Leaf Layers** may be **Clipping Bases** or **Clipped Layers**.
- A **Layer** has one **Layer Blend Mode**.
- **Layer Order** determines which sibling Layers are above and below one another.
- **Layer Order** is the direct sibling order within a **Composite Layer**; other ordering mechanisms have no defined Layer behavior.
- A **Clipped Layer** uses one **Clipping Base**.
- A **Clipping Stack** contains exactly one **Clipping Base** and one or more consecutive upper sibling **Clipped Layers**.
- Consecutive **Clipped Layers** share the nearest lower sibling Layer that is not clipped as their **Clipping Base**.
- Every **Clipping Stack** is confined to Layers with the same direct parent and never crosses a **Composite Layer** boundary.
- A **Composite Layer** resolves its child Layers and their **Clipping Stacks** into one **Layer Source** before participating in its parent's Layer Order or Clipping Stack.
- Layer visibility does not change **Clipping Stack** membership or **Clipping Base** selection.
- A hidden **Clipping Base** provides zero **Clipping Coverage** to its **Clipping Stack**.
- An **Orphaned Clipped Layer** retains its clipping relationship but has zero coverage until it gains a **Clipping Base**.
- **Clipping Coverage** follows final **Layer Source** alpha even when a source transformation overrides **Layer Opacity**.
- A **Clipped Layer** uses its **Layer Blend Mode** within its **Clipping Stack**.
- A **Clipping Base** uses its **Layer Blend Mode** to composite its entire **Clipping Stack** with the accumulated Layers below it.

## Example dialogue

> **Dev:** "Can a **Clipped Layer** make pixels visible outside the **Clipping Base** or increase its alpha?"
> **Domain expert:** "No. It may change color only within the **Clipping Coverage** established by the Base."

## Flagged ambiguities

- "Clipping mask" can mean either parent-child clipping or sibling Layer clipping; the Layer vocabulary uses **Clipping Base** and **Clipped Layer** for the latter.
- **Leaf Layer** means only that its **Layer Source** is not composed from child Layers; it does not imply a drawable subtree.

## Build
Use "Build: Windows Debug" in .vscode/tasks.json

## Native File Dialogs

Runtime `FileDialog` nodes always use the operating system's native file dialog on Windows, macOS, and Linux. Setting `use_native_dialog` to `false` does not disable native dialogs.

The built-in Godot file browser controls are reserved for `EditorFileDialog`. Runtime `FileDialog` instances do not create those controls, so customization and display properties retain serialization compatibility but do not alter the native dialog, and internal control getters return null.

Native dialog availability is required. An unavailable native backend completes the request as canceled instead of falling back to the built-in Godot file browser. Linux deployments require a working XDG desktop portal FileChooser implementation.

## Rendering

Rendering work in this repository targets the RD renderer path. The Compatibility/GLES3 renderer is permanently outside the product support boundary; new rendering features must not add fallbacks or preserve Compatibility behavior.

Canvas item Z indexing is intentionally narrowed to 256 global buckets (`-128` to `127`) for this custom build for less memory allocation for nested canvas groups.

The [Layer rendering contract](docs/rendering/layer-system.md) is authoritative for Layer2D composition, local Z boundaries and sibling clipping conflicts.

INSTANCE_TRANSFORM is the per-instance transform matrix exposed to a CanvasItem vertex shader for the currently drawn MultiMesh instance. Avoid using "model matrix" for this concept: INSTANCE_TRANSFORM is only the per-instance part, separate from the node or canvas item's model transform.

## Arrangement 2D

An arrangement face is a finite or infinite 2D region separated by user-authored polylines.

A triangle result is a finite triangle mesh for a repaired 2D region: a point pool plus triangle index triples into that point pool.
