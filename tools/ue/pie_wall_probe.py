"""PIE probe for GASP (read-only use of the project): run the sandbox character into a wall and log per-frame
database, location, velocity and component-space bone positions, to compare against the Godot port.

Launch: UnrealEditor.exe GameAnimationSample.uproject -ExecCmds="py <this file>" -unattended  (-ExecutePythonScript quits early)
Env: PIE_CASES="x,y,z,yaw,dirx,diry[,midx,midy];..." (UE units, capsule centre; with mid, IA_Jump is injected once when the
actor is within 170 cm (2D) of mid, like tools/test_traversal.gd), PIE_OUT=<log path>, PIE_SECONDS per case (default 4).
"""
import os
import unreal

OUT = os.environ.get("PIE_OUT", "C:/temp/pie_wall_probe.txt")
CASES = [[float(v) for v in c.split(",")] for c in os.environ.get("PIE_CASES", "3400,0,110,180,-1,0").split(";")]
SECONDS = float(os.environ.get("PIE_SECONDS", "4"))
BONES = ["pelvis", "spine_03", "head", "hand_l", "hand_r", "lowerarm_l", "lowerarm_r", "foot_l", "foot_r", "calf_l", "calf_r"]
SETTLE = 1.5  # seconds idle after placing before input starts

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
log = open(OUT, "w")
# the editor throttles to a few fps when its window is not in the foreground; the probe needs a steady 60 Hz
try:
    _perf = unreal.get_default_object(unreal.load_class(None, "/Script/UnrealEd.EditorPerformanceSettings"))
    _perf.set_editor_property("throttle_cpu_when_not_foreground", False)
except Exception as _e:
    unreal.log_warning("pie_wall_probe: could not disable background throttling: %s" % _e)
st = {"phase": "wait", "ticks": 0, "case": 0, "t": 0.0, "handle": None, "jumped": False, "jump_t": -1.0}
IA_JUMP = unreal.load_asset("/Game/Input/IA_Jump")
IA_SPRINT = unreal.load_asset("/Game/Input/IA_Sprint")


def out(s):
    log.write(s + "\n")
    log.flush()


def pawn_and_world():
    w = ues.get_game_world()
    if not w:
        return None, None
    return unreal.GameplayStatics.get_player_pawn(w, 0), w


def place(p, w):
    x, y, z, yaw = CASES[st["case"]][:4]
    st["jumped"] = False
    st["jump_t"] = -1.0
    p.set_actor_location_and_rotation(unreal.Vector(x, y, z), unreal.Rotator(0, 0, yaw), False, True)
    p.get_movement_component().set_editor_property("velocity", unreal.Vector(0, 0, 0))
    c = p.get_controller()
    if c:
        c.set_control_rotation(unreal.Rotator(0, -10, yaw))
    mesh = p.get_editor_property("mesh")
    # the pose must keep evaluating while the editor viewport is not rendered (otherwise bones freeze)
    mesh.set_editor_property("visibility_based_anim_tick_option", unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
    mesh.set_editor_property("enable_update_rate_optimizations", False)
    # like the Godot spawn (anim.reset): re-create the anim instance so OffsetRootBone etc. do not keep the old facing
    cls = mesh.get_anim_instance().get_class()
    mesh.set_anim_instance_class(None)
    mesh.set_anim_instance_class(cls)


def tick(dt):
    st["ticks"] += 1
    try:
        if st["phase"] == "wait":
            if st["ticks"] == 60:
                les.editor_request_begin_play()
                st["phase"] = "starting"
            return
        p, w = pawn_and_world()
        if st["phase"] == "starting":
            if p:
                unreal.SystemLibrary.execute_console_command(w, "t.MaxFPS 60")
                for cmd in [c for c in os.environ.get("PIE_CMDS", "").split(";") if c]:
                    unreal.SystemLibrary.execute_console_command(w, cmd)
                for spec in [s for s in os.environ.get("PIE_PROBE", "").split(";") if s]:
                    x0, x1, y = [float(v) for v in spec.split(",")]
                    x = x0
                    while x <= x1:
                        hit = unreal.SystemLibrary.line_trace_single(w, unreal.Vector(x, y, 400), unreal.Vector(x, y, -100),
                            unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [p], unreal.DrawDebugTrace.NONE, True)
                        out("PROBE x=%.0f y=%.0f %s" % (x, y, ("%.1f" % hit.to_tuple()[4].z) if hit and hit.to_tuple()[0] else "none"))
                        x += 2.0
                cap = p.get_editor_property("capsule_component")
                out("CAPSULE half=%.2f radius=%.2f" % (cap.get_unscaled_capsule_half_height(), cap.get_unscaled_capsule_radius()))
                place(p, w)
                st["phase"] = "run"
                st["t"] = 0.0
                out("CASE %d %s" % (st["case"], CASES[st["case"]]))
            return
        if st["phase"] != "run" or not p:
            return
        st["t"] += dt
        cs = CASES[st["case"]]
        dx, dy = cs[4], cs[5]
        if st["t"] > SETTLE:
            p.add_movement_input(unreal.Vector(dx, dy, 0), 1.0, True)
            # PIE_SPRINT / PIE_HOLD_JUMP: inject the actions every frame, like holding Shift / Space
            if os.environ.get("PIE_SPRINT") or os.environ.get("PIE_HOLD_JUMP"):
                for sub in unreal.ObjectIterator(unreal.EnhancedInputLocalPlayerSubsystem):
                    if os.environ.get("PIE_SPRINT"):
                        sub.inject_input_vector_for_action(IA_SPRINT, unreal.Vector(1, 0, 0), [], [])
                    if os.environ.get("PIE_HOLD_JUMP"):
                        sub.inject_input_vector_for_action(IA_JUMP, unreal.Vector(1, 0, 0), [], [])
        loc0 = p.get_actor_location()
        if len(cs) >= 8 and not st["jumped"] and st["t"] > SETTLE and ((loc0.x - cs[6]) ** 2 + (loc0.y - cs[7]) ** 2) ** 0.5 < 170.0:
            st["jumped"] = True
            # the only EnhancedInputLocalPlayerSubsystem instances belong to PIE local players
            for sub in unreal.ObjectIterator(unreal.EnhancedInputLocalPlayerSubsystem):
                sub.inject_input_vector_for_action(IA_JUMP, unreal.Vector(1, 0, 0), [], [])
            st["jump_t"] = st["t"]
            out("JUMP t=%.4f" % st["t"])
        mesh = p.get_editor_property("mesh")
        ai = mesh.get_anim_instance()
        db = ""
        try:
            d = ai.get_editor_property("CurrentSelectedDatabase")
            db = d.get_name() if d else ""
        except Exception as e:
            db = "?" + str(e)[:40]
        loc = p.get_actor_location()
        vel = p.get_velocity()
        mont = ai.get_current_active_montage()
        mtxt = "%s@%.3f" % (mont.get_name(), ai.montage_get_position(mont)) if mont else ""
        bones = []
        for b in BONES:
            tr = mesh.get_socket_transform(b, unreal.RelativeTransformSpace.RTS_COMPONENT)
            v = tr.translation
            bones.append("%.2f,%.2f,%.2f" % (v.x, v.y, v.z))
        extra = ""
        if os.environ.get("PIE_HANDS"):
            hw = [mesh.get_socket_location(b) for b in ("hand_l", "hand_r")]
            extra += " hands=%.1f,%.1f,%.1f;%.1f,%.1f,%.1f" % (hw[0].x, hw[0].y, hw[0].z, hw[1].x, hw[1].y, hw[1].z)
        if os.environ.get("PIE_FLAGS"):
            try:
                fv = ai.get_editor_property("Trj_FutureVelocity")
                extra += " fut=%.0f,%.0f,%.0f" % (fv.x, fv.y, fv.z)
            except Exception as e:
                extra += " fut=?" + str(e)[:60].replace(" ", "_")
        out("F t=%.4f dt=%.4f loc=%.1f,%.1f,%.1f vel=%.1f,%.1f,%.1f db=%s bones=%s mont=%s%s" % (
            st["t"], dt, loc.x, loc.y, loc.z, vel.x, vel.y, vel.z, db, ";".join(bones), mtxt, extra))
        if st["t"] > SETTLE + SECONDS:
            st["case"] += 1
            if st["case"] >= len(CASES):
                st["phase"] = "done"
                les.editor_request_end_play()
                out("DONE")
                log.close()
                unreal.unregister_slate_post_tick_callback(st["handle"])
                unreal.SystemLibrary.quit_editor()
                return
            place(p, w)
            st["t"] = 0.0
            out("CASE %d %s" % (st["case"], CASES[st["case"]]))
    except Exception as e:
        out("ERR %s" % e)


st["handle"] = unreal.register_slate_post_tick_callback(tick)
