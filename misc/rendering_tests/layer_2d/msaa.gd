extends SceneTree

var viewport: SubViewport
var content: Node2D
var checks := 0
var failures := 0
var strip_shader: Shader


func _initialize() -> void:
	call_deferred("_run")


func _check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		failures += 1
		push_error(message)


func _reset() -> void:
	if is_instance_valid(content):
		content.free()
	content = Node2D.new()
	viewport.add_child(content)


func _layer(parent: Node) -> Layer2D:
	var layer := Layer2D.new()
	layer.composite_mode = Layer2D.COMPOSITE_MODE_ALWAYS
	parent.add_child(layer)
	return layer


func _rect(parent: Node, bounds: Rect2, color: Color) -> Polygon2D:
	var polygon := Polygon2D.new()
	polygon.polygon = PackedVector2Array([bounds.position, bounds.position+Vector2(bounds.size.x,0), bounds.end, bounds.position+Vector2(0,bounds.size.y)])
	polygon.color = color
	parent.add_child(polygon)
	return polygon


func _strip(parent: Node) -> Polygon2D:
	var polygon := Polygon2D.new()
	polygon.polygon = PackedVector2Array([Vector2(3,20.10),Vector2(59,22.90),Vector2(59,23.30),Vector2(3,20.50)])
	var material := ShaderMaterial.new()
	material.shader = strip_shader
	polygon.material = material
	parent.add_child(polygon)
	return polygon


func _snapshot() -> Image:
	await RenderingServer.frame_post_draw
	await RenderingServer.frame_post_draw
	return viewport.get_texture().get_image()


func _same(actual: Image, expected: Image, message: String, alpha_scale := 1.0) -> void:
	var difference := 0.0
	for y in actual.get_height():
		for x in actual.get_width():
			var a := actual.get_pixel(x,y)
			var b := expected.get_pixel(x,y)*alpha_scale
			difference = maxf(difference,maxf(absf(a.a-b.a),maxf(absf(a.r-b.r),maxf(absf(a.g-b.g),absf(a.b-b.b)))))
	_check(difference < 0.025, message+" max_difference="+str(difference))


func _pixel(image: Image, point: Vector2i, expected: Color, message: String) -> void:
	var actual := image.get_pixelv(point)
	_check(absf(actual.r-expected.r)<0.025 and absf(actual.g-expected.g)<0.025 and absf(actual.b-expected.b)<0.025 and absf(actual.a-expected.a)<0.025,message+" actual="+str(actual))


func _coverage() -> void:
	_reset()
	var line := _strip(content)
	var direct := await _snapshot()
	var hits := 0
	for y in range(18,26):
		for x in range(3,59):
			if direct.get_pixel(x,y).a>0.001: hits += 1
	print("LAYER_MSAA coverage mode=%d hdr=%s size=%s hits=%d" % [viewport.msaa_2d,viewport.use_hdr_2d,viewport.size,hits])
	_check(hits>0,"Reference strip draws")
	var layer := _layer(content)
	line.reparent(layer)
	_same(await _snapshot(),direct,"Layer2D inherits viewport MSAA")
	layer.self_modulate.a = 0.5
	_same(await _snapshot(),direct,"Layer opacity scales resolved color and alpha",0.5)
	layer.self_modulate.a = 1.0
	var group := CanvasGroup.new()
	content.add_child(group)
	line.reparent(group)
	layer.free()
	_same(await _snapshot(),direct,"CanvasGroup inherits viewport MSAA")
	var outer := _layer(content)
	group.reparent(outer)
	_same(await _snapshot(),direct,"Nested CanvasGroup in Layer2D preserves coverage")
	# A clipping Base must preserve coverage while a solid clipped source recolors it.
	_reset()
	var base := _layer(content)
	_strip(base)
	var clipped := _layer(content)
	_rect(clipped,Rect2(0,0,64,64),Color.BLUE)
	clipped.clipping_mask = true
	var clipped_image := await _snapshot()
	var expected := direct.duplicate() as Image
	for y in expected.get_height():
		for x in expected.get_width():
			var alpha := expected.get_pixel(x,y).a
			expected.set_pixel(x,y,Color(0,0,alpha,alpha))
	_same(clipped_image,expected,"Clipping stack preserves MSAA Base alpha")
	clipped.visible = false
	_same(await _snapshot(),direct,"Hidden clipped source returns original Base")


func _resume_and_reuse() -> void:
	_reset()
	var outer := _layer(content)
	_rect(outer,Rect2(2,2,12,12),Color.RED)
	var inner := _layer(outer)
	_rect(inner,Rect2(18,2,12,12),Color.GREEN)
	_rect(outer,Rect2(34,2,12,12),Color.BLUE)
	var image := await _snapshot()
	_pixel(image,Vector2i(7,7),Color.RED,"Parent retains content drawn before nested pass")
	_pixel(image,Vector2i(23,7),Color.GREEN,"Nested pass resolves into parent")
	_pixel(image,Vector2i(39,7),Color.BLUE,"Parent resumes after nested pass")
	# Move the nested group's contents repeatedly while reusing both scratch slots.
	for iteration in range(3):
		inner.position.y = 16 if iteration%2==0 else 0
		image = await _snapshot()
		var old_point := Vector2i(23,7 if iteration%2==0 else 23)
		var new_point := Vector2i(23,23 if iteration%2==0 else 7)
		_pixel(image,old_point,Color(0,0,0,0),"Reused scratch buffer has no stale pixels")
		_pixel(image,new_point,Color.GREEN,"Moved nested content is current")
	inner.get_child(0).free()
	image = await _snapshot()
	_pixel(image,Vector2i(23,7),Color(0,0,0,0),"Empty nested layer clears old pixels")
	_pixel(image,Vector2i(23,23),Color(0,0,0,0),"Empty moved layer clears old pixels")
	_pixel(image,Vector2i(7,7),Color.RED,"Empty child does not clear parent")
	_reset()
	for i in range(3):
		var layer := _layer(content)
		_rect(layer,Rect2(2+i*20,35,10,10),Color.RED if i%2==0 else Color.GREEN)
	image = await _snapshot()
	_pixel(image,Vector2i(7,40),Color.RED,"First sibling survives scratch reuse")
	_pixel(image,Vector2i(27,40),Color.GREEN,"Second sibling survives scratch reuse")
	_pixel(image,Vector2i(47,40),Color.RED,"Third sibling survives scratch reuse")
	_pixel(image,Vector2i(17,40),Color(0,0,0,0),"Scratch reuse leaves gaps transparent")
	# Nonzero/fractional bounds and Viewport clipping exercise regional resolves.
	for offset in [Vector2(27.25,31.75),Vector2(-4.25,19.5),Vector2(57.5,58.25)]:
		_reset()
		var shape := _rect(content,Rect2(offset,Vector2(13.5,11.25)),Color.GREEN)
		var direct := await _snapshot()
		shape.reparent(_layer(content))
		_same(await _snapshot(),direct,"Regional resolve preserves fractional and clipped bounds")


func _screen_reads_and_deferred_clear() -> void:
	_reset()
	var outer := _layer(content)
	var inner := _layer(outer)
	_rect(inner,Rect2(8,8,16,16),Color.RED)
	_pixel(await _snapshot(),Vector2i(12,12),Color.RED,"Parent first draw can be a nested layer")
	inner.get_child(0).free()
	_pixel(await _snapshot(),Vector2i(12,12),Color(0,0,0,0),"Empty nested sources clear before sampling")

	var material := ShaderMaterial.new()
	material.shader = Shader.new()
	material.shader.code = """shader_type canvas_item;
render_mode unshaded, blend_premul_alpha;
uniform sampler2D screen : hint_screen_texture, repeat_disable, filter_nearest;
void fragment() { COLOR = vec4(textureLod(screen, SCREEN_UV, 0.0).bgr, 1.0); }
"""
	_reset()
	var background := _rect(content,Rect2(0,0,64,64),Color.RED)
	outer = _layer(content)
	var reader := _rect(outer,Rect2(8,8,16,16),Color.WHITE)
	reader.material = material
	_pixel(await _snapshot(),Vector2i(12,12),Color.BLUE,"Child screen shader resolves its main target source")
	background.color = Color.BLUE
	_pixel(await _snapshot(),Vector2i(12,12),Color.RED,"Screen source changes are resolved in the same frame")

	_reset()
	outer = _layer(content)
	var first := _rect(outer,Rect2(0,0,32,32),Color.RED)
	inner = _layer(outer)
	reader = _rect(inner,Rect2(8,8,16,16),Color.WHITE)
	reader.material = material
	_pixel(await _snapshot(),Vector2i(12,12),Color.BLUE,"Nested screen shader resolves its suspended parent")
	first.color = Color.GREEN
	_pixel(await _snapshot(),Vector2i(12,12),Color.GREEN,"Suspended parent's resolve is not stale")
	first.free()
	_pixel(await _snapshot(),Vector2i(12,12),Color.BLACK,"Screen shader sees a cleared parent before its first draw")

	# Backbuffer reads after completed groups must resolve the main target too.
	_reset()
	outer = _layer(content)
	_rect(outer,Rect2(0,0,32,32),Color.RED)
	reader = _rect(content,Rect2(8,8,16,16),Color.WHITE)
	reader.material = material
	_pixel(await _snapshot(),Vector2i(12,12),Color.BLUE,"Root screen shader sees preceding layer composition")


func _independent_viewports() -> void:
	_reset()
	var group := CanvasGroup.new()
	content.add_child(group)
	_strip(group)
	viewport.msaa_2d = Viewport.MSAA_DISABLED
	var other := SubViewport.new()
	other.size = viewport.size
	other.world_2d = viewport.world_2d
	other.use_hdr_2d = viewport.use_hdr_2d
	other.transparent_bg = true
	other.msaa_2d = Viewport.MSAA_8X
	other.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(other)
	var single := await _snapshot()
	var multisample := other.get_texture().get_image()
	var single_hits := 0
	var multi_hits := 0
	for y in range(18,26):
		for x in range(3,59):
			if single.get_pixel(x,y).a>0.001: single_hits += 1
			if multisample.get_pixel(x,y).a>0.001: multi_hits += 1
	_check(multi_hits>single_hits,"Shared World2D uses each Viewport's own MSAA")
	other.free()


func _run() -> void:
	if RenderingServer.get_rendering_device()==null:
		push_error("Requires a real GPU RenderingDevice")
		quit(1)
		return
	strip_shader = Shader.new()
	strip_shader.code = """shader_type canvas_item;
render_mode unshaded;
varying vec2 point;
void vertex() { point = VERTEX; }
void fragment() {
    vec2 q = interpolateAtCentroid(point);
    if (abs(q.y - (20.15 + 0.05*q.x)) > 0.2) discard;
    COLOR = vec4(1.0);
}
"""
	viewport = SubViewport.new()
	viewport.size = Vector2i(64,64)
	viewport.world_2d = World2D.new()
	viewport.transparent_bg = true
	viewport.disable_3d = true
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(viewport)
	# Reuse the same viewport/pool through sample-count, format and capacity changes.
	for hdr in [true,false,true]:
		viewport.use_hdr_2d = hdr
		for mode in [Viewport.MSAA_DISABLED,Viewport.MSAA_2X,Viewport.MSAA_4X,Viewport.MSAA_8X,Viewport.MSAA_DISABLED]:
			viewport.msaa_2d = mode
			await _coverage()
			await _resume_and_reuse()
			await _screen_reads_and_deferred_clear()
	viewport.use_hdr_2d = true
	viewport.msaa_2d = Viewport.MSAA_4X
	for size in [Vector2i(97,83),Vector2i(65,65),Vector2i(64,64)]:
		viewport.size = size
		await _coverage()
		await _resume_and_reuse()
		await _screen_reads_and_deferred_clear()
	await _independent_viewports()
	print("LAYER_MSAA_REGRESSION checks=%d failures=%d driver=%s" % [checks,failures,RenderingServer.get_current_rendering_driver_name()])
	quit(1 if failures else 0)
