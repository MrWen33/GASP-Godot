import os
REPO = os.environ.get("GASP_REPO", ".")  # repo root; set GASP_REPO when running inside UE
"""Export SCS component templates of CBP_SandboxCharacter (relative transforms + key properties)."""
import unreal, json

OUT = REPO + r"/tools/ue/out/cbp_components.json"
res = {"components": [], "err": []}
bp = unreal.load_asset("/Game/Blueprints/CBP_SandboxCharacter")
sub = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
handles = sub.k2_gather_subobject_data_for_blueprint(bp)
lib = unreal.SubobjectDataBlueprintFunctionLibrary
PROPS = ["relative_location", "relative_rotation", "relative_scale3d", "target_arm_length", "socket_offset", "target_offset",
         "use_pawn_control_rotation", "do_collision_test", "enable_camera_lag", "camera_lag_speed", "field_of_view",
         "camera", "attach_socket_name", "capsule_radius", "capsule_half_height"]
for h in handles:
    try:
        data = lib.get_data(h)
        obj = lib.get_object(data)
        if obj is None:
            continue
        d = {"name": str(lib.get_variable_name(data)), "class": obj.get_class().get_name()}
        for p in PROPS:
            try:
                d[p] = str(obj.get_editor_property(p))
            except Exception:
                pass
        try:
            parent = lib.get_data(lib.get_parent_handle(data)) if hasattr(lib, "get_parent_handle") else None
        except Exception:
            parent = None
        res["components"].append(d)
    except Exception as e:
        res["err"].append(str(e))
open(OUT, "w").write(json.dumps(res, indent=1))
print("exported", len(res["components"]))
