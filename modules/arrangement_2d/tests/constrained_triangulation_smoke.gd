extends SceneTree

var failures := 0


func _init() -> void:
	_test_degenerate_input()
	_test_location()
	_test_intersections_and_overlaps()
	_test_precision()
	_test_hole_and_bridge()
	_test_snapshot_isolation()
	_test_crossing_soup()
	if failures == 0:
		print("CONSTRAINED_TRIANGULATION_2D_SMOKE_OK")
	quit(0 if failures == 0 else 1)


func _check(condition: bool, message: String) -> void:
	if not condition:
		failures += 1
		push_error(message)


func _framed(points: PackedVector2Array, constraints: PackedInt32Array) -> ConstrainedTriangulation2D:
	points = points.duplicate()
	var first := points.size()
	points.append_array(PackedVector2Array([Vector2(-20, -20), Vector2(20, -20), Vector2(20, 20), Vector2(-20, 20)]))
	return ConstrainedTriangulation2D.build(points, constraints, PackedInt32Array([first, first + 1, first + 2, first + 3]))


func _next(edge: int) -> int:
	return edge - edge % 3 + (edge + 1) % 3


func _check_graph(mesh: ConstrainedTriangulation2D) -> void:
	var graph := mesh.get_graph()
	var vertices: PackedFloat64Array = graph.vertices
	var triangles: PackedInt32Array = graph.triangles
	var opposites: PackedInt32Array = graph.opposites
	var constrained: PackedByteArray = graph.constrained
	var frame: PackedByteArray = graph.frame_vertices
	_check(vertices.size() % 2 == 0, "Coordinates must be xy pairs.")
	_check(triangles.size() % 3 == 0, "Triangles must have three corners.")
	_check(opposites.size() == triangles.size() and constrained.size() == triangles.size(), "Halfedge array sizes disagree.")
	_check(frame.size() * 2 == vertices.size(), "Frame flags must address output vertices.")
	var unique_edges := 0
	for edge in triangles.size():
		var a := triangles[edge]
		var b := triangles[_next(edge)]
		_check(a >= 0 and a < frame.size(), "Invalid vertex index.")
		var opposite := opposites[edge]
		_check(opposite >= -1 and opposite < opposites.size(), "Invalid opposite index.")
		if opposite == -1:
			unique_edges += 1
		else:
			_check(opposites[opposite] == edge, "Opposites must be reciprocal.")
			_check(triangles[opposite] == b and triangles[_next(opposite)] == a, "Opposite halfedges must reverse endpoints.")
			_check(constrained[opposite] == constrained[edge], "Constraint flags disagree across an edge.")
			if edge < opposite:
				unique_edges += 1
	for first in range(0, triangles.size(), 3):
		var a := triangles[first] * 2
		var b := triangles[first + 1] * 2
		var c := triangles[first + 2] * 2
		var area := (vertices[b] - vertices[a]) * (vertices[c + 1] - vertices[a + 1]) - (vertices[b + 1] - vertices[a + 1]) * (vertices[c] - vertices[a])
		_check(area > 0.0, "Exported triangles must be counterclockwise in coordinate space.")
	if mesh.get_dimension() == 2:
		_check(frame.size() - unique_edges + triangles.size() / 3 == 1, "Full triangulation must satisfy Euler's formula.")


func _constraint_count(mesh: ConstrainedTriangulation2D) -> int:
	var graph := mesh.get_graph()
	var result := 0
	for edge in graph.constrained.size():
		if graph.constrained[edge] and (graph.opposites[edge] == -1 or edge < graph.opposites[edge]):
			result += 1
	return result


func _test_degenerate_input() -> void:
	var empty := ConstrainedTriangulation2D.build(PackedVector2Array(), PackedInt32Array())
	_check(empty.get_dimension() == -1 and empty.get_graph().vertices.is_empty(), "Empty input must produce an empty snapshot.")
	_check(empty.locate(Vector2.ZERO) == ConstrainedTriangulation2D.OUTSIDE, "Empty input has no fill face.")
	var point := ConstrainedTriangulation2D.build(PackedVector2Array([Vector2.ZERO, Vector2.ZERO]), PackedInt32Array([0, 1]))
	_check(point.get_dimension() == 0 and point.get_graph().vertices.size() == 2, "Exact duplicate points must merge.")
	var line := ConstrainedTriangulation2D.build(PackedVector2Array([Vector2.ZERO, Vector2(1, 0), Vector2(2, 0)]), PackedInt32Array([0, 2]))
	_check(line.get_dimension() == 1 and line.get_graph().triangles.is_empty(), "Collinear input must not produce triangles.")
	_check(line.locate(Vector2(1, 0)) == ConstrainedTriangulation2D.OUTSIDE, "Collinear input has no fill face.")
	_check_graph(empty)
	_check_graph(point)
	_check_graph(line)


func _test_location() -> void:
	var points := PackedVector2Array([Vector2(0, 0), Vector2(4, 0), Vector2(4, 4), Vector2(0, 4)])
	var mesh := ConstrainedTriangulation2D.build(points, PackedInt32Array([0, 1, 1, 2, 2, 3, 3, 0]))
	_check_graph(mesh)
	_check(mesh.locate(Vector2(1, 2)) >= 0, "Interior point must locate a triangle.")
	_check(mesh.locate(Vector2(-1, 2)) == ConstrainedTriangulation2D.OUTSIDE, "Outside convex hull must be reported.")
	_check(mesh.locate(Vector2(2, 0)) == ConstrainedTriangulation2D.ON_CONSTRAINT, "Constraint edge hit must be reported.")
	_check(mesh.locate(Vector2.ZERO) == ConstrainedTriangulation2D.ON_CONSTRAINT, "Constraint endpoint hit must be reported.")
	_check(mesh.locate(Vector2(2, 2)) == 0, "An unconstrained diagonal hit must select the lowest adjacent triangle index.")
	var unconstrained := ConstrainedTriangulation2D.build(points, PackedInt32Array(), PackedInt32Array([0, 0, 2]))
	_check(unconstrained.locate(Vector2.ZERO) >= 0, "Unconstrained hull vertex must locate a finite incident triangle.")
	_check(unconstrained.locate(Vector2(2, 0)) >= 0, "Unconstrained hull edge must locate its finite triangle.")
	var marked := 0
	for flag in unconstrained.get_graph().frame_vertices:
		marked += flag
	_check(marked == 2 and _constraint_count(unconstrained) == 0, "Frame flags must neither duplicate nor constrain vertices.")


func _test_intersections_and_overlaps() -> void:
	var crossing := _framed(PackedVector2Array([Vector2(-4, 0), Vector2(4, 0), Vector2(0, -4), Vector2(0, 4)]), PackedInt32Array([0, 1, 2, 3]))
	_check_graph(crossing)
	_check(crossing.get_graph().vertices.size() == 18, "Crossing must construct exactly one new vertex.")
	_check(_constraint_count(crossing) == 4, "Crossing must split both constraints.")
	_check(crossing.locate(Vector2.ZERO) == ConstrainedTriangulation2D.ON_CONSTRAINT, "Constructed intersection must be a constraint vertex.")
	var junction := _framed(PackedVector2Array([Vector2(-4, 0), Vector2(4, 0), Vector2.ZERO, Vector2(0, 4), Vector2.ZERO]), PackedInt32Array([0, 1, 2, 3, 1, 0, 4, 4]))
	_check_graph(junction)
	_check(junction.get_graph().vertices.size() == 16 and _constraint_count(junction) == 3, "T-junction, reversed duplicates, and zero-length constraints must normalize.")
	var overlap := _framed(PackedVector2Array([Vector2(-4, 0), Vector2(2, 0), Vector2(-2, 0), Vector2(4, 0)]), PackedInt32Array([0, 1, 2, 3]))
	_check_graph(overlap)
	_check(_constraint_count(overlap) == 3, "Partial overlap must produce three constrained subsegments.")


func _test_precision() -> void:
	var rational := _framed(PackedVector2Array([Vector2(-1, 0), Vector2(1, 0), Vector2(0, -1), Vector2(1, 2)]), PackedInt32Array([0, 1, 2, 3]))
	_check_graph(rational)
	var vertices: PackedFloat64Array = rational.get_graph().vertices
	var found := false
	for i in range(0, vertices.size(), 2):
		if abs(vertices[i] - 1.0 / 3.0) < 1e-15 and vertices[i + 1] == 0.0:
			found = true
	_check(found, "Constructed coordinates must retain double precision across the binding.")
	var gap := _framed(PackedVector2Array([Vector2(-4, 0), Vector2.ZERO, Vector2(0.000001, 0), Vector2(4, 0)]), PackedInt32Array([0, 1, 2, 3]))
	_check_graph(gap)
	_check(gap.get_graph().vertices.size() == 16 and _constraint_count(gap) == 2, "A small positive gap must not be snapped shut.")
	_check(gap.locate(Vector2(0.0000005, 0)) >= 0, "A point inside a small gap must not become a constraint hit.")


func _component(graph: Dictionary, seed: int) -> Dictionary:
	var visited := {seed: true}
	var queue: Array[int] = [seed]
	var cursor := 0
	while cursor < queue.size():
		var face := queue[cursor]
		cursor += 1
		for side in 3:
			var edge := face * 3 + side
			var opposite: int = graph.opposites[edge]
			if opposite < 0 or graph.constrained[edge]:
				continue
			var neighbor := opposite / 3
			if not visited.has(neighbor):
				visited[neighbor] = true
				queue.append(neighbor)
	return visited


func _test_hole_and_bridge() -> void:
	var points := PackedVector2Array([Vector2(-8, -8), Vector2(8, -8), Vector2(8, 8), Vector2(-8, 8), Vector2(-2, -2), Vector2(2, -2), Vector2(2, 2), Vector2(-2, 2)])
	var constraints := PackedInt32Array([0, 1, 1, 2, 2, 3, 3, 0, 4, 5, 5, 6, 6, 7, 7, 4, 0, 4, 4, 0])
	var mesh := _framed(points, constraints)
	_check_graph(mesh)
	_check(_constraint_count(mesh) == 9, "A retraced hole bridge must remain a single constrained edge.")
	var ring := mesh.locate(Vector2(6, 1))
	var hole := mesh.locate(Vector2.ZERO)
	var outside_ring := mesh.locate(Vector2(12, 1))
	_check(ring >= 0 and hole >= 0 and outside_ring >= 0, "Full CDT must retain hole and exterior triangles.")
	var connected := _component(mesh.get_graph(), ring)
	_check(not connected.has(hole) and not connected.has(outside_ring), "Hole and outer constraints must block region connectivity.")
	_check(connected.has(mesh.locate(Vector2(-6, 1))), "The retraced bridge must not divide the annulus into disconnected regions.")


func _test_snapshot_isolation() -> void:
	var mesh := _framed(PackedVector2Array(), PackedInt32Array())
	var graph := mesh.get_graph()
	var coordinates: PackedFloat64Array = graph.vertices
	var original := coordinates[0]
	coordinates[0] = 999.0
	graph.vertices = coordinates
	graph.triangles = PackedInt32Array()
	_check(mesh.get_graph().vertices[0] == original and not mesh.get_graph().triangles.is_empty(), "Caller mutations must not alter the snapshot.")
	_check(mesh.locate(Vector2.ZERO) >= 0, "Graph export mutations must not alter native point location.")


func _test_crossing_soup() -> void:
	var rng := RandomNumberGenerator.new()
	rng.seed = 731
	for iteration in 8:
		var points := PackedVector2Array()
		var constraints := PackedInt32Array()
		for segment in 24:
			points.append(Vector2(rng.randf_range(-10, 10), rng.randf_range(-10, 10)))
			points.append(Vector2(rng.randf_range(-10, 10), rng.randf_range(-10, 10)))
			constraints.append_array(PackedInt32Array([segment * 2, segment * 2 + 1]))
		var mesh := _framed(points, constraints)
		_check_graph(mesh)
		for point in points:
			_check(mesh.locate(point) == ConstrainedTriangulation2D.ON_CONSTRAINT, "Every source endpoint must retain its constraint classification.")
