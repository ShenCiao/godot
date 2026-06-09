extends SceneTree

func _init() -> void:
	var ok := true
	ok = _test_open_endpoint_gap() and ok
	ok = _test_threshold_too_small() and ok
	ok = _test_near_closed_arc() and ok
	ok = _test_closed_polygon() and ok
	ok = _test_same_curve_rejection() and ok
	ok = _test_crossing_constraint() and ok
	if ok:
		print("ARRANGEMENT_2D_GAP_BRIDGE_SMOKE_OK")
		quit()
	else:
		quit(1)


func _polyline(points: Array[Vector2]) -> PackedVector2Array:
	var result := PackedVector2Array()
	for point in points:
		result.push_back(point)
	return result


func _assert(condition: bool, message: String) -> bool:
	if !condition:
		push_error(message)
		return false
	return true


func _has_candidate(candidates: Array, from_id: int, to_id: int) -> bool:
	for candidate in candidates:
		var ids := [int(candidate["from_curve_id"]), int(candidate["to_curve_id"])]
		ids.sort()
		if ids[0] == from_id and ids[1] == to_id:
			return true
	return false


func _has_curve_t_pair(candidates: Array, curve_a: int, t_a: float, curve_b: int, t_b: float) -> bool:
	for candidate in candidates:
		var from_curve_id := int(candidate["from_curve_id"])
		var to_curve_id := int(candidate["to_curve_id"])
		var from_t := float(candidate["from_t"])
		var to_t := float(candidate["to_t"])
		if from_curve_id == curve_a and is_equal_approx(from_t, t_a) and to_curve_id == curve_b and is_equal_approx(to_t, t_b):
			return true
		if from_curve_id == curve_b and is_equal_approx(from_t, t_b) and to_curve_id == curve_a and is_equal_approx(to_t, t_a):
			return true
	return false


func _test_open_endpoint_gap() -> bool:
	var arrangement := Arrangement2D.new()
	arrangement.set_polyline(1, _polyline([Vector2(0, 0), Vector2(10, 0)]))
	arrangement.set_polyline(2, _polyline([Vector2(12, 0), Vector2(22, 0)]))
	var candidates: Array = arrangement.get_gap_bridge_candidates(3.0)
	var ok := _assert(candidates.size() == 1, "Expected one open endpoint gap candidate.") \
			and _assert(_has_candidate(candidates, 1, 2), "Expected candidate between curves 1 and 2.") \
			and _assert(candidates[0].has("from_t") and candidates[0].has("to_t") and candidates[0].has("score"), "Candidate fields are incomplete.")
	arrangement.free()
	return ok


func _test_threshold_too_small() -> bool:
	var arrangement := Arrangement2D.new()
	arrangement.set_polyline(1, _polyline([Vector2(0, 0), Vector2(10, 0)]))
	arrangement.set_polyline(2, _polyline([Vector2(12, 0), Vector2(22, 0)]))
	var ok := _assert(arrangement.get_gap_bridge_candidates(1.0).is_empty(), "Expected no candidate below gap length.")
	arrangement.free()
	return ok


func _test_near_closed_arc() -> bool:
	var arrangement := Arrangement2D.new()
	arrangement.set_polyline(10, _polyline([
		Vector2(1, 0),
		Vector2(0, 1),
		Vector2(-1, 0),
		Vector2(0, -1),
		Vector2(0.9, -0.1),
	]))
	var candidates: Array = arrangement.get_gap_bridge_candidates(0.5)
	var ok := _assert(!candidates.is_empty(), "Expected near-closed arc gap candidate.")
	arrangement.free()
	return ok


func _test_closed_polygon() -> bool:
	var arrangement := Arrangement2D.new()
	arrangement.set_polyline(20, _polyline([
		Vector2(0, 0),
		Vector2(10, 0),
		Vector2(10, 10),
		Vector2(0, 10),
		Vector2(0, 0),
	]))
	var ok := _assert(arrangement.get_gap_bridge_candidates(20.0).is_empty(), "Expected no endpoint candidate for closed polygon.")
	arrangement.free()
	return ok


func _test_same_curve_rejection() -> bool:
	var arrangement := Arrangement2D.new()
	arrangement.set_polyline(30, _polyline([Vector2(0, 0), Vector2(1, 0), Vector2(1.05, 0)]))
	var ok := _assert(arrangement.get_gap_bridge_candidates(2.0).is_empty(), "Expected same-curve adjacent t candidate to be rejected.")
	arrangement.free()
	return ok


func _test_crossing_constraint() -> bool:
	var arrangement := Arrangement2D.new()
	arrangement.set_polyline(40, _polyline([Vector2(0, 0), Vector2(10, 0)]))
	arrangement.set_polyline(41, _polyline([Vector2(4, -2), Vector2(4, 2)]))
	arrangement.set_polyline(42, _polyline([Vector2(3, -1), Vector2(5, 1)]))
	var candidates: Array = arrangement.get_gap_bridge_candidates(4.0)
	var ok := _assert(!_has_curve_t_pair(candidates, 40, 0.0, 42, 1.0), "Expected constrained crossing edge not to become a gap candidate.")
	arrangement.free()
	return ok
