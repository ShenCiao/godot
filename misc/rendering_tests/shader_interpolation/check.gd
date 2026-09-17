extends SceneTree

var checks := 0
var failures := 0
var viewport: SubViewport


func _initialize() -> void:
	call_deferred("_run")


func _check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		failures += 1
		push_error(message)


func _shader(expression: String, qualifier: String = "") -> Shader:
	var shader := Shader.new()
	shader.code = """shader_type canvas_item;
render_mode unshaded;
varying %s vec2 point;
varying vec2 points[2];
varying float scalar;
varying vec3 triple;
varying vec4 quadruple;
void vertex() {
    point = VERTEX;
    points[0] = VERTEX;
    points[1] = VERTEX;
    scalar = VERTEX.x;
    triple = vec3(VERTEX, 1.0);
    quadruple = vec4(VERTEX, 1.0, 1.0);
}
void fragment() {
    vec2 pos = %s;
    COLOR = vec4(pos / 32.0, 0.0, 1.0);
}
""" % [qualifier, expression]
	return shader


func _render(shader: Shader, msaa: Viewport.MSAA, thin := false) -> Image:
	for child in viewport.get_children():
		child.free()
	viewport.msaa_2d = msaa
	var polygon := Polygon2D.new()
	if thin:
		polygon.polygon = PackedVector2Array([Vector2(2, 8.05), Vector2(30, 9.45), Vector2(30, 9.85), Vector2(2, 8.45)])
	else:
		polygon.polygon = PackedVector2Array([Vector2(0, 0), Vector2(32, 0), Vector2(32, 32), Vector2(0, 32)])
	var material := ShaderMaterial.new()
	material.shader = shader
	polygon.material = material
	viewport.add_child(polygon)
	await RenderingServer.frame_post_draw
	await RenderingServer.frame_post_draw
	return viewport.get_texture().get_image()


func _run() -> void:
	if RenderingServer.get_rendering_device() == null:
		push_error("Requires a real RenderingDevice GPU backend.")
		quit(1)
		return
	viewport = SubViewport.new()
	viewport.size = Vector2i(32, 32)
	viewport.world_2d = World2D.new()
	viewport.use_hdr_2d = true
	viewport.transparent_bg = true
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(viewport)
	for msaa in [Viewport.MSAA_DISABLED, Viewport.MSAA_4X, Viewport.MSAA_8X]:
		for expr in ["point", "interpolateAtCentroid(point)", "interpolateAtOffset(point, vec2(0.0))", "interpolateAtSample(point, 0)"]:
			var image := await _render(_shader(expr), msaa)
			var color := image.get_pixel(16, 16)
			_check(absf(color.r - 16.5 / 32.0) < 0.025 and color.a > 0.99, "Invalid pixel for %s MSAA=%d: %s" % [expr, msaa, color])
		var modifier := await _render(_shader("point", "centroid"), msaa, true)
		var explicit := await _render(_shader("interpolateAtCentroid(point)"), msaa, true)
		var mismatches := 0
		for y in range(32):
			for x in range(32):
				var a := modifier.get_pixel(x, y)
				var b := explicit.get_pixel(x, y)
				if absf(a.r-b.r) > 0.002 or absf(a.g-b.g) > 0.002 or absf(a.a-b.a) > 0.002:
					mismatches += 1
		_check(mismatches == 0, "Modifier and explicit centroid differ: MSAA=%d pixels=%d" % [msaa, mismatches])
		# The shader discards centers outside the same thin strip as the polygon.
		var masked := _shader("interpolateAtCentroid(point)")
		masked.code = masked.code.replace("COLOR = vec4(pos / 32.0, 0.0, 1.0);", "if (abs(pos.y - (8.15 + 0.05 * pos.x)) > 0.2) discard; COLOR = vec4(1.0);")
		var centroid := await _render(masked, msaa, true)
		masked.code = masked.code.replace("interpolateAtCentroid(point)", "point")
		var center := await _render(masked, msaa, true)
		var center_hits := 0
		var centroid_hits := 0
		for y in range(32):
			for x in range(32):
				if center.get_pixel(x,y).a > 0.001: center_hits += 1
				if centroid.get_pixel(x,y).a > 0.001: centroid_hits += 1
		print("COVERAGE msaa=%d center=%d centroid=%d" % [msaa,center_hits,centroid_hits])
		_check(centroid_hits >= center_hits, "Centroid lost covered pixels")
		if msaa != Viewport.MSAA_DISABLED:
			_check(centroid_hits > center_hits, "Centroid did not recover coverage")
	# Exercise scalar/vector overloads, swizzles and an indexed varying.
	for expr in ["vec2(interpolateAtCentroid(scalar))", "interpolateAtCentroid(triple).xy", "interpolateAtCentroid(quadruple).xy", "interpolateAtCentroid(points[1])", "vec2(interpolateAtSample(point.x, 0))", "interpolateAtOffset(point.xy, vec2(0.25, 0.0))"]:
		var image := await _render(_shader(expr), Viewport.MSAA_4X)
		_check(image.get_pixel(16,16).a > 0.99, "Overload failed: " + expr)
	for function in ["interpolateAtSample", "interpolateAtOffset"]:
		var argument := ", 0" if function == "interpolateAtSample" else ", vec2(0.0)"
		for expr in ["vec2(%s(scalar%s))", "%s(triple%s).xy", "%s(quadruple%s).xy", "%s(points[1]%s)"]:
			var expression: String = expr % [function, argument]
			var image := await _render(_shader(expression), Viewport.MSAA_4X)
			var color := image.get_pixel(16,16)
			_check(absf(color.r - 16.5 / 32.0) < 0.025 and color.a > 0.99, "Overload failed: " + expression)
	var left := await _render(_shader("interpolateAtOffset(point, vec2(-0.25, 0.0))"), Viewport.MSAA_4X)
	var right := await _render(_shader("interpolateAtOffset(point, vec2(0.25, 0.0))"), Viewport.MSAA_4X)
	_check(absf(right.get_pixel(16,16).r - left.get_pixel(16,16).r - 0.5 / 32.0) < 0.002, "Offset interpolation did not move half a pixel")
	var first := await _render(_shader("interpolateAtSample(point, 0)"), Viewport.MSAA_4X)
	var second := await _render(_shader("interpolateAtSample(point, 1)"), Viewport.MSAA_4X)
	_check(first.get_pixel(16,16) != second.get_pixel(16,16), "Different MSAA samples returned the same coordinates")
	print("INTERPOLATION_REGRESSION checks=%d failures=%d driver=%s" % [checks,failures,RenderingServer.get_current_rendering_driver_name()])
	quit(1 if failures else 0)
