class_name CVars
extends RefCounted
## Console variables used by GASP (DefaultEngine.ini CVarsArray + the engine cvars its widgets drive).
## exec() understands the commands issued by EUW_SimpleConsoleButton / EUW_SimpleConsoleSpinBox.

const DEFAULTS := {
	# project cvars (Config/DefaultEngine.ini)
	"ddcvar.traversal.drawdebuglevel": 0,
	"ddcvar.traversal.drawdebugduration": 1.0,
	"ddcvar.mmdatabaselod": 0,
	"ddcvar.drawvislogshapesforfoleysounds": 0,
	"ddcvar.attributebasedrootmotion.enable": 0,
	"ddcvar.offsetrootbone.translationradius": 30.0,
	"ddcvar.experimentalstatemachine.enable": 0,
	"ddcvar.experimentalstatemachine.debug": 0,
	"ddcvar.threadsafeanimationupdate.enable": 1,
	"ddcvar.newgameplaycamerasystem.enable": 1,
	# engine cvars
	"t.maxfps": 0.0,
	"slomo": 1.0,
	"au.lineargainscalarforfinaloutut": 1.0,
	"p.visualizemovement": 0,
	"a.animnode.posehistory.debugdrawtrajectory": 0,
	"a.animnode.motionmatching.debugdrawquery": 0,
	"a.animnode.offsetrootbone.enable": 1,
	"a.animnode.offsetrootbone.debug": 0,
	"a.animnode.orientationwarping.enable": 1,
	"a.animnode.orientationwarping.debug": 0,
	"a.animnode.footplacement.enable": 1,
	"a.animnode.footplacement.debug": 0,
	"gameplaycameras.debug.enable": 0,
	# show flags / stats (toggles)
	"stat fps": 0,
	"show collision": 0,
	"show bones": 0,
}

static var values: Dictionary = DEFAULTS.duplicate()
static var changed_serial := 0

static func get_f(name: String) -> float:
	return float(values.get(name.to_lower(), 0.0))

static func get_i(name: String) -> int:
	return int(values.get(name.to_lower(), 0))

static func get_b(name: String) -> bool:
	return get_f(name) != 0.0

static func has(name: String) -> bool:
	return values.has(name.to_lower())

static func set_value(name: String, v: float) -> void:
	var k := name.to_lower()
	values[k] = v
	changed_serial += 1
	_apply(k)

## "name value" sets, "name" alone toggles show flags / stats (and prints the value for regular cvars)
static func exec(cmd: String) -> void:
	var c := cmd.strip_edges()
	if c == "":
		return
	var lc := c.to_lower()
	if lc.begins_with("stat ") or lc.begins_with("show "):
		set_value(lc, 0.0 if get_b(lc) else 1.0)
		return
	var parts := c.split(" ", false)
	if parts.size() >= 2 and parts[1].is_valid_float():
		set_value(parts[0], parts[1].to_float())
	else:
		print("%s = %s" % [parts[0], str(values.get(parts[0].to_lower(), "<unknown>"))])

static func _apply(k: String) -> void:
	match k:
		"t.maxfps":
			Engine.max_fps = int(get_f(k))
		"slomo":
			Engine.time_scale = maxf(get_f(k), 0.0001)
		"au.lineargainscalarforfinaloutut":
			AudioServer.set_bus_volume_db(0, linear_to_db(maxf(get_f(k), 0.00001)))
