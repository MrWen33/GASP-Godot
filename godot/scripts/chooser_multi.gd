class_name ChooserMulti
## UChooserTable::EvaluateChooser for the multi-result K2Node_EvaluateChooser2 (CHT_AnimationsForStateMachine), data from
## tools/build/build_chooser_sm.py. Columns filter in order (FBoolColumn / FFloatRangeColumn / FEnumColumn / FMultiEnumColumn /
## FRandomizeColumn); output columns (FOutputStructColumn) are written only for the first result: the row of the nested
## table that produced it, then the rows of its parents (UChooserFunctionLibrary::EvaluateChooserMulti callback).

const CONTINUE := 0
const CONTINUE_WITH_OUTPUTS := 1

var tables: Dictionary
var root: String
## S_ChooserOutputs after evaluation (StartTime, UseMM, MMCostLimit, BlendTime, BlendProfile, Tags)
var outputs := {}
var _results: PackedStringArray

func load_json(path: String) -> bool:
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null:
		return false
	var d = JSON.parse_string(f.get_as_text())
	tables = d["tables"]
	root = d["root"]
	return true

static func default_outputs() -> Dictionary:
	return {"StartTime": 0.0, "UseMM": false, "MMCostLimit": 0.0, "BlendTime": 0.3, "BlendProfile": "", "Tags": []}

## ctx: Dictionary of binding name -> value
func evaluate(ctx: Dictionary) -> PackedStringArray:
	_results = PackedStringArray()
	outputs = default_outputs()
	_eval_table(root, ctx)
	return _results

func _eval_table(name: String, ctx: Dictionary) -> int:
	var t: Dictionary = tables[name]
	var results: Array = t["results"]
	var disabled: Array = t["disabled"]
	var indices := []
	for i in results.size():
		if not (i < disabled.size() and disabled[i]):
			indices.append(i)
	for col in t["columns"]:
		if col["type"] == "OutputStructColumn":
			continue
		indices = _filter(col, indices, ctx)
	var set_outputs := false
	for i in indices:
		var r: Dictionary = results[i]
		var status := CONTINUE
		if r.has("nested"):
			status = _eval_table(r["nested"], ctx)
		elif r["asset"] != "":
			_results.append(r["asset"])
			status = CONTINUE_WITH_OUTPUTS if _results.size() == 1 else CONTINUE
		if status != CONTINUE:
			set_outputs = true
			for col in t["columns"]:
				if col["type"] == "OutputStructColumn":
					outputs = (col["rows"][i] as Dictionary).duplicate(true)
	# no FallbackResult in these tables
	return CONTINUE_WITH_OUTPUTS if set_outputs else CONTINUE

func _filter(col: Dictionary, indices: Array, ctx: Dictionary) -> Array:
	var rows: Array = col["rows"]
	var out := []
	match col["type"]:
		"RandomizeColumn":
			# no costs, no randomization context binding (last selected row unknown)
			if indices.size() <= 1:
				return indices
			var total := 0.0
			for i in indices:
				total += float(rows[i]) if i < rows.size() else 1.0
			var rnd := randf_range(0.0, total)
			var w := 0.0
			var sel := 0
			while sel < indices.size() - 1:
				var i: int = indices[sel]
				w += float(rows[i]) if i < rows.size() else 1.0
				if w > rnd:
					break
				sel += 1
			return [indices[sel]]
	var v = ctx[".".join(col["binding"])]  # e.g. "Velocity.Z", "TrajectoryCollision.TimeToLand"
	for i in indices:
		if i >= rows.size():
			continue
		var rv = rows[i]
		var ok := true
		match col["type"]:
			"BoolColumn":
				ok = rv == "MatchAny" or (rv == "MatchTrue") == bool(v)
			"FloatRangeColumn":
				var fv := float(v)
				ok = (rv["no_min"] or fv >= rv["min"]) and (rv["no_max"] or fv <= rv["max"])
			"EnumColumn":
				match rv["comparison"]:
					"MatchEqual": ok = int(v) == int(rv["value"])
					"MatchNotEqual": ok = int(v) != int(rv["value"])
					_: ok = true
			"MultiEnumColumn":
				ok = int(rv) == 0 or (int(rv) & (1 << int(v))) != 0
		if ok:
			out.append(i)
	return out
