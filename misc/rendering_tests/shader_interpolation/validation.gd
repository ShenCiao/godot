extends SceneTree


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var failures := 0
	var cases: Array[Dictionary] = [
		{"name":"local", "declarations":"", "vertex":"point=VERTEX;", "fragment":"vec2 local_point=point; COLOR.xy=interpolateAtCentroid(local_point);"},
		{"name":"expression", "declarations":"", "vertex":"point=VERTEX;", "fragment":"COLOR.xy=interpolateAtCentroid(point+vec2(1.0));"},
		{"name":"uniform", "declarations":"uniform vec2 other;", "vertex":"point=VERTEX;", "fragment":"COLOR.xy=interpolateAtCentroid(other);"},
		{"name":"flat", "declarations":"varying flat vec2 other;", "vertex":"point=VERTEX; other=VERTEX;", "fragment":"COLOR.xy=interpolateAtCentroid(other);"},
		{"name":"unassigned", "declarations":"", "vertex":"", "fragment":"COLOR.xy=interpolateAtCentroid(point);"},
		{"name":"fragment_varying", "declarations":"", "vertex":"", "fragment":"point=UV; COLOR.xy=interpolateAtCentroid(point);"},
		{"name":"vertex_call", "declarations":"", "vertex":"point=VERTEX; VERTEX=interpolateAtCentroid(point);", "fragment":"COLOR.xy=point;"},
		{"name":"vertex_helper", "declarations":"vec2 helper() {return interpolateAtCentroid(point);}", "vertex":"point=VERTEX; VERTEX=helper();", "fragment":"COLOR.xy=point;"},
		{"name":"function_argument", "declarations":"vec2 helper(vec2 arg) {return interpolateAtCentroid(arg);}", "vertex":"point=VERTEX;", "fragment":"COLOR.xy=helper(point);"},
		{"name":"centroid_integer", "declarations":"varying centroid int other;", "vertex":"point=VERTEX; other=1;", "fragment":"COLOR.r=float(other);"},
		{"name":"centroid_fragment", "declarations":"varying centroid vec2 other;", "vertex":"point=VERTEX;", "fragment":"other=point; COLOR.xy=other;"},
		{"name":"unindexed_array", "declarations":"varying vec2 other[2];", "vertex":"point=VERTEX; other[0]=VERTEX; other[1]=VERTEX;", "fragment":"COLOR.xy=interpolateAtCentroid(other);"},
	]
	for item in cases:
		print("EXPECT_SHADER_ERROR ", item.name)
		var shader := Shader.new()
		shader.code = "shader_type canvas_item; uniform float validation_marker; varying vec2 point;\n" + item.declarations + "\nvoid vertex(){"+item.vertex+"}\nvoid fragment(){"+item.fragment+"}"
		if not shader.get_shader_uniform_list().is_empty():
			failures += 1
			push_error("Invalid shader accepted: " + item.name)
	var valid := Shader.new()
	valid.code = """shader_type canvas_item;
uniform float validation_marker;
varying vec2 point;
vec2 helper() { return interpolateAtCentroid(point); }
void fragment() { COLOR.xy=helper(); }
void vertex() { point=VERTEX; }
"""
	if valid.get_shader_uniform_list().is_empty():
		failures += 1
		push_error("Global varying in a helper, before vertex declaration, was rejected")
	print("INTERPOLATION_VALIDATION checks=%d failures=%d" % [cases.size()+1,failures])
	quit(1 if failures else 0)
