extends Node

const FULL := Rect2(0, 0, 48, 48)
const LEFT := Rect2(0, 0, 16, 48)
const RIGHT := Rect2(32, 0, 16, 48)
const TOLERANCE := 0.025
const CLEAR := Color(0, 0, 0, 0)

var viewport: SubViewport
var content: Node2D
var failures := 0
var checks := 0


func _ready() -> void:
	if RenderingServer.get_rendering_device() == null:
		push_error("This regression requires a real RenderingDevice renderer.")
		get_tree().quit(1)
		return
	viewport = SubViewport.new()
	viewport.size = Vector2i(64, 64)
	viewport.world_2d = World2D.new()
	viewport.use_hdr_2d = true
	viewport.transparent_bg = true
	viewport.disable_3d = true
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	add_child(viewport)
	await _test_opacity_and_material()
	await _test_default_base()
	await _test_local_z()
	await _test_conflict()
	await _test_nested_clipping()
	await _test_sprite_base()
	await _test_many_layers()
	print("LAYER_REGRESSION checks=%d failures=%d" % [checks, failures])
	get_tree().quit(1 if failures else 0)


func _reset() -> void:
	if is_instance_valid(content):
		content.free()
	content = Node2D.new()
	viewport.add_child(content)


func _layer(parent: Node) -> Layer2D:
	var layer := Layer2D.new()
	parent.add_child(layer)
	return layer


func _rect(parent: Node, rect: Rect2, color: Color) -> Polygon2D:
	var polygon := Polygon2D.new()
	polygon.polygon = PackedVector2Array([
		rect.position, rect.position + Vector2(rect.size.x, 0),
		rect.end, rect.position + Vector2(0, rect.size.y),
	])
	polygon.color = color
	parent.add_child(polygon)
	return polygon


func _snapshot() -> Image:
	# Allow scene changes and canvas drawing to reach the rendering thread.
	await RenderingServer.frame_post_draw
	await RenderingServer.frame_post_draw
	return viewport.get_texture().get_image()


func _check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		failures += 1
		push_error(message)


func _pixel(image: Image, point: Vector2i, expected: Color, message: String) -> void:
	var actual := image.get_pixelv(point)
	_check(absf(actual.r - expected.r) < TOLERANCE and
			absf(actual.g - expected.g) < TOLERANCE and
			absf(actual.b - expected.b) < TOLERANCE and
			absf(actual.a - expected.a) < TOLERANCE,
			"%s: expected %s, got %s" % [message, expected, actual])


func _same_image(actual: Image, expected: Image, message: String) -> void:
	var max_difference := 0.0
	for y in actual.get_height():
		for x in actual.get_width():
			var a := actual.get_pixel(x, y)
			var b := expected.get_pixel(x, y)
			max_difference = maxf(max_difference, maxf(absf(a.r - b.r),
					maxf(absf(a.g - b.g), maxf(absf(a.b - b.b), absf(a.a - b.a)))))
	_check(max_difference < TOLERANCE, "%s: max difference %s" % [message, max_difference])


func _test_opacity_and_material() -> void:
	_reset()
	var layer := _layer(content)
	_rect(layer, FULL, Color.RED)
	_rect(layer, LEFT, Color.RED)
	var direct := await _snapshot()
	_check(not layer.is_composite_active(), "Default layer must draw directly")
	layer.self_modulate.a = 0.5
	var faded := await _snapshot()
	_check(layer.is_composite_active(), "Opacity must composite the complete layer")
	_pixel(faded, Vector2i(8, 8), Color(0.5, 0, 0, 0.5), "Overlap receives opacity once")
	layer.self_modulate = Color.WHITE
	_same_image(await _snapshot(), direct, "Returning to direct rendering clears generated geometry")
	_check(not layer.is_composite_active(), "Default opacity releases group state")
	_rect(content, FULL, Color(0.2, 0.4, 0.6, 1))
	content.move_child(layer, -1)
	for mode in [Layer2D.LAYER_BLEND_MODE_ADD, Layer2D.LAYER_BLEND_MODE_MULTIPLY]:
		layer.layer_blend_mode = mode
		var automatic := await _snapshot()
		layer.composite_mode = Layer2D.COMPOSITE_MODE_ALWAYS
		_same_image(await _snapshot(), automatic, "Automatic blend equals explicit composition")
		layer.composite_mode = Layer2D.COMPOSITE_MODE_AUTO
	layer.layer_blend_mode = Layer2D.LAYER_BLEND_MODE_DEFAULT
	var shader := Shader.new()
	shader.code = "shader_type canvas_item; render_mode unshaded; void fragment() { COLOR = vec4(0.0, COLOR.a, 0.0, COLOR.a); }"
	var material := ShaderMaterial.new()
	material.shader = shader
	content.material = material
	layer.use_parent_material = true
	var inherited := await _snapshot()
	_check(layer.is_composite_active(), "Effective parent material requires composition")
	_pixel(inherited, Vector2i(8, 8), Color.GREEN, "Inherited layer material is applied")
	content.material = null
	await _snapshot()
	_check(not layer.is_composite_active(), "Removing parent material restores direct rendering")
	layer.material = material
	await _snapshot()
	_check(not layer.is_composite_active(), "Unused own material does not force composition")
	layer.use_parent_material = false
	await _snapshot()
	_check(layer.is_composite_active(), "Switching material owner updates composition")


func _test_default_base() -> void:
	_reset()
	var base := _layer(content)
	_rect(base, LEFT, Color(1, 0, 0, 0.5))
	_rect(base, RIGHT, Color.RED)
	var clipped := _layer(content)
	_rect(clipped, FULL, Color.BLUE)
	clipped.clipping_mask = true
	var image := await _snapshot()
	_check(base.is_composite_active(), "Default Base must provide a combined source")
	_pixel(image, Vector2i(8, 8), Color(0, 0, 0.5, 0.5), "Clipping preserves partial Base alpha")
	_pixel(image, Vector2i(24, 8), CLEAR, "Clipping preserves holes in combined Base")
	_pixel(image, Vector2i(40, 8), Color.BLUE, "Clipping uses all Base children")
	clipped.clipping_mask = false
	_pixel(await _snapshot(), Vector2i(24, 8), Color.BLUE, "Disabling clipping renders the full source")
	_check(not base.is_composite_active() and not clipped.is_composite_active(), "Both layers return to direct rendering")
	clipped.clipping_mask = true
	content.move_child(clipped, 0)
	print("LAYER_REGRESSION expect orphan warning")
	_pixel(await _snapshot(), Vector2i(24, 8), CLEAR, "Clipping cannot choose a later sibling as Base")
	content.move_child(base, 0)
	_pixel(await _snapshot(), Vector2i(8, 8), Color(0, 0, 0.5, 0.5), "Reordering restores the Base")
	var older_base := _layer(content)
	_rect(older_base, FULL, Color.GREEN)
	content.move_child(older_base, 0)
	base.visible = false
	_pixel(await _snapshot(), Vector2i(8, 8), Color.GREEN, "Hidden Base blocks clipping instead of choosing an older Base")
	base.visible = true
	var another_parent := _layer(content)
	clipped.reparent(another_parent)
	print("LAYER_REGRESSION expect orphan warning after reparent")
	await _snapshot()
	clipped.reparent(content)
	content.move_child(clipped, base.get_index() + 1)
	_pixel(await _snapshot(), Vector2i(40, 8), Color.BLUE, "Reparenting updates Base relationships")
	base.free()
	_pixel(await _snapshot(), Vector2i(24, 8), Color.BLUE, "Deleting Base selects the nearest remaining lower Layer")


func _test_local_z() -> void:
	_reset()
	var external := _rect(content, FULL, Color.GREEN)
	external.z_index = -1
	var parent := _layer(content)
	var onion := _layer(parent)
	onion.z_index = -2
	onion.material = CanvasItemMaterial.new()
	_rect(onion, FULL, Color.BLUE)
	_pixel(await _snapshot(), Vector2i(8, 8), Color.BLUE, "Negative child Z stays above content outside its parent Layer")
	_check(parent.is_composite_active(), "Parent supplies the local Z boundary")
	var current := _layer(parent)
	_rect(current, LEFT, Color.RED)
	parent.move_child(current, 0)
	var image := await _snapshot()
	_pixel(image, Vector2i(8, 8), Color.RED, "Current cel remains above onion skin despite sibling indices")
	_pixel(image, Vector2i(40, 8), Color.BLUE, "Onion skin remains visible outside the current cel")
	onion.z_index = 0
	onion.material = null
	_pixel(await _snapshot(), Vector2i(8, 8), Color.BLUE, "Equal Z restores sibling order")
	_check(not parent.is_composite_active(), "Removing special Z releases parent composition")
	# Ordinary content under helper Node2Ds must also stay inside its Layer.
	var helper := Node2D.new()
	parent.add_child(helper)
	var stroke := _rect(helper, RIGHT, Color.RED)
	stroke.z_index = 4
	external.z_index = 1
	_pixel(await _snapshot(), Vector2i(40, 8), Color.GREEN, "Descendant Z cannot escape through an ordinary Node2D helper")


func _test_conflict() -> void:
	_reset()
	var parent := _layer(content)
	var base := _layer(parent)
	_rect(base, LEFT, Color.RED)
	var clipped := _layer(parent)
	_rect(clipped, FULL, Color.BLUE)
	clipped.clipping_mask = true
	var other := _layer(parent)
	var valid := await _snapshot()
	_pixel(valid, Vector2i(24, 8), CLEAR, "Initial clipping is active")
	other.z_index = -2
	print("LAYER_REGRESSION expect ordering warning")
	_pixel(await _snapshot(), Vector2i(24, 8), Color.BLUE, "Special Z on another sibling disables this group's clipping")
	_check(clipped.clipping_mask, "Conflict preserves the stored clipping property")
	await _snapshot()
	await _snapshot()
	other.z_index = 0
	_same_image(await _snapshot(), valid, "Removing the conflict restores clipping")
	clipped.z_index = -1
	print("LAYER_REGRESSION expect ordering warning after recovery")
	var image := await _snapshot()
	_pixel(image, Vector2i(8, 8), Color.RED, "Conflict gives Z priority over clipping")
	_pixel(image, Vector2i(24, 8), Color.BLUE, "Conflicting clipped source draws normally")


func _test_nested_clipping() -> void:
	_reset()
	var parent := _layer(content)
	var group := _layer(parent)
	group.z_index = -2
	var base := _layer(group)
	_rect(base, LEFT, Color.RED)
	var clipped := _layer(group)
	_rect(clipped, FULL, Color.BLUE)
	clipped.clipping_mask = true
	var image := await _snapshot()
	_pixel(image, Vector2i(8, 8), Color.BLUE, "A group's own Z does not disable internal clipping")
	_pixel(image, Vector2i(24, 8), CLEAR, "Nested clipping remains bounded")
	var inner_image := image
	group.z_index = 0
	var outer_clip := _layer(parent)
	_rect(outer_clip, FULL, Color.GREEN)
	outer_clip.clipping_mask = true
	image = await _snapshot()
	_pixel(image, Vector2i(8, 8), Color.GREEN, "Nested group can become an outer Base")
	_pixel(image, Vector2i(24, 8), CLEAR, "Outer clipping uses the resolved inner alpha")
	outer_clip.clipping_mask = false
	outer_clip.visible = false
	_same_image(await _snapshot(), inner_image, "Nested sources remain stable after changing outer clipping")
	_check(not group.is_composite_active(), "Internal clipping alone does not force an extra enclosing buffer")


func _test_sprite_base() -> void:
	_reset()
	var source := Image.create(16, 48, false, Image.FORMAT_RGBA8)
	source.fill(Color.RED)
	var sprite := Sprite2D.new()
	sprite.texture = ImageTexture.create_from_image(source)
	sprite.centered = false
	content.add_child(sprite)
	var clipped := _layer(content)
	_rect(clipped, FULL, Color.BLUE)
	clipped.clipping_mask = true
	var image := await _snapshot()
	_pixel(image, Vector2i(8, 8), Color.BLUE, "Existing Sprite2D Base API remains supported")
	_pixel(image, Vector2i(24, 8), CLEAR, "Sprite2D coverage clips a Layer2D source")


func _test_many_layers() -> void:
	_reset()
	var layers: Array[Layer2D] = []
	for i in 10000:
		var layer := _layer(content)
		_rect(layer, Rect2(i % 64, (i / 64) % 64, 1, 1), Color.RED)
		layers.append(layer)
	_pixel(await _snapshot(), Vector2i(8, 8), Color.RED, "Dense content renders")
	var composites := 0
	for layer in layers:
		composites += int(layer.is_composite_active())
	_check(composites == 0, "10000 default layers must all use direct rendering")
