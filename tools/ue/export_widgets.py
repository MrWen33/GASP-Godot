import os
REPO = os.environ.get("GASP_REPO", ".")  # repo root; set GASP_REPO when running inside UE
"""Export GASP editor utility widgets (Controls, GameAnimationWidget, ReadMe + WidgetData) to T3D, widget images to PNG,
and dump each widget tree (class, name, text, image, console command, slot) to JSON."""
import unreal, os, json

OUT_T3D = REPO + r"/tools/ue/out/t3d_widgets"
OUT_IMG = REPO + r"/godot/assets/Widgets/Images"
OUT_JSON = REPO + r"/tools/ue/out/widgets.json"
os.makedirs(OUT_T3D, exist_ok=True)
os.makedirs(OUT_IMG, exist_ok=True)
reg = unreal.AssetRegistryHelpers.get_asset_registry()
res = {"exported": [], "err": [], "trees": {}}

def export(obj, path):
    t = unreal.AssetExportTask()
    t.object = obj
    t.filename = path
    t.automated = True
    t.prompt = False
    t.replace_identical = True
    return unreal.Exporter.run_asset_export_task(t)

for ad in reg.get_assets_by_path("/Game/Widgets", recursive=True):
    cls = str(ad.asset_class_path.asset_name)
    pkg = str(ad.package_name)
    name = str(ad.asset_name)
    try:
        obj = ad.get_asset()
        if cls == "Texture2D":
            ok = export(obj, os.path.join(OUT_IMG, name + ".png"))
        else:
            ok = export(obj, os.path.join(OUT_T3D, name + ".t3d"))
        res["exported"].append([pkg, cls, ok])
    except Exception as e:
        res["err"].append([pkg, str(e)])

PROPS = ["text", "brush", "console_command", "button_text", "tool_tip_text", "visibility", "font", "justification",
         "auto_wrap_text", "color_and_opacity", "padding", "min_value", "max_value", "value", "delta", "cvar",
         "character", "character_class", "widget_to_open"]
for ad in reg.get_assets_by_path("/Game/Widgets", recursive=True):
    cls = str(ad.asset_class_path.asset_name)
    if cls not in ("EditorUtilityWidgetBlueprint", "WidgetBlueprint"):
        continue
    name = str(ad.asset_name)
    try:
        bp = ad.get_asset()
        gen = unreal.load_object(None, str(ad.package_name) + "." + name + "_C")
        cdo = unreal.get_default_object(gen)
        tree = []
        try:
            wt = bp.get_editor_property("widget_tree")
        except Exception as e:
            wt = None
            res["err"].append([name, "widget_tree: " + str(e)])
        widgets = []
        if wt is not None:
            try:
                widgets = unreal.WidgetTree.get_all_widgets(wt) if hasattr(unreal.WidgetTree, "get_all_widgets") else []
            except Exception as e:
                res["err"].append([name, "get_all_widgets: " + str(e)])
        for w in widgets:
            d = {"name": w.get_name(), "class": w.get_class().get_name()}
            try:
                p = w.get_parent()
                d["parent"] = p.get_name() if p else ""
            except Exception:
                pass
            for pr in PROPS:
                try:
                    d[pr] = str(w.get_editor_property(pr))
                except Exception:
                    pass
            tree.append(d)
        res["trees"][name] = tree
    except Exception as e:
        res["err"].append([name, str(e)])
open(OUT_JSON, "w").write(json.dumps(res, indent=1))
print("widgets export done", len(res["exported"]), len(res["err"]))
