class_name ExperimentalStateMachine
extends RefCounted
## ABP_SandboxCharacter "State Controller" state machine + "State Machine Blend Stack" inputs (experimental state machine
## path, DDCVar.ExperimentalStateMachine.Enable / component tag "Force SM Setup"). States pick animations with
## SetBlendStackAnimFromChooser (CHT_AnimationsForStateMachine, optionally motion matched over the chosen assets).

## E_ExperimentalStateMachineState values (the enum's value order differs from its NewEnumeratorN names:
## NewEnumerator0 Idle Loop = 0, 3 Transition to Idle = 1, 1 Locomotion Loop = 2, 4 Transition to Locomotion = 3, 2 In Air Loop = 4, 5, 6)
enum SMState { IDLE_LOOP, TRANSITION_TO_IDLE_LOOP, LOCOMOTION_LOOP, TRANSITION_TO_LOCOMOTION_LOOP, IN_AIR_LOOP, TRANSITION_TO_IN_AIR_LOOP, IDLE_BREAK }
const SM_STATE_NAMES := ["Idle Loop", "Transition to Idle Loop", "Locomotion Loop", "Transition to Locomotion Loop", "In Air Loop", "Transition to In Air Loop", "Idle Break"]
## E_MovementDirection values (NewEnumerator2 = LR has value 3, NewEnumerator3 = LL has value 2)
enum Dir { F = 0, B = 1, LL = 2, LR = 3, RL = 4, RR = 5 }
const DIR_NAMES := {0: "F", 1: "B", 2: "LL", 3: "LR", 4: "RL", 5: "RR"}

## state graph nodes (AnimStateNode_*); RE_LOCO / RE_IDLE are the unnamed pass-through states, CONDUIT the In Air exit
enum St { IDLE_LOOP, TRANS_IDLE, TRANS_LOCO, LOCO_LOOP, IDLE_BREAK, TRANS_AIR, AIR_LOOP, RE_LOCO, RE_IDLE, CONDUIT }
const NODE_NAMES := ["Idle Loop", "Transition to Idle Loop", "Transition to Locomotion Loop", "Locomotion Loop", "Idle Break",
	"Transition to In Air Loop", "In Air Loop", "               ", "         ", "Conduit"]
const MAX_TRANSITIONS_PER_FRAME := 3

const CHOOSER_PATH := "res://data/chooser_sm.json"
const NOTIFIES_PATH := "res://data/sm_notifies.json"

var c # SandboxCharacter
var chooser := ChooserMulti.new()
var transitions := {} # Node -> [[priority, target, cond: Callable, notify name]] sorted by priority
var early_transitions := {} # anim name -> [{start, end, condition_gait_not_equal (-1 = always), destination}]
var branch_in := {} # anim name -> PoseSearchBranchIn database name

var node: int = St.TRANS_IDLE
var elapsed := 0.0
var initialized := false

# ABP variables
var StateMachineState: int = SMState.TRANSITION_TO_IDLE_LOOP
var BlendStackInputs := {"Anim": "", "Loop": false, "StartTime": 0.0, "BlendTime": 0.0, "BlendProfile": "", "Tags": []}
var PreviousBlendStackInputs := {}
var NoValidAnim := false
var NotifyTransition_ReTransition := false
var NotifyTransition_ToLoop := false
var ForceBlend := false
var SearchCost := 0.0
var MovementDirection: int = Dir.F
var MovementDirectionLastFrame: int = Dir.F
var MovementDirectionBias := 0 # never written in the ABP (CDO = NewEnumerator0)
var MovementDirectionThresholds := Vector4(-60, 60, -120, 120) # FL, FR, BL, BR
var TargetRotation := 0.0 # yaw (pitch / roll of CharacterTransform are zero)
var TargetRotationDelta := 0.0
var TargetRotationOnTransitionStart := 0.0
# blend stack state from the previous update (GetCurrentAssetTimeRemaining / IsCurrentAssetLooping)
var bs_anim := ""
var bs_time := 0.0
var bs_length := 0.0
var bs_looping := true

func _init(character) -> void:
	c = character
	chooser.load_json(CHOOSER_PATH)
	var nd = JSON.parse_string(FileAccess.get_file_as_string(NOTIFIES_PATH))
	for path in nd:
		var anim_name := String(path).get_file()
		for e in nd[path]:
			if e["class"] == "AnimNotifyState_PoseSearchBranchIn":
				branch_in[anim_name] = String(e["database"]).get_file().get_slice(".", 0)
			else:
				var cond := -1
				if String(e["TransitionCondition"]).contains("GAIT_NOT_EQUAL"):
					cond = ["WALK", "RUN", "SPRINT"].find(String(e["GaitNotEqual"]).get_slice(".", 1).get_slice(":", 0))
				var dest := 1 if String(e["TransitionDestination"]).contains("TRANSITION_TO_LOOP") else 0
				if not early_transitions.has(anim_name):
					early_transitions[anim_name] = []
				early_transitions[anim_name].append({"start": float(e["time"]), "end": float(e["time"]) + float(e["duration"]), "cond": cond, "dest": dest})
	_build_transitions()

# ------------------------------------------------------------------------------------------------
# graph (direct transitions first, then the alias transitions, stable by PriorityOrder)
func _add(from_nodes: Array, to: int, priority: int, cond: Callable, notify := "") -> void:
	for f in from_nodes:
		if not transitions.has(f):
			transitions[f] = []
		transitions[f].append([priority, to, cond, notify])

func _build_transitions() -> void:
	# direct
	_add([St.TRANS_LOCO], St.LOCO_LOOP, 2, func(): return _almost_complete() and elapsed > 0.0, "Transition: Animation Finished")    # T2
	_add([St.TRANS_LOCO], St.LOCO_LOOP, 2, func(): return NoValidAnim or BlendStackInputs["Loop"], "Transition: No Valid Anim")      # T3
	_add([St.TRANS_IDLE], St.IDLE_LOOP, 1, func(): return _almost_complete() and elapsed > 0.0, "Transition: Animation Finished")    # T5
	_add([St.IDLE_LOOP], St.IDLE_BREAK, 1, func(): return elapsed > 3.0, "Transition: Idle Break")                                   # T6
	_add([St.TRANS_AIR], St.AIR_LOOP, 1, func(): return _almost_complete() and elapsed > 0.0, "Transition: Animation Finished")      # T7
	_add([St.IDLE_BREAK], St.IDLE_LOOP, 1, func(): return _almost_complete() and elapsed > 0.0, "Transition: Animation Finished")    # T8
	_add([St.IDLE_BREAK], St.IDLE_LOOP, 1, func(): return NoValidAnim or BlendStackInputs["Loop"], "Transition: No Valid Anim")      # T9
	_add([St.TRANS_LOCO], St.LOCO_LOOP, 1, func(): return NotifyTransition_ToLoop)                                                  # T10
	_add([St.TRANS_IDLE], St.IDLE_LOOP, 1, func(): return NoValidAnim or BlendStackInputs["Loop"], "Transition: No Valid Anim")      # T11
	_add([St.TRANS_AIR], St.AIR_LOOP, 1, func(): return NoValidAnim or BlendStackInputs["Loop"], "Transition: No Valid Anim")        # T13
	_add([St.CONDUIT], St.TRANS_LOCO, 1, func(): return _is_moving(), "Transition: In Air to Locomotion")                           # T16
	_add([St.CONDUIT], St.TRANS_IDLE, 1, func(): return not _is_moving(), "Transition: In Air to Idle")                             # T17
	_add([St.RE_LOCO], St.TRANS_LOCO, 1, func(): return true)                                                                       # T18
	_add([St.RE_IDLE], St.TRANS_IDLE, 1, func(): return true)                                                                       # T25
	# aliases
	var idle_states := [St.IDLE_LOOP, St.TRANS_IDLE, St.IDLE_BREAK]
	var loco_states := [St.TRANS_LOCO, St.LOCO_LOOP]
	_add(idle_states, St.TRANS_LOCO, 1, func(): return _is_moving(), "Transition: Idle to Locomotion")                                 # T0 Idle -> Locomotion
	_add(loco_states, St.TRANS_IDLE, 1, func(): return not _is_moving(), "Transition: Locomotion To Idle")                            # T1 Locomotion -> Idle
	_add(idle_states, St.RE_IDLE, 1, func(): return c.Stance != c.stance_last and elapsed > 0.0, "Transition: State Changed")          # T4 Re-Transition to Idle Loop
	_add(idle_states, St.RE_IDLE, 1, func(): return c.ShouldTurnInPlace and (elapsed > 0.75 if _has_tag("TurnInPlace") else true), "Transition: Turn In Place") # T26
	_add(idle_states + loco_states, St.TRANS_AIR, 1, func(): return c.MovementMode == c.EMovementMode.IN_AIR, "Transition: Grounded to In Air") # T14
	_add([St.AIR_LOOP, St.TRANS_AIR], St.CONDUIT, 1, func(): return c.MovementMode == c.EMovementMode.ON_GROUND)                    # T15 In Air -> Grounded
	_add(loco_states, St.RE_LOCO, 1, func(): return (MovementDirection != MovementDirectionLastFrame or c.Stance != c.stance_last or c.Gait != c.gait_last) and elapsed > 0.0, "Transition: State Changed") # T19
	_add(loco_states, St.RE_LOCO, 1, func(): return c.IsPivoting and elapsed > (0.5 if (_has_tag("Pivot") or _has_tag("Start")) else 0.0), "Transition: Pivot") # T20
	_add(loco_states, St.RE_LOCO, 2, func(): return absf(UE.normalize_axis(TargetRotation - TargetRotationOnTransitionStart)) > 60.0 and elapsed < 0.5 and elapsed > 0.0 and (_has_tag("Pivot") or _has_tag("Start")), "Transition: \"Broke\" Rotation") # T21
	_add(loco_states, St.RE_LOCO, 1, func(): return NotifyTransition_ReTransition)                                                     # T23
	for k in transitions:
		var arr: Array = transitions[k]
		var sorted := []
		for p in [1, 2]:
			for t in arr:
				if t[0] == p:
					sorted.append(t)
		transitions[k] = sorted

func _has_tag(t: String) -> bool:
	return (BlendStackInputs["Tags"] as Array).has(t)

func _is_moving() -> bool:
	return c.MovementState == c.EMovementState.MOVING

## IsAnimationAlmostComplete: !IsCurrentAssetLooping && GetCurrentAssetTimeRemaining <= 0.75
func _almost_complete() -> bool:
	return not bs_looping and (bs_length - bs_time) <= 0.75

# ------------------------------------------------------------------------------------------------
## Update_MovementDirection + Update_TargetRotation (Update_Logic, before the anim graph update)
func update_logic() -> void:
	MovementDirectionLastFrame = MovementDirection
	if _is_moving():
		var direction := _calculate_direction(c.trj_future_velocity.normalized() if c.trj_future_velocity.length() > 0.0001 else Vector3.ZERO)
		MovementDirectionThresholds = _get_thresholds()
		var t := MovementDirectionThresholds
		if c.RotationMode == c.ERotationMode.ORIENT_TO_MOVEMENT or c.Gait == c.EGait.SPRINT:
			MovementDirection = Dir.F
		elif direction >= t.x and direction <= t.y:
			MovementDirection = Dir.F
		elif direction >= t.z and direction <= t.x:
			MovementDirection = Dir.LL if MovementDirectionBias == 0 else Dir.LR
		elif direction >= t.y and direction <= t.w:
			MovementDirection = Dir.RL if MovementDirectionBias == 0 else Dir.RR
		else:
			MovementDirection = Dir.B
	var char_yaw := UE.quat_yaw(c.character_xform_q)
	if _is_moving():
		TargetRotation = char_yaw if c.RotationMode == c.ERotationMode.ORIENT_TO_MOVEMENT else char_yaw + _strafe_yaw_rotation_offset()
	else:
		TargetRotation = char_yaw
	TargetRotationDelta = UE.normalize_axis(TargetRotation - UE.quat_yaw(c.root_xform_q))

## CalculateDirection(Normal(Trj_FutureVelocity, 0.0001), CharacterTransform)
func _calculate_direction(v: Vector3) -> float:
	return UE.calculate_direction(v, c.character_xform_q)

## Get_MovementDirectionThresholds
func _get_thresholds() -> Vector4:
	if MovementDirection == Dir.F or MovementDirection == Dir.B:
		return Vector4(-60, 60, -120, 120)
	if c.IsPivoting:
		return Vector4(-60, 60, -120, 120)
	if BlendStackInputs["Loop"] and not c.wants_to_aim:
		return Vector4(-60, 60, -140, 140)
	return Vector4(-40, 40, -140, 140)

## Get_StrafeYawRotationOffset: StrafeOffset_<dir> curve of StrafeOffsetCurveContainer at MapRangeClamped(dir, -180, 180, 0, 8) / 30
func _strafe_yaw_rotation_offset() -> float:
	var mapped := UE.map_range_clamped(_calculate_direction(c.trj_future_velocity.normalized() if c.trj_future_velocity.length() > 0.0001 else Vector3.ZERO), -180.0, 180.0, 0.0, 8.0) / 30.0
	return c.anim.get_anim_curve("StrafeOffsetCurveContainer", "StrafeOffset_" + DIR_NAMES[MovementDirection], mapped)

## IsPivoting with UseExperimentalStateMachine (Stance / Gait dependent angle and speed windows)
func is_pivoting() -> bool:
	var sp: float = c.Speed2D
	var thr := 65.0
	var in_range := sp >= 50.0 and sp <= 200.0
	if c.Stance == c.EStance.STAND:
		match c.Gait:
			c.EGait.WALK:
				thr = UE.map_range_clamped(sp, 150.0, 200.0, 70.0, 60.0)
				in_range = sp >= 50.0 and sp <= 200.0
			c.EGait.RUN:
				thr = UE.map_range_clamped(sp, 300.0, 500.0, 70.0, 60.0)
				in_range = sp >= 200.0 and sp <= 550.0
			_:
				thr = UE.map_range_clamped(sp, 300.0, 700.0, 60.0, 50.0)
				in_range = sp >= 200.0 and sp <= 700.0
	return absf(c._trajectory_turn_angle()) >= thr and in_range and _is_moving()

# ------------------------------------------------------------------------------------------------
## FAnimNode_StateMachine::Update: re-initialized when the TwoWayBlend switches to it (bReinitializeOnBecomingRelevant),
## first update skips transitions (bSkipFirstUpdateTransition), at most 3 transitions per frame, then the current state updates.
func update(dt: float) -> void:
	if not initialized:
		initialized = true
		node = St.TRANS_IDLE # entry
		elapsed = 0.0
		_on_entry(node)
		elapsed += dt
		_on_update(node, dt)
		return
	var count := 0
	while count < MAX_TRANSITIONS_PER_FRAME:
		var t = _find_transition(node)
		if t == null:
			break
		count += 1
		for n in t["notifies"]:
			_transition_notify(n)
		node = t["target"]
		elapsed = 0.0
		_on_entry(node)
	elapsed += dt
	_on_update(node, dt)

func reset() -> void:
	initialized = false

func _find_transition(from: int):
	for tr in transitions.get(from, []):
		if tr[2].call():
			var target: int = tr[1]
			var notifies := [tr[3]] if tr[3] != "" else []
			if target == St.CONDUIT:
				# a conduit is entered only when one of its exits is valid
				var exit = _find_transition(St.CONDUIT)
				if exit == null:
					continue
				return {"target": exit["target"], "notifies": notifies + exit["notifies"]}
			return {"target": target, "notifies": notifies}
	return null

func _on_entry(n: int) -> void:
	match n:
		St.IDLE_LOOP: set_blend_stack_anim_from_chooser(SMState.IDLE_LOOP, false)
		St.TRANS_IDLE: set_blend_stack_anim_from_chooser(SMState.TRANSITION_TO_IDLE_LOOP, true)
		St.TRANS_LOCO:
			TargetRotationOnTransitionStart = TargetRotation
			set_blend_stack_anim_from_chooser(SMState.TRANSITION_TO_LOCOMOTION_LOOP, true)
		St.LOCO_LOOP:
			TargetRotationOnTransitionStart = TargetRotation
			set_blend_stack_anim_from_chooser(SMState.LOCOMOTION_LOOP, false)
		St.IDLE_BREAK: set_blend_stack_anim_from_chooser(SMState.IDLE_BREAK, true)
		St.TRANS_AIR: set_blend_stack_anim_from_chooser(SMState.TRANSITION_TO_IN_AIR_LOOP, true)
		St.AIR_LOOP: set_blend_stack_anim_from_chooser(SMState.IN_AIR_LOOP, false)

func _on_update(n: int, dt: float) -> void:
	if n == St.TRANS_LOCO:
		# OnUpdate_TransitionToLocomotionLoop: RInterpTo(TargetRotationOnTransitionStart, TargetRotation, dt, 5)
		TargetRotationOnTransitionStart = UE.rinterp_to_yaw(TargetRotationOnTransitionStart, TargetRotation, dt, 5.0)

## AnimNotify_Transition: * events (TransitionStart notifies)
func _transition_notify(n: String) -> void:
	if CVars.get_b("DDCVar.ExperimentalStateMachine.Debug"):
		DebugDraw.print_string(n, Color(1.0, 0.166667, 0.0), 2.0)

# ------------------------------------------------------------------------------------------------
func chooser_context() -> Dictionary:
	var ao: Vector2 = c._ao_value()
	return {
		"Gait": c.Gait, "Stance": c.Stance, "Stance_LastFrame": c.stance_last, "StateMachineState": StateMachineState,
		"MovementDirection": MovementDirection, "MovementDirectionLastFrame": MovementDirectionLastFrame,
		"Speed2D": c.Speed2D, "Get_AO_Yaw": ao.x if c.RotationMode == c.ERotationMode.STRAFE else 0.0,
		"IsPivoting": c.IsPivoting, "ShouldTurnInPlace": c.ShouldTurnInPlace,
		"PlayLand": c.MovementMode == c.EMovementMode.ON_GROUND and c.movement_mode_last == c.EMovementMode.IN_AIR,
		"PlayMovingLand": c.MovementMode == c.EMovementMode.ON_GROUND and c.movement_mode_last == c.EMovementMode.IN_AIR and absf(c._trajectory_turn_angle()) <= 120.0,
		"JustLanded_Light": c.JustLanded_Light, "JustLanded_Heavy": c.JustLanded_Heavy, "JustTraversed": c.JustTraversed,
		"TargetRotationDelta": TargetRotationDelta, "TrajectoryCollision.TimeToLand": c.TrajectoryCollision["TimeToLand"],
		"Velocity.Z": c.abp_velocity.z,
	}

## SetBlendStackAnimFromChooser
func set_blend_stack_anim_from_chooser(state: int, force_blend: bool) -> void:
	StateMachineState = state
	PreviousBlendStackInputs = BlendStackInputs.duplicate(true)
	NoValidAnim = false
	NotifyTransition_ReTransition = false
	NotifyTransition_ToLoop = false
	var ctx := chooser_context()
	var anims := chooser.evaluate(ctx)
	var outputs := chooser.outputs
	if OS.get_environment("SM_TRACE") != "":
		print("  choose %s: %d anims %s outputs=%s ctx=%s" % [SM_STATE_NAMES[state], anims.size(), str(anims.slice(0, 4)), str(outputs), str(ctx)])
	if anims.is_empty():
		NoValidAnim = true
		return
	BlendStackInputs = {"Anim": anims[0], "Loop": c.anim.is_anim_looping(anims[0]), "StartTime": outputs["StartTime"],
		"BlendTime": outputs["BlendTime"], "BlendProfile": outputs["BlendProfile"], "Tags": outputs["Tags"]}
	if outputs["UseMM"]:
		var dbs := PackedStringArray()
		for a in anims:
			dbs.append(branch_in.get(a, ""))
		var r: Dictionary = c.anim.motion_match_anims(anims, dbs)
		if OS.get_environment("SM_TRACE") != "":
			print("  mm -> ", r)
		SearchCost = r.get("cost", 0.0)
		var limit: float = outputs["MMCostLimit"]
		if r.get("success", false) and (SearchCost <= limit if limit > 0.0 else true):
			BlendStackInputs["Anim"] = r["anim"]
			BlendStackInputs["Loop"] = c.anim.is_anim_looping(r["anim"])
			BlendStackInputs["StartTime"] = r["time"]
		else:
			NoValidAnim = true
			return
	if force_blend:
		ForceBlend = true

## graph inputs of the "State Machine Blend Stack"; ForceBlendNextUpdate is consumed by this update
func graph_inputs(inputs: Dictionary) -> void:
	inputs["use_sm"] = true
	inputs["sm_anim"] = BlendStackInputs["Anim"]
	inputs["sm_start_time"] = BlendStackInputs["StartTime"]
	inputs["sm_loop"] = BlendStackInputs["Loop"]
	inputs["sm_blend_time"] = BlendStackInputs["BlendTime"]
	inputs["sm_blend_profile"] = BlendStackInputs["BlendProfile"]
	inputs["sm_force_blend"] = ForceBlend
	inputs["sm_stops"] = _has_tag("Stop")
	inputs["speed_2d"] = c.Speed2D
	# Get_DesiredFacing: TargetRotation composed with yaw -90 (mesh space)
	inputs["desired_facing"] = UE.quat_from_yaw(TargetRotation - 90.0)
	ForceBlend = false

## after the anim graph update: blend stack state for next frame's transition rules and BP_NotifyState_EarlyTransition ticks
## (only the active, not blending out, player)
func post_update(state: Dictionary) -> void:
	bs_anim = state.get("sm_anim", "")
	bs_time = state.get("sm_time", 0.0)
	bs_length = state.get("sm_length", 0.0)
	bs_looping = state.get("sm_looping", true)
	for e in early_transitions.get(bs_anim, []):
		if bs_time >= e["start"] and bs_time <= e["end"]:
			if e["cond"] < 0 or c.Gait != e["cond"]:
				if e["dest"] == 0:
					NotifyTransition_ReTransition = true
				else:
					NotifyTransition_ToLoop = true

## Debug_ExperimentalStateMachine (BlueprintPostEvaluateAnimation, DDCVar.ExperimentalStateMachine.Debug)
func debug_draw(mesh_loc: Vector3, offset_root_yaw: float, max_speed: float) -> void:
	var char_loc: Vector3 = c._actor_location_ue()
	var mm: String = ["OnGround", "InAir"][c.MovementMode]
	var txt: String = "%s %s %s %s\r\n\r\nState: %s\r\nAnimation: %s\r\nLast MM Cost: %s" % [mm, ["Stand", "Crouch"][c.Stance], ["Walk", "Run", "Sprint"][c.Gait],
		DIR_NAMES[MovementDirection], SM_STATE_NAMES[StateMachineState], BlendStackInputs["Anim"], str(snappedf(SearchCost, 0.000001))]
	DebugDraw.text(char_loc, txt, Color.BLACK)
	var char_yaw := UE.quat_yaw(c.character_xform_q)
	var up1 := mesh_loc + Vector3(0, 0, 1)
	_arrow_circle(up1, UE.yaw_to_vector(char_yaw), 50.0, 50.0, Color.WHITE, true)
	# DrawDebugAngleThresholds: radial segments from 40 to 50 at FL, FR, BL, BR
	for a in [MovementDirectionThresholds.x, MovementDirectionThresholds.y, MovementDirectionThresholds.z, MovementDirectionThresholds.w]:
		var d := UE.yaw_to_vector(char_yaw + a)
		DebugDraw.line(up1 + d * 40.0, up1 + d * 50.0, Color.WHITE)
	var v: Vector3 = c.abp_velocity
	if v.length() > 0.0:
		DebugDraw.arrow(mesh_loc + Vector3(0, 0, 1.5), mesh_loc + Vector3(0, 0, 1.5) + v.normalized() * UE.map_range_clamped(v.length(), 0.0, max_speed, 0.0, 50.0), Color(1.0, 0.666667, 0.0), 5.0)
	var fv: Vector3 = c.trj_future_velocity
	if fv.length() > 0.0:
		DebugDraw.arrow(up1, up1 + fv.normalized() * UE.map_range_clamped(fv.length(), 0.0, max_speed, 0.0, 50.0), Color(0.7, 0.466667, 0.0), 5.0)
	_arrow_circle(mesh_loc + Vector3(0, 0, 1.5), UE.yaw_to_vector(TargetRotation), 0.0, 25.0, Color(0.0, 0.333333, 1.0), false)
	DebugDraw.text(mesh_loc + Vector3(0, 0, 1.5) + UE.yaw_to_vector(TargetRotation) * 25.0 + Vector3(0, 0, 5), "Target Rotation", Color(0.0, 0.333333, 1.0))
	_arrow_circle(mesh_loc + Vector3(0, 0, 2), UE.yaw_to_vector(offset_root_yaw + 90.0), 0.0, 25.0, Color(1, 0, 0), false)
	DebugDraw.text(mesh_loc + Vector3(0, 0, 2) + UE.yaw_to_vector(offset_root_yaw + 90.0) * 25.0 + Vector3(0, 0, 10), "Root Rotation", Color(1, 0, 0))

func _arrow_circle(center: Vector3, dir: Vector3, radius: float, length: float, color: Color, circle: bool) -> void:
	if circle and radius > 0.0:
		var prev := center + Vector3(radius, 0, 0)
		for i in range(1, 101):
			var t := TAU * i / 100.0
			var p := center + Vector3(cos(t), sin(t), 0) * radius
			DebugDraw.line(prev, p, color)
			prev = p
	DebugDraw.arrow(center, center + dir * length, color, 5.0)
