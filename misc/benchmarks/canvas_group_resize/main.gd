extends Node

const BRANCH_COUNT := 2
const GROUP_DEPTH := 4
const STROKES_PER_BRANCH := 100
const WARMUP_FRAMES := 60
const RESIZE_FRAMES := 240

var paint_viewport: SubViewport
var world_view: CanvasGroup
var benchmark_frame := 0
var previous_tick_usec := 0
var frame_times_usec: Array[int] = []
var hidden_root := false
var group_depth := GROUP_DEPTH
var use_mipmaps := false
var upgrade_mipmaps := false
var canvas_groups: Array[CanvasGroup] = []
var test_screen_texture := false


func _ready() -> void:
	var user_args := OS.get_cmdline_user_args()
	hidden_root = "--hidden-root" in user_args
	use_mipmaps = "--mipmaps" in user_args
	upgrade_mipmaps = "--upgrade-mipmaps" in user_args
	test_screen_texture = "--screen-texture" in user_args
	for argument in user_args:
		if argument.begins_with("--depth="):
			group_depth = argument.trim_prefix("--depth=").to_int()
	paint_viewport = SubViewport.new()
	paint_viewport.name = "PaintViewport"
	paint_viewport.size = Vector2i(1600, 900)
	paint_viewport.use_hdr_2d = true
	paint_viewport.disable_3d = true
	paint_viewport.transparent_bg = true
	paint_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	add_child(paint_viewport)

	world_view = CanvasGroup.new()
	world_view.name = "WorldView"
	world_view.visible = not hidden_root
	world_view.use_mipmaps = use_mipmaps
	canvas_groups.append(world_view)
	paint_viewport.add_child(world_view)

	var mesh := QuadMesh.new()
	mesh.size = Vector2(10.0, 10.0)
	var gradient_material := _create_gradient_material()
	for branch_index in BRANCH_COUNT:
		var parent: Node = world_view
		for depth_index in range(1, group_depth):
			if test_screen_texture:
				_add_gradient(parent, gradient_material)
			var group := CanvasGroup.new()
			group.name = "Layer_%d_%d" % [branch_index, depth_index]
			group.use_mipmaps = use_mipmaps
			canvas_groups.append(group)
			parent.add_child(group)
			parent = group

		for stroke_index in STROKES_PER_BRANCH:
			var multimesh := MultiMesh.new()
			multimesh.transform_format = MultiMesh.TRANSFORM_2D
			multimesh.mesh = mesh
			multimesh.instance_count = 1
			var position := Vector2(
				100.0 + float(stroke_index % 20) * 35.0,
				100.0 + float(stroke_index / 20) * 35.0 + float(branch_index) * 250.0
			)
			multimesh.set_instance_transform_2d(0, Transform2D(0.0, position))
			var stroke := MultiMeshInstance2D.new()
			stroke.name = "Stroke_%d_%d" % [branch_index, stroke_index]
			stroke.multimesh = multimesh
			parent.add_child(stroke)

		if test_screen_texture and branch_index == 0:
			var sample_multimesh := MultiMesh.new()
			sample_multimesh.transform_format = MultiMesh.TRANSFORM_2D
			sample_multimesh.mesh = mesh
			sample_multimesh.instance_count = 1
			sample_multimesh.set_instance_transform_2d(0, Transform2D(0.0, Vector2(1500.0, 880.0)))
			var sample := MultiMeshInstance2D.new()
			sample.multimesh = sample_multimesh
			sample.material = _create_screen_sample_material()
			parent.add_child(sample)

	print("SCRIPT_BENCH start hidden_root=%s mipmaps=%s branches=%d depth=%d strokes=%d" % [
		hidden_root, use_mipmaps, BRANCH_COUNT, group_depth, BRANCH_COUNT * STROKES_PER_BRANCH
	])


func _process(_delta: float) -> void:
	if upgrade_mipmaps and benchmark_frame == WARMUP_FRAMES / 2:
		for group in canvas_groups:
			group.use_mipmaps = true

	var now := Time.get_ticks_usec()
	if benchmark_frame > WARMUP_FRAMES and benchmark_frame <= WARMUP_FRAMES + RESIZE_FRAMES:
		frame_times_usec.append(now - previous_tick_usec)
	previous_tick_usec = now

	if benchmark_frame >= WARMUP_FRAMES and benchmark_frame < WARMUP_FRAMES + RESIZE_FRAMES:
		var resize_index := benchmark_frame - WARMUP_FRAMES
		paint_viewport.size = Vector2i(1600 + resize_index % 64, 900 + resize_index % 16)

	benchmark_frame += 1
	if benchmark_frame <= WARMUP_FRAMES + RESIZE_FRAMES:
		return

	RenderingServer.force_sync()
	var image := paint_viewport.get_texture().get_image()
	print("SCRIPT_BENCH pixels inside_alpha=%.3f outside_alpha=%.3f screen_red=%.3f" % [
		image.get_pixel(100, 100).a,
		image.get_pixel(1500, 800).a,
		image.get_pixel(1500, 880).r
	])
	frame_times_usec.sort()
	var total_usec := 0
	for frame_usec in frame_times_usec:
		total_usec += frame_usec
	var sample_count := frame_times_usec.size()
	print("SCRIPT_BENCH result hidden_root=%s samples=%d mean_us=%d p50_us=%d p95_us=%d max_us=%d" % [
		hidden_root,
		sample_count,
		total_usec / sample_count,
		frame_times_usec[int(sample_count / 2)],
		frame_times_usec[int(sample_count * 0.95)],
		frame_times_usec[sample_count - 1]
	])
	get_tree().quit()


func _create_gradient_material() -> ShaderMaterial:
	var shader := Shader.new()
	shader.code = "shader_type canvas_item; void fragment() { COLOR = vec4(SCREEN_UV.y, 0.0, 0.0, 1.0); }"
	var material := ShaderMaterial.new()
	material.shader = shader
	return material


func _create_screen_sample_material() -> ShaderMaterial:
	var shader := Shader.new()
	shader.code = "shader_type canvas_item; uniform sampler2D screen_texture : hint_screen_texture, repeat_disable, filter_nearest; void fragment() { vec4 sampled = textureLod(screen_texture, SCREEN_UV, 0.0); COLOR = vec4(sampled.a, 0.0, 0.0, 1.0); }"
	var material := ShaderMaterial.new()
	material.shader = shader
	return material


func _add_gradient(parent: Node, material: ShaderMaterial) -> void:
	var polygon := Polygon2D.new()
	polygon.polygon = PackedVector2Array([
		Vector2(0.0, 0.0), Vector2(1700.0, 0.0),
		Vector2(1700.0, 1000.0), Vector2(0.0, 1000.0)
	])
	polygon.material = material
	parent.add_child(polygon)
