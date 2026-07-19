extends SceneTree

const DEFAULT_IMAGE_PATH := "C:/Users/Ciao/Documents/Cat.png"
const MAX_1080P_TIME_MS := 1000.0
const JUNCTION_IMAGE_SIZE := Vector2i(128, 128)
const JUNCTION_CENTER := Vector2(64.0, 64.0)
const JUNCTION_LINE_RADIUS := 2.5
const JUNCTION_TIP_TOLERANCE := 16.0
const JUNCTION_CENTER_TOLERANCE := 18.0
const RADIUS_GUIDE_TOLERANCE := 0.5
const SOURCE_PROBE_SPACING := 4.0
const SOURCE_OWNER_DISTANCE := JUNCTION_LINE_RADIUS + 1.0
const SOURCE_DIRECTION_DOT := 0.8
const SOURCE_AMBIGUITY_DISTANCE := SOURCE_OWNER_DISTANCE * 2.0
const X_CORRIDOR_TOLERANCE := JUNCTION_LINE_RADIUS + 0.75
const X_MAX_PROJECTION_BACKTRACK := 1.25
const DIAGONAL_MAX_SAMPLES := 24
const SHARED_VERTEX_EPSILON := 1.0e-4
const JUNCTION_PLACEMENT_TOLERANCE := 1.25


func _init() -> void:
	var args := OS.get_cmdline_user_args()
	var junction_only := args.has("--junction-only")
	var image_path := DEFAULT_IMAGE_PATH
	for argument in args:
		if !argument.begins_with("--"):
			image_path = argument
			break
	if junction_only:
		var junctions_ok := _test_junctions()
		if junctions_ok:
			print("TOPOLOGY_CENTERLINE_JUNCTION_SMOKE_OK")
		quit(0 if junctions_ok else 1)
		return
	var source := Image.new()
	var error := source.load(image_path)
	if error != OK:
		push_error("Failed to load smoke-test image '%s': %s" % [image_path, error_string(error)])
		quit(1)
		return

	var ok := _run_case(source, Vector2i(256, 256), false)
	ok = _test_junctions() and ok
	ok = _run_case(source, Vector2i(1920, 1080), true) and ok
	ok = _test_api_overloads(source) and ok
	if ok:
		print("TOPOLOGY_CENTERLINE_SMOKE_OK")
	quit(0 if ok else 1)


func _run_case(source: Image, size: Vector2i, enforce_time_limit: bool) -> bool:
	var image := source.duplicate() as Image
	image.resize(size.x, size.y, Image.INTERPOLATE_LANCZOS)
	var started := Time.get_ticks_usec()
	var strokes: Array[Dictionary] = TopologyCenterlineVectorizer.vectorize_image(image)
	var elapsed_ms := float(Time.get_ticks_usec() - started) / 1000.0
	print("TopologyCenterlineVectorizer %dx%d: %.3f ms, %d strokes" % [size.x, size.y, elapsed_ms, strokes.size()])

	var ok := _assert(strokes.size() > 0, "%dx%d produced no strokes." % [size.x, size.y])
	var total_samples := 0
	for stroke_index in strokes.size():
		var stroke := strokes[stroke_index]
		ok = _assert(stroke.has("positions"), "Stroke %d has no positions." % stroke_index) and ok
		ok = _assert(stroke.has("radii"), "Stroke %d has no radii." % stroke_index) and ok
		if !stroke.has("positions") || !stroke.has("radii"):
			continue
		var positions: PackedVector2Array = stroke["positions"]
		var radii: PackedFloat32Array = stroke["radii"]
		total_samples += positions.size()
		ok = _assert(positions.size() >= 2, "Stroke %d has fewer than two positions." % stroke_index) and ok
		ok = _assert(positions.size() == radii.size(), "Stroke %d positions/radii lengths differ." % stroke_index) and ok
		for position in positions:
			ok = _assert(_is_finite(position.x) && _is_finite(position.y), "Stroke %d contains a non-finite position." % stroke_index) and ok
			ok = _assert(position.x >= -1.0 && position.x <= size.x + 1.0 && position.y >= -1.0 && position.y <= size.y + 1.0,
					"Stroke %d contains a position outside the image bounds." % stroke_index) and ok
		for radius in radii:
			ok = _assert(_is_finite(radius) && radius >= 0.0, "Stroke %d contains an invalid radius." % stroke_index) and ok
	print("TopologyCenterlineVectorizer %dx%d: %d polyline samples" % [size.x, size.y, total_samples])

	if enforce_time_limit && elapsed_ms > MAX_1080P_TIME_MS:
		print("TOPOLOGY_CENTERLINE_PERF_TARGET_MISSED: 1920x1080 took %.3f ms (target %.3f ms)." % [elapsed_ms, MAX_1080P_TIME_MS])
	return ok


func _test_api_overloads(source: Image) -> bool:
	var image := source.duplicate() as Image
	image.resize(256, 256, Image.INTERPOLATE_LANCZOS)
	var generic: Array[Dictionary] = TopologyCenterlineVectorizer.vectorize(image)
	var texture := ImageTexture.create_from_image(image)
	var textured: Array[Dictionary] = TopologyCenterlineVectorizer.vectorize_texture(texture)
	return _assert(!generic.is_empty(), "vectorize(Image) produced no strokes.") \
			and _assert(!textured.is_empty(), "vectorize_texture(ImageTexture) produced no strokes.") \
			and _assert(generic.size() == textured.size(), "Image and texture entry points produced different stroke counts.")


func _test_junctions() -> bool:
	var ok := true
	ok = _test_junction_case("T", [
		[Vector2(16.0, 64.0), Vector2(112.0, 64.0)],
		[JUNCTION_CENTER, Vector2(64.0, 112.0)],
	], PackedVector2Array([
		Vector2(16.0, 64.0),
		Vector2(112.0, 64.0),
		Vector2(64.0, 112.0),
	]), 2, [
		[Vector2(16.0, 64.0), Vector2(112.0, 64.0)],
		[JUNCTION_CENTER, Vector2(64.0, 112.0)],
	], [
		[Vector2(16.0, 64.0), Vector2(64.0, 112.0)],
		[Vector2(112.0, 64.0), Vector2(64.0, 112.0)],
	]) and ok
	ok = _test_junction_case("Y", [
		[JUNCTION_CENTER, Vector2(64.0, 16.0)],
		[JUNCTION_CENTER, Vector2(40.0, 112.0)],
		[JUNCTION_CENTER, Vector2(88.0, 112.0)],
	], PackedVector2Array([
		Vector2(64.0, 16.0),
		Vector2(40.0, 112.0),
		Vector2(88.0, 112.0),
	]), 2, [
	], [
		[Vector2(40.0, 112.0), Vector2(88.0, 112.0)],
	], [
	]) and ok
	ok = _test_junction_case("X", [
		[Vector2(20.0, 20.0), Vector2(108.0, 108.0)],
		[Vector2(20.0, 108.0), Vector2(108.0, 20.0)],
	], PackedVector2Array([
		Vector2(20.0, 20.0),
		Vector2(108.0, 108.0),
		Vector2(20.0, 108.0),
		Vector2(108.0, 20.0),
	]), 2, [
		[Vector2(20.0, 20.0), Vector2(108.0, 108.0)],
		[Vector2(20.0, 108.0), Vector2(108.0, 20.0)],
	], [
		[Vector2(20.0, 20.0), Vector2(20.0, 108.0)],
		[Vector2(20.0, 20.0), Vector2(108.0, 20.0)],
		[Vector2(108.0, 108.0), Vector2(20.0, 108.0)],
		[Vector2(108.0, 108.0), Vector2(108.0, 20.0)],
	], [
		[Vector2(20.0, 20.0), Vector2(108.0, 108.0)],
		[Vector2(20.0, 108.0), Vector2(108.0, 20.0)],
	]) and ok
	ok = _test_junction_case("asymmetric X", [
		[Vector2(12.0, 25.0), Vector2(116.0, 103.0)],
		[Vector2(40.0, 112.0), Vector2(88.0, 16.0)],
	], PackedVector2Array([
		Vector2(12.0, 25.0),
		Vector2(116.0, 103.0),
		Vector2(40.0, 112.0),
		Vector2(88.0, 16.0),
	]), 2, [
		[Vector2(12.0, 25.0), Vector2(116.0, 103.0)],
		[Vector2(40.0, 112.0), Vector2(88.0, 16.0)],
	], [], [
		[Vector2(12.0, 25.0), Vector2(116.0, 103.0)],
		[Vector2(40.0, 112.0), Vector2(88.0, 16.0)],
	]) and ok
	ok = _test_junction_case("bent T", [
		[Vector2(16.0, 72.0), JUNCTION_CENTER],
		[JUNCTION_CENTER, Vector2(112.0, 60.0)],
		[JUNCTION_CENTER, Vector2(64.0, 112.0)],
	], PackedVector2Array([
		Vector2(16.0, 72.0),
		Vector2(112.0, 60.0),
		Vector2(64.0, 112.0),
	]), 2, [
		[Vector2(16.0, 72.0), Vector2(112.0, 60.0)],
		[JUNCTION_CENTER, Vector2(64.0, 112.0)],
	], [
		[Vector2(16.0, 72.0), Vector2(64.0, 112.0)],
		[Vector2(112.0, 60.0), Vector2(64.0, 112.0)],
	]) and ok
	ok = _test_curved_oblique_t("curved oblique T", 4.0) and ok
	ok = _test_curved_oblique_t("thick curved oblique T", 10.0) and ok
	ok = _test_junction_case("thick X", [
		[Vector2(20.0, 20.0), Vector2(108.0, 108.0)],
		[Vector2(20.0, 108.0), Vector2(108.0, 20.0)],
	], PackedVector2Array([
		Vector2(20.0, 20.0),
		Vector2(108.0, 108.0),
		Vector2(20.0, 108.0),
		Vector2(108.0, 20.0),
	]), 2, [
		[Vector2(20.0, 20.0), Vector2(108.0, 108.0)],
		[Vector2(20.0, 108.0), Vector2(108.0, 20.0)],
	], [], [
		[Vector2(20.0, 20.0), Vector2(108.0, 108.0)],
		[Vector2(20.0, 108.0), Vector2(108.0, 20.0)],
	], 6.0) and ok
	ok = _test_junction_case("thick T", [
		[Vector2(16.0, 64.0), Vector2(112.0, 64.0)],
		[JUNCTION_CENTER, Vector2(64.0, 112.0)],
	], PackedVector2Array([
		Vector2(16.0, 64.0),
		Vector2(112.0, 64.0),
		Vector2(64.0, 112.0),
	]), 2, [
		[Vector2(16.0, 64.0), Vector2(112.0, 64.0)],
		[JUNCTION_CENTER, Vector2(64.0, 112.0)],
	], [
		[Vector2(16.0, 64.0), Vector2(64.0, 112.0)],
		[Vector2(112.0, 64.0), Vector2(64.0, 112.0)],
	], [
		[Vector2(16.0, 64.0), Vector2(112.0, 64.0), 24.0],
		[JUNCTION_CENTER, Vector2(64.0, 112.0), 24.0],
	], 12.0) and ok
	ok = _test_diagonal_simplification() and ok
	ok = _test_near_touching_strokes() and ok
	ok = _test_thin_neck_continuity() and ok
	ok = _test_diagonal_thin_neck_continuity() and ok
	ok = _test_one_pixel_diagonal_continuity() and ok
	ok = _test_same_component_white_slot_not_bridged() and ok
	ok = _test_one_pixel_gap_not_bridged() and ok
	ok = _test_thin_neck_loop() and ok
	ok = _test_self_touching_loop() and ok
	ok = _test_radius_guide() and ok
	return ok


func _test_junction_case(name: String, segments: Array, tips: PackedVector2Array, expected_strokes: int = -1,
		required_connections: Array = [], forbidden_connections: Array = [], straight_connections: Array = [],
		line_radius: float = JUNCTION_LINE_RADIUS) -> bool:
	var image := _make_junction_image(segments, line_radius)
	var params := {"threshold": 0.0, "despeckling": 0.0}
	var strokes: Array[Dictionary] = TopologyCenterlineVectorizer.vectorize_image(image, params)
	print("TopologyCenterline junction %s: %d strokes" % [name, strokes.size()])
	var ok := true
	if expected_strokes >= 0:
		ok = _assert(strokes.size() == expected_strokes,
				"%s junction produced %d strokes; expected %d." % [name, strokes.size(), expected_strokes]) and ok
	else:
		ok = _assert(strokes.size() >= 2, "%s junction produced fewer than two strokes." % name) and ok
	var endpoints := PackedVector2Array()
	for stroke_index in strokes.size():
		var stroke := strokes[stroke_index]
		ok = _assert(stroke.has("positions") && stroke.has("radii"), "%s stroke %d is missing output arrays." % [name, stroke_index]) and ok
		if !stroke.has("positions") || !stroke.has("radii"):
			continue
		var positions: PackedVector2Array = stroke["positions"]
		var radii: PackedFloat32Array = stroke["radii"]
		ok = _assert(positions.size() >= 2, "%s stroke %d has fewer than two positions." % [name, stroke_index]) and ok
		ok = _assert(positions.size() == radii.size(), "%s stroke %d positions/radii differ." % [name, stroke_index]) and ok
		if positions.size() < 2 || positions.size() != radii.size():
			continue
		endpoints.append(positions[0])
		endpoints.append(positions[positions.size() - 1])
		for sample_index in positions.size():
			var position: Vector2 = positions[sample_index]
			var radius: float = radii[sample_index]
			ok = _assert(_is_finite(position.x) && _is_finite(position.y), "%s contains a non-finite position." % name) and ok
			ok = _assert(_is_finite(radius) && radius >= 0.0, "%s contains an invalid radius." % name) and ok

	for tip in tips:
		var endpoint_owners := _nearby_point_count(endpoints, tip, JUNCTION_TIP_TOLERANCE)
		ok = _assert(endpoint_owners >= 1,
				"%s lost the branch endpoint near %s." % [name, tip]) and ok
		ok = _assert(endpoint_owners == 1,
				"%s assigned the branch endpoint near %s to %d strokes." % [name, tip, endpoint_owners]) and ok
		var direction := (tip - JUNCTION_CENTER).normalized()
		var branch_probe := JUNCTION_CENTER + direction * minf(24.0, JUNCTION_CENTER.distance_to(tip) * 0.5)
		var branch_covered := false
		for stroke in strokes:
			if _stroke_covers_source_probe(stroke, branch_probe, direction):
				branch_covered = true
				break
		ok = _assert(branch_covered, "%s lost the branch direction toward %s." % [name, tip]) and ok
	ok = _assert(_nearest_stroke_distance(strokes, JUNCTION_CENTER) <= JUNCTION_CENTER_TOLERANCE,
				"%s has no centerline geometry near the junction." % name) and ok
	for raw_connection in required_connections:
		var connection: Array = raw_connection
		var first: Vector2 = connection[0]
		var second: Vector2 = connection[1]
		ok = _assert(_stroke_connects(strokes, first, second),
				"%s split the stroke from %s to %s." % [name, first, second]) and ok
	for raw_connection in forbidden_connections:
		var connection: Array = raw_connection
		var first: Vector2 = connection[0]
		var second: Vector2 = connection[1]
		ok = _assert(!_stroke_connects(strokes, first, second),
				"%s introduced an unintended stroke from %s to %s." % [name, first, second]) and ok
	ok = _assert_source_segments_owned_once(name, strokes, segments) and ok
	ok = _assert_source_intersections_are_vertices(name, strokes, segments) and ok
	for raw_connection in straight_connections:
		var connection: Array = raw_connection
		var first: Vector2 = connection[0]
		var second: Vector2 = connection[1]
		var follows_corridor := _stroke_follows_straight_corridor(strokes, first, second)
		if connection.size() >= 3:
			follows_corridor = _stroke_follows_straight_corridor_window(
					strokes, first, second, JUNCTION_CENTER, float(connection[2]))
		ok = _assert(follows_corridor,
				"%s bent or doubled back outside the source corridor from %s to %s." % [name, first, second]) and ok
	return ok


func _test_curved_oblique_t(name: String, line_radius: float) -> bool:
	var left := Vector2(16.0, 64.0)
	var right := Vector2(112.0, 64.0)
	var branch_tip := Vector2(52.0, 112.0)
	var branch_control := Vector2(52.0, 88.0)
	var branch_end := Vector2(72.0, 68.0)
	var expected_junction := Vector2(76.0, 64.0)
	var branch := _sample_quadratic_path(branch_tip, branch_control, branch_end, 32)
	var reference := branch.duplicate()
	reference.append(expected_junction)
	var image := _make_path_image([
		PackedVector2Array([left, right]),
		branch,
	], line_radius)
	var strokes: Array[Dictionary] = TopologyCenterlineVectorizer.vectorize_image(
			image, {"threshold": 0.0, "despeckling": 0.0})
	print("TopologyCenterline junction %s: %d strokes" % [name, strokes.size()])
	var ok := _assert(strokes.size() == 2,
			"%s produced %d strokes; expected 2." % [name, strokes.size()])
	var placement_tolerance := maxf(1.5, line_radius * 0.35)
	var trend_corridor_tolerance := maxf(2.0, line_radius * 0.35)
	var shared := _find_shared_vertex_near(strokes, expected_junction, 8.0)
	ok = _assert(shared["found"], "%s has no explicit shared vertex near the expected junction." % name) and ok
	if !shared["found"]:
		return false
	var shared_position: Vector2 = shared["position"]
	ok = _assert(shared_position.distance_to(expected_junction) <= placement_tolerance,
			"%s shared vertex is %.3fpx from the trend intersection." %
			[name, shared_position.distance_to(expected_junction)]) and ok
	var horizontal_stroke := _find_stroke_with_endpoints(strokes, left, right, JUNCTION_TIP_TOLERANCE)
	var branch_stroke := _find_stroke_with_endpoints(strokes, branch_tip, shared_position, JUNCTION_TIP_TOLERANCE,
			SHARED_VERTEX_EPSILON * 2.0)
	ok = _assert(horizontal_stroke >= 0, "%s split the horizontal continuation." % name) and ok
	ok = _assert(branch_stroke >= 0 && branch_stroke != horizontal_stroke,
			"%s did not preserve the terminating branch." % name) and ok
	if horizontal_stroke < 0 || branch_stroke < 0:
		return false
	ok = _assert(_stroke_follows_straight_corridor_window(
			strokes, left, right, expected_junction, 24.0),
			"%s pulled the horizontal continuation out of its corridor." % name) and ok
	var positions: PackedVector2Array = strokes[branch_stroke]["positions"]
	var outward := _orient_polyline_from_vertex(positions, shared_position)
	var expected_tangent := (branch_end - branch_control).normalized()
	ok = _assert_terminal_trend(outward, reference, expected_tangent, trend_corridor_tolerance) and ok
	return ok


func _test_diagonal_simplification() -> bool:
	var start := Vector2(12.0, 21.0)
	var finish := Vector2(116.0, 101.0)
	var segments := [[start, finish]]
	var image := _make_junction_image(segments)
	var strokes: Array[Dictionary] = TopologyCenterlineVectorizer.vectorize_image(image, {"threshold": 0.0, "despeckling": 0.0})
	var ok := _assert(strokes.size() == 1,
			"Diagonal simplification produced %d strokes; expected 1." % strokes.size())
	ok = _assert_source_segments_owned_once("diagonal simplification", strokes, segments) and ok
	ok = _assert(_stroke_follows_straight_corridor(strokes, start, finish),
			"Diagonal simplification left the source corridor or doubled back.") and ok
	var sample_count := 0
	if strokes.size() == 1 && strokes[0].has("positions"):
		var positions: PackedVector2Array = strokes[0]["positions"]
		sample_count = positions.size()
	var dense_strokes: Array[Dictionary] = TopologyCenterlineVectorizer.vectorize_image(image, {
		"threshold": 0.0,
		"despeckling": 0.0,
		"polyline_max_error": 0.1,
		"polyline_max_segment_length": 4.0,
	})
	var sparse_strokes: Array[Dictionary] = TopologyCenterlineVectorizer.vectorize_image(image, {
		"threshold": 0.0,
		"despeckling": 0.0,
		"polyline_max_error": 1.0,
		"polyline_max_segment_length": 0.0,
	})
	var dense_sample_count := _connecting_stroke_sample_count(dense_strokes, start, finish)
	var sparse_sample_count := _connecting_stroke_sample_count(sparse_strokes, start, finish)
	print("TopologyCenterline diagonal simplification: sparse=%d default=%d dense=%d samples" %
			[sparse_sample_count, sample_count, dense_sample_count])
	ok = _assert(sample_count >= 2 && sample_count <= DIAGONAL_MAX_SAMPLES,
			"Diagonal simplification kept %d samples; expected 2..%d." % [sample_count, DIAGONAL_MAX_SAMPLES]) and ok
	ok = _assert(sparse_sample_count < sample_count && sample_count < dense_sample_count,
			"Polyline density parameters did not order sample counts: sparse=%d default=%d dense=%d." %
			[sparse_sample_count, sample_count, dense_sample_count]) and ok
	return ok


func _test_near_touching_strokes() -> bool:
	var left_start := Vector2(16.0, 48.0)
	var left_end := Vector2(60.0, 48.0)
	var right_start := Vector2(68.0, 48.0)
	var right_end := Vector2(112.0, 48.0)
	var image := _make_junction_image([
		[left_start, left_end],
		[right_start, right_end],
	])
	var strokes: Array[Dictionary] = TopologyCenterlineVectorizer.vectorize_image(image, {"threshold": 0.0, "despeckling": 0.0})
	print("TopologyCenterline near-touching strokes: %d strokes" % strokes.size())
	var ok := _assert(strokes.size() == 2,
			"Near-touching disconnected lines produced %d strokes; expected 2." % strokes.size())
	ok = _assert(_stroke_connects(strokes, left_start, left_end), "The left disconnected stroke was split or lost.") and ok
	ok = _assert(_stroke_connects(strokes, right_start, right_end), "The right disconnected stroke was split or lost.") and ok
	ok = _assert(!_stroke_connects(strokes, left_start, right_end), "The white gap between disconnected strokes was bridged.") and ok
	return ok


func _test_thin_neck_continuity() -> bool:
	var start := Vector2(12.0, 64.5)
	var finish := Vector2(116.0, 64.5)
	var image := _make_horizontal_thin_neck_image()
	var strokes: Array[Dictionary] = TopologyCenterlineVectorizer.vectorize_image(
			image, {"threshold": 0.0, "despeckling": 0.0})
	print("TopologyCenterline thin neck: %d strokes" % strokes.size())
	return _assert(_foreground_component_count_8(image) == 1,
			"The horizontal thin-neck fixture is not one foreground component.") \
			and _assert(strokes.size() == 1,
			"A visually connected stroke produced %d strokes at its one-pixel neck; expected 1." % strokes.size()) \
			and _assert(_stroke_connects(strokes, start, finish),
					"A visually connected stroke was split at its one-pixel neck.")


func _test_diagonal_thin_neck_continuity() -> bool:
	var start := Vector2(16.0, 112.0)
	var neck_start := Vector2(48.0, 80.0)
	var neck_end := Vector2(80.0, 48.0)
	var finish := Vector2(112.0, 16.0)
	var image := Image.create(JUNCTION_IMAGE_SIZE.x, JUNCTION_IMAGE_SIZE.y, false, Image.FORMAT_RGBA8)
	image.fill(Color.WHITE)
	_draw_segment(image, start, neck_start, 2.5)
	_draw_segment(image, neck_start, neck_end, 0.55)
	_draw_segment(image, neck_end, finish, 2.5)
	var strokes: Array[Dictionary] = TopologyCenterlineVectorizer.vectorize_image(
			image, {"threshold": 0.0, "despeckling": 0.0})
	print("TopologyCenterline diagonal thin neck: %d strokes" % strokes.size())
	return _assert(_foreground_component_count_8(image) == 1,
			"The diagonal thin-neck fixture is not one foreground component.") \
			and _assert(strokes.size() == 1,
			"A diagonal one-pixel neck produced %d strokes; expected 1." % strokes.size()) \
			and _assert(_stroke_connects(strokes, start, finish),
					"A diagonal stroke was split at its one-pixel neck.") \
			and _assert(_stroke_follows_straight_corridor(strokes, start, finish),
					"The restored diagonal thin neck left its source corridor.")


func _test_one_pixel_diagonal_continuity() -> bool:
	var image := Image.create(JUNCTION_IMAGE_SIZE.x, JUNCTION_IMAGE_SIZE.y, false, Image.FORMAT_RGBA8)
	image.fill(Color.WHITE)
	for coordinate in range(24, 105):
		image.set_pixel(coordinate, coordinate, Color.BLACK)
	var start := Vector2(24.5, 24.5)
	var finish := Vector2(104.5, 104.5)
	var strokes: Array[Dictionary] = TopologyCenterlineVectorizer.vectorize_image(
			image, {"threshold": 0.0, "despeckling": 0.0})
	print("TopologyCenterline one-pixel diagonal: %d strokes" % strokes.size())
	return _assert(_foreground_component_count_8(image) == 1,
			"The one-pixel diagonal fixture is not one 8-connected foreground component.") \
			and _assert(strokes.size() == 1,
					"A one-pixel diagonal produced %d strokes; expected 1." % strokes.size()) \
			and _assert(_stroke_connects(strokes, start, finish),
					"The one-pixel diagonal was split or lost.")


func _test_same_component_white_slot_not_bridged() -> bool:
	var image := Image.create(JUNCTION_IMAGE_SIZE.x, JUNCTION_IMAGE_SIZE.y, false, Image.FORMAT_RGBA8)
	image.fill(Color.WHITE)
	for y in range(24, 105):
		image.set_pixel(56, y, Color.BLACK)
		image.set_pixel(58, y, Color.BLACK)
	for x in range(56, 59):
		image.set_pixel(x, 104, Color.BLACK)
	var strokes: Array[Dictionary] = TopologyCenterlineVectorizer.vectorize_image(
			image, {"threshold": 0.0, "despeckling": 0.0})
	print("TopologyCenterline same-component white slot: %d strokes" % strokes.size())
	var ok := _assert(_foreground_component_count_8(image) == 1,
			"The white-slot U fixture is not one 8-connected foreground component.")
	for y in range(24, 104):
		ok = _assert(image.get_pixel(57, y).get_luminance() >= 1.0,
				"The white-slot U fixture is filled at y=%d." % y) and ok
	ok = _assert(strokes.size() == 1,
			"A one-pixel U produced %d strokes; expected one continuous stroke." % strokes.size()) and ok
	for probe_y in [40.5, 88.5]:
		ok = _assert(_nearest_stroke_distance(strokes, Vector2(56.5, probe_y)) <= 1.5,
				"The left arm of the one-pixel U was lost near y=%.1f." % probe_y) and ok
		ok = _assert(_nearest_stroke_distance(strokes, Vector2(58.5, probe_y)) <= 1.5,
				"The right arm of the one-pixel U was lost near y=%.1f." % probe_y) and ok
	ok = _assert(!_strokes_intersect_segment(strokes, Vector2(57.5, 23.5), Vector2(57.5, 100.5)),
			"A vector segment crossed the white slot between locally separate arms.") and ok
	return ok


func _test_one_pixel_gap_not_bridged() -> bool:
	var start := Vector2(12.0, 64.5)
	var finish := Vector2(116.0, 64.5)
	var image := _make_horizontal_thin_neck_image()
	image.set_pixel(64, 64, Color.WHITE)
	var strokes: Array[Dictionary] = TopologyCenterlineVectorizer.vectorize_image(
			image, {"threshold": 0.0, "despeckling": 0.0})
	print("TopologyCenterline one-pixel gap: %d strokes" % strokes.size())
	var ok := _assert(_foreground_component_count_8(image) == 2,
			"The one-pixel-gap fixture must contain two foreground components.")
	ok = _assert(_stroke_connects(strokes, start, Vector2(63.5, 64.5)),
			"The left side of the one-pixel gap was lost.") and ok
	ok = _assert(_stroke_connects(strokes, Vector2(65.5, 64.5), finish),
			"The right side of the one-pixel gap was lost.") and ok
	ok = _assert(!_stroke_connects(strokes, start, finish),
			"Topology restoration crossed a one-pixel white gap.") and ok
	ok = _assert(strokes.size() == 2,
			"A one-pixel white gap produced %d strokes; expected two non-overlapping sides." % strokes.size()) and ok
	return ok


func _test_thin_neck_loop() -> bool:
	var top_left := Vector2(32.5, 32.5)
	var top_right := Vector2(96.5, 32.5)
	var bottom_right := Vector2(96.5, 96.5)
	var bottom_left := Vector2(32.5, 96.5)
	var image := Image.create(JUNCTION_IMAGE_SIZE.x, JUNCTION_IMAGE_SIZE.y, false, Image.FORMAT_RGBA8)
	image.fill(Color.WHITE)
	_draw_segment(image, top_left, Vector2(52.5, 32.5), 3.0)
	_draw_segment(image, Vector2(48.5, 32.5), Vector2(80.5, 32.5), 0.51)
	_draw_segment(image, Vector2(76.5, 32.5), top_right, 3.0)
	_draw_segment(image, top_right, bottom_right, 3.0)
	_draw_segment(image, bottom_right, bottom_left, 3.0)
	_draw_segment(image, bottom_left, top_left, 3.0)
	var strokes: Array[Dictionary] = TopologyCenterlineVectorizer.vectorize_image(
			image, {"threshold": 0.0, "despeckling": 0.0})
	print("TopologyCenterline thin-neck loop: %d strokes" % strokes.size())
	var ok := _assert(_foreground_component_count_8(image) == 1,
			"The thin-neck loop is not one foreground component.")
	ok = _assert(strokes.size() == 1,
			"The thin-neck loop produced %d strokes; expected 1." % strokes.size()) and ok
	if strokes.size() != 1 || !strokes[0].has("positions"):
		return false
	var positions: PackedVector2Array = strokes[0]["positions"]
	ok = _assert(positions.size() >= 3 && positions[0].distance_to(positions[positions.size() - 1]) <= SHARED_VERTEX_EPSILON,
			"The restored thin-neck loop is not closed.") and ok
	for midpoint in [Vector2(64.5, 32.5), Vector2(96.5, 64.5),
			Vector2(64.5, 96.5), Vector2(32.5, 64.5)]:
		ok = _assert(_nearest_stroke_distance(strokes, midpoint) <= 3.5,
				"The thin-neck loop missed source support near %s." % midpoint) and ok
	return ok


func _test_self_touching_loop() -> bool:
	var stem_start := Vector2(32.0, 112.0)
	var stem_end := Vector2(32.0, 32.0)
	var top_right := Vector2(96.0, 32.0)
	var bottom_right := Vector2(96.0, 96.0)
	var contact := Vector2(32.0, 96.0)
	var segments := [
		[stem_start, stem_end],
		[stem_end, top_right],
		[top_right, bottom_right],
		[bottom_right, contact],
	]
	var image := _make_junction_image(segments, 4.0)
	var strokes: Array[Dictionary] = TopologyCenterlineVectorizer.vectorize_image(
			image, {"threshold": 0.0, "despeckling": 0.0})
	print("TopologyCenterline self-touching loop: %d strokes" % strokes.size())
	var ok := _assert(strokes.size() == 1,
			"Self-touching loop produced %d strokes; expected 1." % strokes.size())
	ok = _assert_source_segments_owned_once("self-touching loop", strokes, segments) and ok
	if strokes.size() != 1 || !strokes[0].has("positions"):
		return false
	var positions: PackedVector2Array = strokes[0]["positions"]
	var last := positions.size() - 1
	var contact_is_front := positions[0].distance_to(contact) < positions[last].distance_to(contact)
	var contact_endpoint := positions[0] if contact_is_front else positions[last]
	var free_endpoint := positions[last] if contact_is_front else positions[0]
	ok = _assert(free_endpoint.distance_to(stem_start) <= JUNCTION_TIP_TOLERANCE,
			"Self-touching loop lost its free endpoint.") and ok
	ok = _assert(contact_endpoint.distance_to(contact) <= JUNCTION_PLACEMENT_TOLERANCE,
			"Self-touching endpoint missed the visual contact.") and ok
	var repeated_as_interior := false
	for index in range(1, last):
		if positions[index].distance_to(contact_endpoint) <= SHARED_VERTEX_EPSILON:
			repeated_as_interior = true
			break
	ok = _assert(repeated_as_interior,
			"Self-touch was not encoded as a terminal vertex repeated inside the same stroke.") and ok
	return ok


func _test_radius_guide() -> bool:
	var line_y := 32.0
	var image := _make_junction_image([
		[Vector2(16.0, line_y), Vector2(112.0, line_y)],
	])
	var topology_strokes: Array[Dictionary] = TopologyCenterlineVectorizer.vectorize_image(
			image, {"threshold": 0.0, "despeckling": 0.0})
	var topology_radii := _interior_radii(topology_strokes, line_y)
	var ok := _assert(!topology_radii.is_empty(), "Topology produced no interior radius samples.")
	if topology_radii.is_empty():
		return false
	var topology_median := _median(topology_radii)
	const REFERENCE_RADIUS := 2.318
	print("TopologyCenterline radius guide: topology=%.3f, reference=%.3f" % [topology_median, REFERENCE_RADIUS])
	ok = _assert(absf(topology_median - REFERENCE_RADIUS) <= RADIUS_GUIDE_TOLERANCE,
			"Topology median radius %.3f differs from the reference %.3f." %
			[topology_median, REFERENCE_RADIUS]) and ok
	return ok


func _interior_radii(strokes: Array[Dictionary], line_y: float) -> Array[float]:
	var result: Array[float] = []
	for stroke in strokes:
		if !stroke.has("positions") || !stroke.has("radii"):
			continue
		var positions: PackedVector2Array = stroke["positions"]
		var radii: PackedFloat32Array = stroke["radii"]
		for segment in mini(positions.size(), radii.size()) - 1:
			var start := positions[segment]
			var finish := positions[segment + 1]
			var delta_x := finish.x - start.x
			if absf(delta_x) <= 1.0e-6:
				continue
			for target_x in range(32, 97, 4):
				var weight := (float(target_x) - start.x) / delta_x
				if weight < 0.0 || weight > 1.0:
					continue
				var position := start.lerp(finish, weight)
				if absf(position.y - line_y) <= 4.0:
					result.push_back(lerpf(radii[segment], radii[segment + 1], weight))
	return result


func _median(values: Array[float]) -> float:
	var sorted := values.duplicate()
	sorted.sort()
	var middle := sorted.size() / 2
	if sorted.size() % 2 == 1:
		return sorted[middle]
	return (sorted[middle - 1] + sorted[middle]) * 0.5


func _make_junction_image(segments: Array, line_radius: float = JUNCTION_LINE_RADIUS) -> Image:
	var image := Image.create(JUNCTION_IMAGE_SIZE.x, JUNCTION_IMAGE_SIZE.y, false, Image.FORMAT_RGBA8)
	image.fill(Color.WHITE)
	for raw_segment in segments:
		var segment: Array = raw_segment
		var start: Vector2 = segment[0]
		var finish: Vector2 = segment[1]
		_draw_segment(image, start, finish, line_radius)
	return image


func _make_path_image(paths: Array, line_radius: float) -> Image:
	var image := Image.create(JUNCTION_IMAGE_SIZE.x, JUNCTION_IMAGE_SIZE.y, false, Image.FORMAT_RGBA8)
	image.fill(Color.WHITE)
	for raw_path in paths:
		var path: PackedVector2Array = raw_path
		for point_index in path.size() - 1:
			_draw_segment(image, path[point_index], path[point_index + 1], line_radius)
	return image


func _make_horizontal_thin_neck_image() -> Image:
	var image := Image.create(JUNCTION_IMAGE_SIZE.x, JUNCTION_IMAGE_SIZE.y, false, Image.FORMAT_RGBA8)
	image.fill(Color.WHITE)
	_draw_segment(image, Vector2(12.0, 64.5), Vector2(52.0, 64.5), 5.0)
	_draw_segment(image, Vector2(48.0, 64.5), Vector2(80.0, 64.5), 0.51)
	_draw_segment(image, Vector2(76.0, 64.5), Vector2(116.0, 64.5), 5.0)
	return image


func _foreground_component_count_8(image: Image) -> int:
	var width := image.get_width()
	var height := image.get_height()
	var visited := PackedByteArray()
	visited.resize(width * height)
	var components := 0
	for seed in visited.size():
		var seed_x := seed % width
		var seed_y := floori(float(seed) / float(width))
		if visited[seed] || image.get_pixel(seed_x, seed_y).get_luminance() >= 1.0:
			continue
		components += 1
		var queue: Array[int] = [seed]
		visited[seed] = 1
		var head := 0
		while head < queue.size():
			var pixel := queue[head]
			head += 1
			var x := pixel % width
			var y := floori(float(pixel) / float(width))
			for dy in range(-1, 2):
				for dx in range(-1, 2):
					if dx == 0 && dy == 0:
						continue
					var nx := x + dx
					var ny := y + dy
					if nx < 0 || ny < 0 || nx >= width || ny >= height:
						continue
					var neighbor := ny * width + nx
					if !visited[neighbor] && image.get_pixel(nx, ny).get_luminance() < 1.0:
						visited[neighbor] = 1
						queue.append(neighbor)
	return components


func _sample_quadratic_path(start: Vector2, control: Vector2, finish: Vector2,
		subdivisions: int) -> PackedVector2Array:
	var result := PackedVector2Array()
	for subdivision in subdivisions + 1:
		var weight := float(subdivision) / float(subdivisions)
		var inverse := 1.0 - weight
		result.append(start * (inverse * inverse) + control * (2.0 * inverse * weight) +
				finish * (weight * weight))
	return result


func _draw_segment(image: Image, start: Vector2, finish: Vector2, line_radius: float = JUNCTION_LINE_RADIUS) -> void:
	var min_x := maxi(0, floori(minf(start.x, finish.x) - line_radius - 1.0))
	var max_x := mini(JUNCTION_IMAGE_SIZE.x - 1, ceili(maxf(start.x, finish.x) + line_radius + 1.0))
	var min_y := maxi(0, floori(minf(start.y, finish.y) - line_radius - 1.0))
	var max_y := mini(JUNCTION_IMAGE_SIZE.y - 1, ceili(maxf(start.y, finish.y) + line_radius + 1.0))
	var delta := finish - start
	var segment_length_squared: float = delta.length_squared()
	if segment_length_squared <= 0.0:
		return
	for y in range(min_y, max_y + 1):
		for x in range(min_x, max_x + 1):
			var point := Vector2(x + 0.5, y + 0.5)
			var t: float = clampf((point - start).dot(delta) / segment_length_squared, 0.0, 1.0)
			if point.distance_to(start + delta * t) <= line_radius:
				image.set_pixel(x, y, Color.BLACK)


func _nearest_distance(points: PackedVector2Array, target: Vector2) -> float:
	var nearest: float = 1.0e20
	for point in points:
		nearest = minf(nearest, point.distance_to(target))
	return nearest


func _nearby_point_count(points: PackedVector2Array, target: Vector2, tolerance: float) -> int:
	var count := 0
	for point in points:
		if point.distance_to(target) <= tolerance:
			count += 1
	return count


func _point_segment_distance(point: Vector2, start: Vector2, finish: Vector2) -> float:
	var delta := finish - start
	var length_squared := delta.length_squared()
	if length_squared <= 0.0:
		return point.distance_to(start)
	var t := clampf((point - start).dot(delta) / length_squared, 0.0, 1.0)
	return point.distance_to(start + delta * t)


func _nearest_stroke_distance(strokes: Array[Dictionary], target: Vector2) -> float:
	var nearest := 1.0e20
	for stroke in strokes:
		if !stroke.has("positions"):
			continue
		var positions: PackedVector2Array = stroke["positions"]
		for segment in positions.size() - 1:
			nearest = minf(nearest, _point_segment_distance(target, positions[segment], positions[segment + 1]))
	return nearest


func _strokes_intersect_segment(strokes: Array[Dictionary], start: Vector2, finish: Vector2) -> bool:
	for stroke in strokes:
		if !stroke.has("positions"):
			continue
		var positions: PackedVector2Array = stroke["positions"]
		for segment in positions.size() - 1:
			if Geometry2D.segment_intersects_segment(positions[segment], positions[segment + 1], start, finish) != null:
				return true
	return false


func _stroke_covers_source_probe(stroke: Dictionary, probe: Vector2, source_direction: Vector2) -> bool:
	if !stroke.has("positions"):
		return false
	var positions: PackedVector2Array = stroke["positions"]
	for sample_index in positions.size() - 1:
		var start := positions[sample_index]
		var finish := positions[sample_index + 1]
		var delta := finish - start
		var length_squared := delta.length_squared()
		if length_squared <= 0.0:
			continue
		if absf(delta.normalized().dot(source_direction)) < SOURCE_DIRECTION_DOT:
			continue
		if _point_segment_distance(probe, start, finish) <= SOURCE_OWNER_DISTANCE:
			return true
	return false


func _source_probe_is_ambiguous(probe: Vector2, source_index: int, segments: Array) -> bool:
	for other_index in segments.size():
		if other_index == source_index:
			continue
		var other: Array = segments[other_index]
		if other.size() < 2:
			continue
		var other_start: Vector2 = other[0]
		var other_finish: Vector2 = other[1]
		if _point_segment_distance(probe, other_start, other_finish) <= SOURCE_AMBIGUITY_DISTANCE:
			return true
	return false


func _assert_source_segments_owned_once(name: String, strokes: Array[Dictionary], segments: Array) -> bool:
	var ok := true
	for source_index in segments.size():
		var source: Array = segments[source_index]
		if source.size() < 2:
			ok = _assert(false, "%s source primitive %d has fewer than two endpoints." % [name, source_index]) and ok
			continue
		var source_start: Vector2 = source[0]
		var source_finish: Vector2 = source[1]
		var delta := source_finish - source_start
		var length := delta.length()
		if length <= 0.0:
			ok = _assert(false, "%s source primitive %d has zero length." % [name, source_index]) and ok
			continue
		var direction := delta / length
		var interval_count := maxi(2, ceili(length / SOURCE_PROBE_SPACING))
		var stable_owner := -1
		var usable_probe_count := 0
		for probe_index in range(1, interval_count):
			var t := float(probe_index) / float(interval_count)
			var probe := source_start.lerp(source_finish, t)
			if _source_probe_is_ambiguous(probe, source_index, segments):
				continue
			usable_probe_count += 1
			var owners: Array[int] = []
			for stroke_index in strokes.size():
				if _stroke_covers_source_probe(strokes[stroke_index], probe, direction):
					owners.push_back(stroke_index)
			ok = _assert(owners.size() == 1,
					"%s source primitive %d probe %s has %d stroke owners; expected 1." %
					[name, source_index, probe, owners.size()]) and ok
			if owners.size() != 1:
				continue
			if stable_owner < 0:
				stable_owner = owners[0]
			else:
				ok = _assert(owners[0] == stable_owner,
						"%s source primitive %d changed owner from stroke %d to stroke %d near %s." %
						[name, source_index, stable_owner, owners[0], probe]) and ok
		ok = _assert(usable_probe_count > 0,
				"%s source primitive %d has no probes outside junction ambiguity regions." % [name, source_index]) and ok
	return ok


func _shared_vertex_owner_count(strokes: Array[Dictionary], target: Vector2) -> int:
	var best_count := 0
	for source_stroke in strokes:
		if !source_stroke.has("positions"):
			continue
		var source_positions: PackedVector2Array = source_stroke["positions"]
		for candidate in source_positions:
			if candidate.distance_to(target) > X_CORRIDOR_TOLERANCE:
				continue
			var owner_count := 0
			for owner_stroke in strokes:
				if !owner_stroke.has("positions"):
					continue
				var owner_positions: PackedVector2Array = owner_stroke["positions"]
				for position in owner_positions:
					if position.distance_to(candidate) <= SHARED_VERTEX_EPSILON:
						owner_count += 1
						break
			best_count = maxi(best_count, owner_count)
	return best_count


func _nearest_shared_vertex_distance(strokes: Array[Dictionary], target: Vector2) -> float:
	var nearest := 1.0e20
	for source_stroke in strokes:
		if !source_stroke.has("positions"):
			continue
		var source_positions: PackedVector2Array = source_stroke["positions"]
		for candidate in source_positions:
			var owners := 0
			for owner_stroke in strokes:
				if !owner_stroke.has("positions"):
					continue
				var owner_positions: PackedVector2Array = owner_stroke["positions"]
				for position in owner_positions:
					if position.distance_to(candidate) <= SHARED_VERTEX_EPSILON:
						owners += 1
						break
			if owners >= 2:
				nearest = minf(nearest, candidate.distance_to(target))
	return nearest


func _assert_source_intersections_are_vertices(name: String, strokes: Array[Dictionary], segments: Array) -> bool:
	var intersections := PackedVector2Array()
	for first_index in segments.size():
		var first: Array = segments[first_index]
		if first.size() < 2:
			continue
		for second_index in range(first_index + 1, segments.size()):
			var second: Array = segments[second_index]
			if second.size() < 2:
				continue
			var intersection = Geometry2D.segment_intersects_segment(first[0], first[1], second[0], second[1])
			if intersection == null:
				continue
			var point: Vector2 = intersection
			if _nearest_distance(intersections, point) > SHARED_VERTEX_EPSILON:
				intersections.append(point)
	var ok := true
	for intersection in intersections:
		var owners := _shared_vertex_owner_count(strokes, intersection)
		if owners < 2:
			for stroke in strokes:
				if stroke.has("positions"):
					print("%s intersection debug: %s" % [name, stroke["positions"]])
		ok = _assert(owners >= 2,
				"%s source intersection near %s is not an explicit shared polyline vertex." % [name, intersection]) and ok
		var placement_error := _nearest_shared_vertex_distance(strokes, intersection)
		var placement := _find_shared_vertex_near(strokes, intersection, JUNCTION_CENTER_TOLERANCE)
		ok = _assert(placement_error <= JUNCTION_PLACEMENT_TOLERANCE,
				"%s shared vertex %s is %.3fpx from visual junction %s; expected at most %.3fpx." %
				[name, placement["position"], placement_error, intersection, JUNCTION_PLACEMENT_TOLERANCE]) and ok
	return ok


func _find_shared_vertex_near(strokes: Array[Dictionary], target: Vector2, search_radius: float) -> Dictionary:
	var best_position := Vector2.ZERO
	var best_distance := 1.0e20
	var best_owners := 0
	for source_stroke in strokes:
		if !source_stroke.has("positions"):
			continue
		var source_positions: PackedVector2Array = source_stroke["positions"]
		for candidate in source_positions:
			var distance := candidate.distance_to(target)
			if distance > search_radius:
				continue
			var owners := 0
			for owner_stroke in strokes:
				if !owner_stroke.has("positions"):
					continue
				var owner_positions: PackedVector2Array = owner_stroke["positions"]
				for position in owner_positions:
					if position.distance_to(candidate) <= SHARED_VERTEX_EPSILON:
						owners += 1
						break
			if owners > best_owners || (owners == best_owners && distance < best_distance):
				best_owners = owners
				best_distance = distance
				best_position = candidate
	return {
		"found": best_owners >= 2,
		"position": best_position,
		"owners": best_owners,
	}


func _find_stroke_with_endpoints(strokes: Array[Dictionary], first: Vector2, second: Vector2,
		first_tolerance: float, second_tolerance: float = -1.0) -> int:
	var resolved_second_tolerance := first_tolerance if second_tolerance < 0.0 else second_tolerance
	for stroke_index in strokes.size():
		if !strokes[stroke_index].has("positions"):
			continue
		var positions: PackedVector2Array = strokes[stroke_index]["positions"]
		if positions.size() < 2:
			continue
		var front := positions[0]
		var back := positions[positions.size() - 1]
		if (front.distance_to(first) <= first_tolerance && back.distance_to(second) <= resolved_second_tolerance) || \
				(front.distance_to(second) <= resolved_second_tolerance && back.distance_to(first) <= first_tolerance):
			return stroke_index
	return -1


func _orient_polyline_from_vertex(positions: PackedVector2Array, vertex: Vector2) -> PackedVector2Array:
	if positions.is_empty():
		return PackedVector2Array()
	if positions[0].distance_to(vertex) <= SHARED_VERTEX_EPSILON:
		return positions
	if positions[positions.size() - 1].distance_to(vertex) > SHARED_VERTEX_EPSILON:
		return PackedVector2Array()
	var result := PackedVector2Array()
	for index in positions.size():
		result.append(positions[positions.size() - 1 - index])
	return result


func _polyline_length(path: PackedVector2Array) -> float:
	var length := 0.0
	for segment in path.size() - 1:
		length += path[segment].distance_to(path[segment + 1])
	return length


func _sample_polyline_at_distance(path: PackedVector2Array, distance: float) -> Vector2:
	if path.is_empty():
		return Vector2.INF
	var traversed := 0.0
	for segment in path.size() - 1:
		var length := path[segment].distance_to(path[segment + 1])
		if traversed + length >= distance && length > 1.0e-6:
			return path[segment].lerp(path[segment + 1], (distance - traversed) / length)
		traversed += length
	return path[path.size() - 1]


func _sample_polyline_from_end(path: PackedVector2Array, distance: float) -> Vector2:
	var reversed := PackedVector2Array()
	for index in path.size():
		reversed.append(path[path.size() - 1 - index])
	return _sample_polyline_at_distance(reversed, distance)


func _nearest_polyline_distance(point: Vector2, path: PackedVector2Array) -> float:
	var nearest := 1.0e20
	for segment in path.size() - 1:
		nearest = minf(nearest, _point_segment_distance(point, path[segment], path[segment + 1]))
	return nearest


func _assert_terminal_trend(outward: PackedVector2Array, reference: PackedVector2Array,
		expected_tangent: Vector2, corridor_tolerance: float) -> bool:
	const STEP := 6.0
	const SPAN := 24.0
	var ok := _assert(_polyline_length(outward) >= SPAN,
			"Curved oblique T branch has no complete terminal support window.")
	if !ok:
		return false
	var q0 := _sample_polyline_at_distance(outward, 0.0)
	var q6 := _sample_polyline_at_distance(outward, STEP)
	var q12 := _sample_polyline_at_distance(outward, STEP * 2.0)
	var q18 := _sample_polyline_at_distance(outward, STEP * 3.0)
	var near_tangent := (q0 - q6).normalized()
	var middle_tangent := (q6 - q12).normalized()
	var far_tangent := (q12 - q18).normalized()
	ok = _assert(near_tangent.dot(expected_tangent) >= cos(deg_to_rad(20.0)),
			"Curved oblique T branch ignored its incoming trend at the junction.") and ok
	ok = _assert(near_tangent.dot(middle_tangent) >= cos(deg_to_rad(25.0)) &&
			middle_tangent.dot(far_tangent) >= cos(deg_to_rad(25.0)),
			"Curved oblique T branch contains a terminal kink or bulge.") and ok
	ok = _assert(q0.distance_to(q18) >= 18.0 / 1.08,
			"Curved oblique T branch has excessive terminal arc length.") and ok
	var maximum_corridor_error := 0.0
	for distance in range(0, int(SPAN) + 1, 2):
		var output_point := _sample_polyline_at_distance(outward, float(distance))
		var reference_point := _sample_polyline_from_end(reference, float(distance))
		maximum_corridor_error = maxf(maximum_corridor_error,
				_nearest_polyline_distance(output_point, reference))
		maximum_corridor_error = maxf(maximum_corridor_error,
				_nearest_polyline_distance(reference_point, outward))
	ok = _assert(maximum_corridor_error <= corridor_tolerance,
			"Curved oblique T branch left its source trend corridor by %.3fpx." % maximum_corridor_error) and ok
	return ok


func _connecting_stroke_sample_count(strokes: Array[Dictionary], first: Vector2, second: Vector2) -> int:
	for stroke in strokes:
		if !stroke.has("positions"):
			continue
		var positions: PackedVector2Array = stroke["positions"]
		if positions.size() < 2:
			continue
		var front := positions[0]
		var back := positions[positions.size() - 1]
		if (front.distance_to(first) <= JUNCTION_TIP_TOLERANCE && back.distance_to(second) <= JUNCTION_TIP_TOLERANCE) || \
				(front.distance_to(second) <= JUNCTION_TIP_TOLERANCE && back.distance_to(first) <= JUNCTION_TIP_TOLERANCE):
			return positions.size()
	return 0


func _stroke_follows_straight_corridor_window(strokes: Array[Dictionary], first: Vector2, second: Vector2,
		center: Vector2, half_span: float) -> bool:
	var source_delta := second - first
	var source_length := source_delta.length()
	if source_length <= 0.0:
		return false
	var source_direction := source_delta / source_length
	var center_projection := (center - first).dot(source_direction)
	for stroke in strokes:
		if !stroke.has("positions"):
			continue
		var positions: PackedVector2Array = stroke["positions"]
		if positions.size() < 2:
			continue
		var forward_error := positions[0].distance_to(first) + positions[positions.size() - 1].distance_to(second)
		var reverse_error := positions[0].distance_to(second) + positions[positions.size() - 1].distance_to(first)
		if minf(forward_error, reverse_error) > JUNCTION_TIP_TOLERANCE * 2.0:
			continue
		var forward := forward_error <= reverse_error
		var inspected_segment := false
		var follows := true
		for ordered_index in positions.size() - 1:
			var first_index := ordered_index if forward else positions.size() - 1 - ordered_index
			var second_index := ordered_index + 1 if forward else positions.size() - 2 - ordered_index
			var start := positions[first_index]
			var finish := positions[second_index]
			var midpoint_projection := ((start + finish) * 0.5 - first).dot(source_direction)
			if absf(midpoint_projection - center_projection) > half_span:
				continue
			inspected_segment = true
			if _point_segment_distance(start, first, second) > X_CORRIDOR_TOLERANCE || \
					_point_segment_distance(finish, first, second) > X_CORRIDOR_TOLERANCE:
				follows = false
				break
			var segment := finish - start
			if segment.length_squared() > 1.0e-8 && segment.normalized().dot(source_direction) < SOURCE_DIRECTION_DOT:
				follows = false
				break
		if inspected_segment && follows:
			return true
	return false


func _stroke_follows_straight_corridor(strokes: Array[Dictionary], first: Vector2, second: Vector2) -> bool:
	var source_delta := second - first
	var source_length := source_delta.length()
	if source_length <= 0.0:
		return false
	var source_direction := source_delta / source_length
	for stroke in strokes:
		if !stroke.has("positions"):
			continue
		var positions: PackedVector2Array = stroke["positions"]
		if positions.size() < 2:
			continue
		var front := positions[0]
		var back := positions[positions.size() - 1]
		var forward_error := front.distance_to(first) + back.distance_to(second)
		var reverse_error := front.distance_to(second) + back.distance_to(first)
		var forward_matches := front.distance_to(first) <= JUNCTION_TIP_TOLERANCE && \
				back.distance_to(second) <= JUNCTION_TIP_TOLERANCE
		var reverse_matches := front.distance_to(second) <= JUNCTION_TIP_TOLERANCE && \
				back.distance_to(first) <= JUNCTION_TIP_TOLERANCE
		if !forward_matches && !reverse_matches:
			continue
		var forward := forward_matches && (!reverse_matches || forward_error <= reverse_error)
		var maximum_projection := -1.0e20
		var previous_position := Vector2.ZERO
		var follows_corridor := true
		for ordered_index in positions.size():
			var sample_index := ordered_index if forward else positions.size() - 1 - ordered_index
			var position := positions[sample_index]
			if _point_segment_distance(position, first, second) > X_CORRIDOR_TOLERANCE:
				follows_corridor = false
				break
			var projection := (position - first).dot(source_direction)
			if projection < maximum_projection - X_MAX_PROJECTION_BACKTRACK:
				follows_corridor = false
				break
			if ordered_index > 0:
				var segment := position - previous_position
				if segment.length_squared() > 1.0e-8 && segment.normalized().dot(source_direction) < SOURCE_DIRECTION_DOT:
					follows_corridor = false
					break
			maximum_projection = maxf(maximum_projection, projection)
			previous_position = position
		if follows_corridor:
			return true
	return false


func _stroke_connects(strokes: Array[Dictionary], first: Vector2, second: Vector2) -> bool:
	for stroke in strokes:
		if !stroke.has("positions"):
			continue
		var positions: PackedVector2Array = stroke["positions"]
		if positions.size() < 2:
			continue
		var front := positions[0]
		var back := positions[positions.size() - 1]
		if (front.distance_to(first) <= JUNCTION_TIP_TOLERANCE && back.distance_to(second) <= JUNCTION_TIP_TOLERANCE) || \
				(front.distance_to(second) <= JUNCTION_TIP_TOLERANCE && back.distance_to(first) <= JUNCTION_TIP_TOLERANCE):
			return true
	return false


func _is_finite(value: float) -> bool:
	return !is_nan(value) && !is_inf(value)


func _assert(condition: bool, message: String) -> bool:
	if !condition:
		push_error(message)
		return false
	return true
