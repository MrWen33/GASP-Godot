import os
REPO = os.environ.get("GASP_REPO", ".")  # repo root; set GASP_REPO when running inside UE
"""Numeric value order of the project's user defined enums: value -> (name, display name)."""
import unreal, json
OUT = REPO + r"/tools/ue/out/enum_values.json"
res = {}
K = unreal.KismetNodeHelperLibrary
for name in ["E_ExperimentalStateMachineState", "E_MovementDirection", "E_Gait", "E_Stance", "E_MovementMode", "E_MovementState", "E_RotationMode"]:
    e = unreal.load_asset("/Game/Blueprints/Data/" + name)
    items = []
    for v in range(8):
        try:
            items.append({"value": v, "name": str(K.get_enumerator_name(e, v)), "display": str(K.get_enumerator_user_friendly_name(e, v))})
        except Exception as ex:
            items.append({"value": v, "err": str(ex)})
    res[name] = items
json.dump(res, open(OUT, "w"), indent=1)
