extends SceneTree
## Frame-time check: runs 120 frames per configuration and prints the average wall time per frame.

var frame := 0
var main: Node
var t0 := 0
var configs := [[], ["show bones"], ["a.AnimNode.PoseHistory.DebugDrawTrajectory"], ["p.VisualizeMovement", "show collision"], ["stat fps"]]
var ci := 0

func _init() -> void:
	main = (load("res://scenes/main.tscn") as PackedScene).instantiate()
	root.add_child(main)

func _process(_dt: float) -> bool:
	frame += 1
	if frame == 30:
		t0 = Time.get_ticks_usec()
	if frame > 30 and (frame - 30) % 120 == 0:
		var us := (Time.get_ticks_usec() - t0) / 120.0
		print("config %s: %.2f ms/frame" % [str(configs[ci]), us / 1000.0])
		for k in configs[ci]:
			CVars.set_value(k, 0)
		ci += 1
		if ci >= configs.size():
			quit()
			return false
		for k in configs[ci]:
			CVars.set_value(k, 1)
		t0 = Time.get_ticks_usec()
	return false
