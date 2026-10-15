extends Node3D
## Sandbox level bootstrap: builds DefaultLevel, spawns the character at the PlayerStart and hooks the UI.

func _ready() -> void:
	var level = $Level
	level.environment = $WorldEnvironment.environment
	level.build()
	var ch = $Character
	ch.debug_label = $UI/Debug
	var ps: Dictionary = level.player_start
	ch.spawn_at_ue(ps["pos"] + Vector3(0, 0, 86.0 - 92.0), ps["yaw"])
	_start_ambient()
	var dd := DebugDraw.new()
	dd.name = "DebugDraw"
	add_child(dd)
	var ui = load("res://scripts/gasp_ui.gd").new()
	ui.name = "GaspUI"
	ui.character_path = NodePath("../Character")
	ui.camera_path = NodePath("../CameraRig")
	add_child(ui)
	level.button_pressed.connect(ui.on_level_button)

## GameAnimationWidget.SpawnCharacter: spawn the pawn class at the current pawn's transform, destroy the old pawn,
## possess the new one and restore the cached control rotation.
func spawn_character(variant: String) -> Node:
	var old = get_node_or_null("Character")
	if old == null:
		return null
	var ch := CharacterBody3D.new()
	ch.set_script(load("res://scripts/sandbox_character.gd"))
	ch.character_variant = variant
	ch.transform = Transform3D(Basis(Vector3.UP, -deg_to_rad(old.actor_yaw)), old.global_position)
	var cy: float = old.control_yaw
	var cp: float = old.control_pitch
	old.name = "Character_Old"
	remove_child(old)
	old.queue_free()
	ch.name = "Character"
	add_child(ch)
	move_child(ch, 3)
	ch.debug_label = $UI/Debug
	ch.control_yaw = cy
	ch.control_pitch = cp
	$CameraRig.target = ch
	var ui = get_node_or_null("GaspUI")
	if ui:
		ui.character = ch
	return ch

## AmbientSound (BackgroundMix SoundCue): Mixer(0.1, 0, 0.2, 0.4) over looping Modulator -> WavePlayer chains
func _start_ambient() -> void:
	FoleyAudio.setup_buses()
	var layers := [
		["res://assets/Audio/Ambient/Starter_Music01.wav", 0.1 * 0.5, 0.5],
		["res://assets/Audio/Ambient/Starter_Birds01.wav", 0.0, 1.0],
		["res://assets/Audio/Ambient/Starter_Wind05.wav", 0.2, 1.0],
		["res://assets/Audio/Ambient/Starter_Wind06.wav", 0.4, 1.0],
	]
	for l in layers:
		if l[1] <= 0.0:
			continue
		var p := AudioStreamPlayer.new()
		p.name = String(l[0]).get_file().get_basename()
		p.stream = load(l[0])
		p.volume_db = linear_to_db(l[1])
		p.pitch_scale = l[2]
		p.bus = "Ambient"
		p.finished.connect(p.play)
		add_child(p)
		p.play()
