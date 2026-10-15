class_name FoleyAudio
extends Node
## DefaultFoleyEventAudioBank + MSS_FoleySound presets + BP_AnimNotify_FoleyEvent.
## Each event picks a random wave (no immediate repeat), random volume in dB and random pitch, then plays on
## the Foley bus (SM_Foley: -10 dB). UE PlaySoundAtLocation without attenuation is not spatialized.

const POOL_SIZE := 12
const FOOTSTEP_EVENTS := ["Run", "Scuff", "Walk", "RunBackwds", "RunStrafe", "WalkBackwds", "ScuffPivot"]

var bank: Dictionary = {}
var streams: Dictionary = {}   # event -> Array[AudioStream]
var last_index: Dictionary = {}
var pool: Array[AudioStreamPlayer] = []
var next_player := 0
var rng := RandomNumberGenerator.new()
var played_log: Array = []     # [time, event, volume] for tests

static func setup_buses() -> void:
	if AudioServer.get_bus_index("Foley") >= 0:
		return
	for b in [["Foley", -10.0], ["Ambient", 0.0]]:
		AudioServer.add_bus()
		var i := AudioServer.bus_count - 1
		AudioServer.set_bus_name(i, b[0])
		AudioServer.set_bus_volume_db(i, b[1])
		AudioServer.set_bus_send(i, "Master")

func _ready() -> void:
	setup_buses()
	rng.randomize()
	var f := FileAccess.open("res://data/foley.json", FileAccess.READ)
	bank = JSON.parse_string(f.get_as_text())
	for i in POOL_SIZE:
		var p := AudioStreamPlayer.new()
		p.bus = "Foley"
		add_child(p)
		pool.append(p)

func _get_streams(event: String) -> Array:
	if not streams.has(event):
		var arr: Array = []
		var e: Dictionary = bank.get(event, {})
		for w in e.get("waves", []):
			var s = load(w)
			if s:
				arr.append(s)
		streams[event] = arr
	return streams[event]

## Notify class name -> Foley.Event.* leaf tag
static func event_from_notify_class(cls: String) -> String:
	var re := RegEx.create_from_string("^BP_AnimNotify_FoleyEvent_([A-Za-z]+?)(_[LR])?_C$")
	var m := re.search(cls)
	return m.get_string(1) if m else ""

func play(event: String, volume_multiplier := 1.0, pitch_multiplier := 1.0) -> void:
	var e: Dictionary = bank.get(event, {})
	var arr := _get_streams(event)
	if arr.is_empty():
		return
	var idx := rng.randi_range(0, arr.size() - 1)
	if arr.size() > 1 and idx == int(last_index.get(event, -1)):
		idx = (idx + 1 + rng.randi_range(0, arr.size() - 2)) % arr.size()
	last_index[event] = idx
	var gain: float = float(e.get("gain", 1.0)) * db_to_linear(rng.randf_range(float(e.get("volume_min", -1.0)), float(e.get("volume_max", 1.0))))
	var pitch: float = rng.randf_range(float(e.get("pitch_min", 0.95)), float(e.get("pitch_max", 1.05)))
	var p := pool[next_player]
	next_player = (next_player + 1) % pool.size()
	p.stream = arr[idx]
	p.volume_db = linear_to_db(maxf(gain * volume_multiplier, 1e-5))
	p.pitch_scale = maxf(pitch * pitch_multiplier, 0.01)
	p.play()
	played_log.append([Engine.get_physics_frames() / float(Engine.physics_ticks_per_second), event, volume_multiplier])
	if played_log.size() > 64:
		played_log.pop_front()

## BP_AnimNotify_FoleyEvent::Received_Notify -> GetAudioBank
func handle_notifies(notifies: Array, can_play_footsteps: bool) -> void:
	for n in notifies:
		var ev := event_from_notify_class(String(n["class"]))
		if ev == "" or ev == "Jump" or ev == "Land":
			continue  # Jump/Land notifies have no bank (CBP plays them from OnJumped/OnLanded)
		if ev in FOOTSTEP_EVENTS and not can_play_footsteps:
			continue
		play(ev)
