extends SceneTree

func _init() -> void:
	var args := OS.get_cmdline_user_args()
	if args.is_empty():
		quit(2)
		return
	var image := Image.new()
	var error := image.load(args[0])
	if error != OK:
		print("LOAD_ERROR ", error)
		quit(1)
		return
	image.resize(256, 256, Image.INTERPOLATE_LANCZOS)
	var strokes: Array[Dictionary] = TopologyCenterlineVectorizer.vectorize_image(image)
	print("COUNT ", strokes.size())
	for index in strokes.size():
		var positions: PackedVector2Array = strokes[index]["positions"]
		if positions.is_empty():
			print(index, " EMPTY")
			continue
		var first := positions[0]
		var last := positions[positions.size() - 1]
		var min_x := first.x
		var max_x := first.x
		var min_y := first.y
		var max_y := first.y
		for position in positions:
			min_x = min(min_x, position.x)
			max_x = max(max_x, position.x)
			min_y = min(min_y, position.y)
			max_y = max(max_y, position.y)
		print("%d N=%d FIRST=(%.2f,%.2f) LAST=(%.2f,%.2f) BOX=(%.2f,%.2f)-(%.2f,%.2f)" % [index, positions.size(), first.x, first.y, last.x, last.y, min_x, min_y, max_x, max_y])
	quit(0)
