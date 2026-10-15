import os
REPO = os.environ.get("GASP_REPO", ".")  # repo root; set GASP_REPO when running inside UE
"""SCS components of CBP_SandboxCharacter_SetupComparison (+ inherited Mesh overrides): mesh, anim class, tags, visibility, transforms."""
import unreal, json
OUT = REPO + r"/tools/ue/out/comparison_components.json"
res = {"components": [], "err": []}
bp = unreal.load_asset("/Game/Blueprints/RetargetedCharacters/CBP_SandboxCharacter_SetupComparison")
sub = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
lib = unreal.SubobjectDataBlueprintFunctionLibrary
PROPS = ["relative_location", "relative_rotation", "relative_scale3d", "component_tags", "visible", "hidden_in_game", "anim_class",
         "skeletal_mesh_asset", "attach_socket_name", "animation_mode", "override_materials"]
for h in sub.k2_gather_subobject_data_for_blueprint(bp):
    try:
        data = lib.get_data(h)
        obj = lib.get_object(data)
        if obj is None:
            continue
        d = {"name": str(lib.get_variable_name(data)), "class": obj.get_class().get_name(), "inherited": bool(lib.is_inherited_component(data))}
        try:
            ph = lib.get_data(data).get_editor_property("parent_object_handle") if False else None
        except Exception:
            pass
        for p in PROPS:
            try:
                v = obj.get_editor_property(p)
                d[p] = v.get_path_name() if hasattr(v, "get_path_name") else str(v)
            except Exception:
                pass
        res["components"].append(d)
    except Exception as e:
        res["err"].append(str(e))
open(OUT, "w").write(json.dumps(res, indent=1))
