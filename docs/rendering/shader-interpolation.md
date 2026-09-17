# Fragment input interpolation

The shader language supports centroid-qualified varyings and explicit fragment
input interpolation in `canvas_item` and `spatial` shaders.

## Centroid qualifier

```glsl
varying centroid vec2 brush_position;

void vertex() {
    brush_position = VERTEX.xy;
}

void fragment() {
    // Interpolated at a location in the primitive's covered pixel region.
    COLOR = vec4(brush_position, 0.0, 1.0);
}
```

`centroid` selects a covered interpolation location for partially covered MSAA
pixels. The location is implementation-defined and is not necessarily the
geometric centroid. This qualifier retains perspective-correct interpolation and
ordinary pixel-frequency shading. Single-sample rendering normally uses the pixel
center. The qualifier applies to floating-point vertex-to-fragment varyings;
integer varyings require `flat`.

## Explicit interpolation functions

For each function, `T` is `float`, `vec2`, `vec3`, or `vec4`:

| Signature | Evaluation location |
| --- | --- |
| `T interpolateAtCentroid(T interpolant)` | A covered location in the current pixel |
| `T interpolateAtSample(T interpolant, int sample_index)` | The selected render-target sample |
| `T interpolateAtOffset(T interpolant, vec2 offset)` | Offset from the pixel center, in screen pixels |

The first argument must reference a non-flat varying assigned in `vertex()`.
Components/swizzles and indexed array elements are accepted. Local copies,
uniforms, expressions, and function parameters are not interpolants. Helpers may
call these functions using global varyings and must be reached only from the
fragment stage, including `light()` where applicable. Varyings assigned in
`fragment()` and shared with `light()` cannot be explicitly interpolated.

The caller must supply a sample index supported by the active render target; use
index zero for a single-sample target. Offset components should be in the portable
range `[-0.5, 0.5)`; hardware may quantize offsets. Selecting a sample explicitly
does not itself enable sample-frequency shader execution.

## Derivatives and brush coordinates

Keep ordinary interpolation for screen-space derivatives, then explicitly obtain
the covered position for brush evaluation:

```glsl
varying vec2 brush_position;
varying float brush_radius;

void vertex() {
    brush_position = VERTEX.xy;
    brush_radius = 1.0;
}

void fragment() {
    vec2 position_dx = dFdx(brush_position);
    vec2 position_dy = dFdy(brush_position);
    vec2 position = interpolateAtCentroid(brush_position);
    float radius = interpolateAtCentroid(brush_radius);
    // Use position_dx/position_dy for the pixel footprint and position/radius
    // for the pointwise brush evaluation.
}
```

Centroid interpolation changes the location of the requested input values. It
does not change `FRAGCOORD`, create missing rasterized fragments, compute pixel
coverage integrals, or identify shapes cut out procedurally inside a primitive.
Derivatives of centroid-qualified inputs can be unstable at primitive edges;
compute derivatives from ordinary center-interpolated inputs before divergent
control flow.

## Backends and validation

Explicit functions require the RenderingDevice renderers (Forward+ or Mobile).
Vulkan consumes the GLSL/SPIR-V interpolation operations. Direct3D 12 translates
them through the existing SPIR-V/NIR/DXIL path. Metal uses SPIRV-Cross pull-model
interpolation, requiring MSL 2.3 or later, which is within the supported Metal
device profiles. The centroid qualifier also maps to native GLSL interpolation.

The GPU regression and negative language-validation runner are documented in
`misc/rendering_tests/shader_interpolation/README.md`.
