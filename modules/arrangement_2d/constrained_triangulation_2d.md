# ConstrainedTriangulation2D

`ConstrainedTriangulation2D` is a read-only, reference-counted CDT snapshot in the
`arrangement_2d` module. It uses that module's existing CGAL 6.1 installation,
EPECK kernel, `Exact_intersections_tag`, and `Constrained_triangulation_plus_2`.

## Construction

```text
build(vertices: PackedVector2Array,
      constraints: PackedInt32Array,
      frame_vertices: PackedInt32Array = []) -> ConstrainedTriangulation2D
```

`constraints` contains endpoint index pairs: `[a0, b0, a1, b1, ...]`. All vertices
participate, including points unused by constraints. Coordinates must be finite;
indices must address input vertices. Invalid arguments produce a Godot error and
return null.

CGAL constructs intersections and splits constraints at crossings, T-junctions,
and overlaps. Exact duplicate points merge; zero-length constraints contribute
only a vertex. Positive gaps remain gaps. Input point coordinates are converted
directly into the exact kernel. Geometry receives no tolerance-based snapping,
domain filtering, or quality refinement.

The caller can add an exterior frame to the input and identify its vertices with
`frame_vertices`. These indices only mark vertices. They do not create geometry
or constrained edges. Flags survive duplicate-point merging. Frame sizing and
which source polylines participate are caller policies.

The default constructor creates an empty snapshot. `get_dimension()` reports
`-1` for empty input, `0` for a single unique point, `1` for collinear input, and
`2` for a triangulated plane. Dimensions below 2 have vertices but no triangles.

## Graph export

`get_graph()` returns a fresh dictionary with these packed arrays:

| Key | Type | Meaning |
| --- | --- | --- |
| `vertices` | `PackedFloat64Array` | Interleaved coordinates `[x0, y0, x1, y1, ...]` of all finite output vertices. |
| `triangles` | `PackedInt32Array` | Three vertex indices per finite triangle, counterclockwise in coordinate space. |
| `opposites` | `PackedInt32Array` | Opposite halfedge index, or `-1` at the convex hull. |
| `constrained` | `PackedByteArray` | One 0/1 flag per halfedge indicating a source constraint. |
| `frame_vertices` | `PackedByteArray` | One 0/1 flag per output vertex marking caller-provided frame points. |

Halfedge `h = 3 * triangle + side` goes from `triangles[h]` to the next corner of
the same triangle. `opposites[h] / 3` is the neighboring triangle when an opposite
exists. Opposites reverse endpoints and have matching constraint flags. All
finite triangles are exported, including triangles in holes and outside source
contours. No infinite vertex is exported.

Indices remain stable for this snapshot's lifetime. They are not original input
indices and are not stable across builds. Exported coordinates are double
approximations of exact geometry; connectivity must use indices, not coordinate
comparisons. Godot's usual float `Vector2` cannot retain every constructed
intersection coordinate. The graph deliberately uses `PackedFloat64Array`.

Returned arrays use copy-on-write storage. Mutating the returned dictionary or
arrays does not mutate the native snapshot. Export once for repeated graph work.

## Point location

`locate(point: Vector2) -> int` uses exact predicates against the retained CDT:

- A nonnegative result is a triangle index in the exported graph.
- `OUTSIDE = -1` means outside the convex hull or a snapshot of dimension below 2.
- `ON_CONSTRAINT = -2` means exactly on a constrained edge or a vertex incident
  to one. This includes source endpoints and constructed intersection vertices.

On an unconstrained edge or vertex, location returns the lowest incident finite
triangle index. This also applies on an unconstrained convex-hull boundary.
Frame-adjacent triangles are ordinary triangles; callers interpret frame flags.
Invalid nonfinite query coordinates produce a Godot error and return `OUTSIDE`.

Point location does not classify a source polygon's interior, choose a fill
region, or interpret a gap threshold.

## Ownership and validation

Build with exclusive ownership and publish only the completed object. Serialize
native operations on one owning thread and keep a reference alive through each
operation. C# can retain the generated `RefCounted` wrapper and dispose it
after its operations finish. Independent snapshots have independent geometry.

Use the editor build tasks in `.vscode/tasks.json`. The focused module test runs
through the actual Godot binding without enabling engine unit tests:

```powershell
.\bin\godot.windows.editor.dev.x86_64.mono.console.exe --headless --path . --script modules/arrangement_2d/tests/constrained_triangulation_smoke.gd
```

The test covers full-graph connectivity, intersections, overlaps, degenerate
inputs, double-precision export, positive gaps, holes with retraced bridges,
point-location boundary rules, frame flags, and snapshot isolation.
