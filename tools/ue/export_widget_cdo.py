import os
REPO = os.environ.get("GASP_REPO", ".")  # repo root; set GASP_REPO when running inside UE
"""Export class-default values of GASP's helper user widgets (variables set per instance in the panels)."""
import unreal, json

OUT = REPO + r"/godot/data/widget_defaults.json"
VARS = {
    "WB_SimpleTextBlock": ["BoxSize", "BoxColor", "Text", "TextAlignment", "TextPadding", "IsBold"],
    "EUW_SimpleConsoleButton": ["ButtonName", "Console Command", "Button Width", "Invert Color", "Command Has Int State", "IntValue"],
    "EUW_SimpleConsoleSpinBox": ["Console Command", "Enable Slider", "Use Min", "Min Value", "Use Max", "Max Value", "SliderSnapDelta", "Box Width", "UpdateValueToMatchConsoleVariable"],
}
res = {}
for w, vars_ in VARS.items():
    d = {}
    try:
        cls = unreal.load_object(None, "/Game/Widgets/WidgetData/%s.%s_C" % (w, w))
        cdo = unreal.get_default_object(cls)
        for v in vars_:
            for name in (v, v.replace(" ", "_"), v.lower().replace(" ", "_")):
                try:
                    d[v] = str(cdo.get_editor_property(name))
                    break
                except Exception as e:
                    d[v] = "ERR " + str(e)[:80]
    except Exception as e:
        d["err"] = str(e)
    res[w] = d
open(OUT, "w").write(json.dumps(res, indent=1))
print("widget cdo done")
