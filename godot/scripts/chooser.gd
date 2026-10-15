class_name ChooserTable
## Evaluates a Chooser table exported by tools/build/build_chooser.py (multi-result mode:
## every matching row contributes; nested choosers are evaluated recursively).

var tables: Dictionary
var root: String

func load_json(path: String) -> bool:
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null:
		return false
	var d = JSON.parse_string(f.get_as_text())
	tables = d["tables"]
	root = d["root"]
	return true

var outputs := {}
var _guid_re := RegEx.create_from_string("_[0-9]+_[0-9A-F]{32}$")

## ctx: Object (properties) or Dictionary. Output columns are written to `outputs`.
func evaluate(ctx) -> PackedStringArray:
	var out := PackedStringArray()
	outputs = {}
	_eval_table(root, ctx, out)
	return out

func _clean(n: String) -> String:
	return _guid_re.sub(n, "")

func _get_value(ctx, chain: Array):
	var v = ctx[_clean(chain[0])] if ctx is Dictionary else ctx.get(chain[0])
	for i in range(1, chain.size()):
		v = v[chain[i]]
	return v

func _eval_table(name: String, ctx, out: PackedStringArray) -> void:
	var t: Dictionary = tables[name]
	var results: Array = t["results"]
	var disabled: Array = t.get("disabled", [])
	for row in results.size():
		if row < disabled.size() and disabled[row]:
			continue
		var ok := true
		for col in t["columns"]:
			if col["type"] == "OutputEnumColumn":
				continue
			var rows: Array = col["rows"]
			if row >= rows.size():
				continue
			var rv = rows[row]
			var v = _get_value(ctx, col["binding"])
			match col["type"]:
				"BoolColumn":
					if rv == "MatchTrue" and not v: ok = false
					elif rv == "MatchFalse" and v: ok = false
				"FloatRangeColumn":
					var fv := float(v)
					if not ((rv["no_min"] or fv >= rv["min"]) and (rv["no_max"] or fv <= rv["max"])):
						ok = false
				"EnumColumn":
					if rv["comparison"] == "MatchEqual" and int(v) != int(rv["value"]): ok = false
					elif rv["comparison"] == "MatchNotEqual" and int(v) == int(rv["value"]): ok = false
				"MultiEnumColumn":
					if (int(rv) & (1 << int(v))) == 0: ok = false
			if not ok:
				break
		if not ok:
			continue
		for col in t["columns"]:
			if col["type"] == "OutputEnumColumn" and row < col["rows"].size():
				outputs[_clean(col["binding"][0])] = int(col["rows"][row])
		var r: Dictionary = results[row]
		if r.has("nested"):
			_eval_table(r["nested"], ctx, out)
		elif r.has("asset"):
			if not out.has(r["asset"]):
				out.append(r["asset"])
