extends SceneTree

func _init() -> void:
	var ok := true
	ok = _test_polyline_query_curves_returns_unique_ids() and ok
	ok = _test_polyline_query_curves_ignores_poly_t_work() and ok
	ok = _test_endpoint_lengths_without_intersection_return_total_length() and ok
	ok = _test_endpoint_lengths_to_first_degree_gt_two_vertex() and ok
	ok = _test_endpoint_lengths_return_zero_when_endpoint_is_junction() and ok
	ok = _test_endpoint_info_reports_dangling() and ok
	ok = _test_closed_curve_without_intersection_returns_total_length() and ok
	if ok:
		print("ARRANGEMENT_2D_QUERY_CURVES_SMOKE_OK")
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


func _has_id(ids: PackedInt64Array, id: int) -> bool:
	for value in ids:
		if int(value) == id:
			return true
	return false


func _test_polyline_query_curves_returns_unique_ids() -> bool:
	var arrangement := Arrangement2D.new()
	arrangement.set_polyline(10, _polyline([Vector2(0, 0), Vector2(10, 0)]))
	arrangement.set_polyline(20, _polyline([Vector2(7, -5), Vector2(7, 5)]))
	arrangement.set_polyline(30, _polyline([Vector2(20, 0), Vector2(30, 0)]))
	var ids: PackedInt64Array = arrangement.polyline_query_curves(_polyline([Vector2(4, -1), Vector2(8, 1)]))
	var ok := _assert(ids.size() == 2, "Expected exactly two crossed source curve ids.") \
			and _assert(_has_id(ids, 10), "Expected query to include horizontal source curve.") \
			and _assert(_has_id(ids, 20), "Expected query to include vertical source curve.") \
			and _assert(!_has_id(ids, 30), "Expected query to exclude untouched source curve.")
	arrangement.free()
	return ok


func _test_polyline_query_curves_ignores_poly_t_work() -> bool:
	var arrangement := Arrangement2D.new()
	arrangement.set_polyline(40, _polyline([Vector2(0, 0), Vector2(10, 0)]))
	var ids: PackedInt64Array = arrangement.polyline_query_curves(_polyline([
		Vector2(2, -1),
		Vector2(2, 1),
		Vector2(8, -1),
		Vector2(8, 1),
	]))
	var ok := _assert(ids.size() == 1, "Expected repeated hits on the same curve to be deduplicated.") \
			and _assert(int(ids[0]) == 40, "Expected the unique hit curve id.")
	arrangement.free()
	return ok


func _test_endpoint_lengths_without_intersection_return_total_length() -> bool:
	var arrangement := Arrangement2D.new()
	arrangement.set_polyline(50, _polyline([Vector2(0, 0), Vector2(3, 4), Vector2(6, 4)]))
	var lengths: Vector2 = arrangement.get_curve_endpoint_junction_lengths(50)
	var ok := _assert(is_equal_approx(lengths.x, 8.0), "Expected start length to be the whole source length.") \
			and _assert(is_equal_approx(lengths.y, 8.0), "Expected end length to be the whole source length.")
	arrangement.free()
	return ok


func _test_endpoint_lengths_to_first_degree_gt_two_vertex() -> bool:
	var arrangement := Arrangement2D.new()
	arrangement.set_polyline(60, _polyline([Vector2(0, 0), Vector2(10, 0)]))
	arrangement.set_polyline(61, _polyline([Vector2(4, -1), Vector2(4, 1)]))
	var lengths: Vector2 = arrangement.get_curve_endpoint_junction_lengths(60)
	var ok := _assert(is_equal_approx(lengths.x, 4.0), "Expected start length to stop at first degree > 2 vertex.") \
			and _assert(is_equal_approx(lengths.y, 6.0), "Expected end length to stop at first degree > 2 vertex.")
	arrangement.free()
	return ok


func _test_endpoint_lengths_return_zero_when_endpoint_is_junction() -> bool:
	var arrangement := Arrangement2D.new()
	arrangement.set_polyline(65, _polyline([Vector2(0, 0), Vector2(10, 0)]))
	arrangement.set_polyline(66, _polyline([Vector2(0, 0), Vector2(0, 2)]))
	arrangement.set_polyline(67, _polyline([Vector2(0, 0), Vector2(0, -2)]))
	var lengths: Vector2 = arrangement.get_curve_endpoint_junction_lengths(65)
	var ok := _assert(is_equal_approx(lengths.x, 0.0), "Expected start length to be zero when the source endpoint is a junction.") \
			and _assert(is_equal_approx(lengths.y, 10.0), "Expected end length to span back to the endpoint junction.")
	arrangement.free()
	return ok


func _test_endpoint_info_reports_dangling() -> bool:
	var arrangement := Arrangement2D.new()
	arrangement.set_polyline(68, _polyline([Vector2(0, 0), Vector2(10, 0)]))
	arrangement.set_polyline(69, _polyline([Vector2(0, 0), Vector2(0, 2)]))
	arrangement.set_polyline(71, _polyline([Vector2(0, 0), Vector2(0, -2)]))
	var info: Dictionary = arrangement.get_curve_endpoint_info(68)
	var ok := _assert(!bool(info["start_dangling"]), "Expected endpoint on a junction not to be dangling.") \
			and _assert(bool(info["end_dangling"]), "Expected open endpoint to be dangling.") \
			and _assert(is_equal_approx(float(info["start_junction_length"]), 0.0), "Expected junction endpoint length to be zero.") \
			and _assert(is_equal_approx(float(info["end_junction_length"]), 10.0), "Expected dangling endpoint length to span to the junction.")
	arrangement.free()
	return ok


func _test_closed_curve_without_intersection_returns_total_length() -> bool:
	var arrangement := Arrangement2D.new()
	arrangement.set_polyline(70, _polyline([
		Vector2(0, 0),
		Vector2(1, 0),
		Vector2(1, 1),
		Vector2(0, 1),
		Vector2(0, 0),
	]))
	var lengths: Vector2 = arrangement.get_curve_endpoint_junction_lengths(70)
	var ok := _assert(is_equal_approx(lengths.x, 4.0), "Expected closed start direction to return whole curve length when no degree > 2 vertex exists.") \
			and _assert(is_equal_approx(lengths.y, 4.0), "Expected closed end direction to return whole curve length when no degree > 2 vertex exists.")
	arrangement.free()
	return ok
