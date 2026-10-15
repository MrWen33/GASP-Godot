import os
REPO = os.environ.get("GASP_REPO", ".")  # repo root; set GASP_REPO when running inside UE
# Dumps DefaultLevel actors/components (world transforms, meshes, material params, splines, text),
# samples the landscape height on a grid and exports any static meshes the level uses.
# Run with the full editor:  UnrealEditor.exe <uproject> -ExecutePythonScript=<this> -unattended -nosplash
import unreal, json, os, math

OUT_JSON = REPO + r"/godot/data/level.json"
OUT_ASSETS = REPO + r"/godot/assets"
LOG = REPO + r"/tools/ue/out/export_level.log"
logf = open(LOG, "w")
def log(s):
    logf.write(str(s) + "\n"); logf.flush()

try:
    unreal.EditorLoadingAndSavingUtils.load_map("/Game/Levels/DefaultLevel")
    actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    actors = actor_sub.get_all_level_actors()
    log("actors %d" % len(actors))

    def xf(t):
        l = t.translation; r = t.rotation; s = t.scale3d
        return {"t": [l.x, l.y, l.z], "q": [r.x, r.y, r.z, r.w], "s": [s.x, s.y, s.z]}

    def mat_info(m):
        if m is None:
            return None
        info = {"path": m.get_path_name()}
        try:
            base = m
            while isinstance(base, unreal.MaterialInstance) and base.get_editor_property("parent") is not None:
                base = base.get_editor_property("parent")
            info["base"] = base.get_path_name()
        except Exception as e:
            pass
        if isinstance(m, unreal.MaterialInstance):
            vp = {}; sp = {}; tp = {}
            for v in m.get_editor_property("vector_parameter_values"):
                c = v.parameter_value; vp[str(v.parameter_info.name)] = [c.r, c.g, c.b, c.a]
            for v in m.get_editor_property("scalar_parameter_values"):
                sp[str(v.parameter_info.name)] = v.parameter_value
            for v in m.get_editor_property("texture_parameter_values"):
                tp[str(v.parameter_info.name)] = v.parameter_value.get_path_name() if v.parameter_value else None
            info["vectors"] = vp; info["scalars"] = sp; info["textures"] = tp
        return info

    used_meshes = set()
    out = {"actors": []}
    for a in actors:
        cls = a.get_class().get_name()
        ad = {"class": cls, "name": a.get_name(), "label": a.get_actor_label(), "xform": xf(a.get_actor_transform()), "components": []}
        try:
            props = {}
            for pn in ["ColorGroup", "AutoNameFromHeight", "DestinationName", "Destination", "ButtonText"]:
                try:
                    v = a.get_editor_property(pn)
                    props[pn] = str(v.get_name()) if hasattr(v, "get_name") else str(v)
                except Exception:
                    pass
            ad["props"] = props
        except Exception:
            pass
        for c in a.get_components_by_class(unreal.SceneComponent):
            cd = {"type": c.get_class().get_name(), "name": c.get_name(), "xform": xf(c.get_world_transform()),
                  "visible": bool(c.is_visible()) if hasattr(c, "is_visible") else True}
            if isinstance(c, unreal.StaticMeshComponent):
                sm = c.static_mesh
                cd["mesh"] = sm.get_path_name() if sm else None
                if sm:
                    used_meshes.add(sm.get_path_name().split(".")[0])
                cd["materials"] = [mat_info(c.get_material(i)) for i in range(c.get_num_materials())]
                cd["collision"] = str(c.get_collision_enabled())
                if isinstance(c, unreal.InstancedStaticMeshComponent):
                    inst = []
                    for i in range(c.get_instance_count()):
                        inst.append(xf(c.get_instance_transform(i, True)))
                    cd["instances"] = inst
            if isinstance(c, unreal.SplineComponent):
                pts = []
                n = c.get_number_of_spline_points()
                for i in range(n):
                    p = c.get_location_at_spline_point(i, unreal.SplineCoordinateSpace.WORLD)
                    pts.append([p.x, p.y, p.z])
                cd["points"] = pts
                cd["closed"] = bool(c.is_closed_loop())
            if isinstance(c, unreal.TextRenderComponent):
                cd["text"] = str(c.get_editor_property("text"))
                col = c.get_editor_property("text_render_color")
                cd["color"] = [col.r, col.g, col.b, col.a]
                cd["world_size"] = c.get_editor_property("world_size")
                cd["halign"] = str(c.get_editor_property("horizontal_alignment"))
                cd["valign"] = str(c.get_editor_property("vertical_alignment"))
            if isinstance(c, unreal.BoxComponent):
                e = c.get_editor_property("box_extent"); cd["extent"] = [e.x, e.y, e.z]
            if isinstance(c, unreal.CapsuleComponent):
                cd["radius"] = c.get_editor_property("capsule_radius"); cd["half_height"] = c.get_editor_property("capsule_half_height")
            if isinstance(c, unreal.LightComponent):
                col = c.get_editor_property("light_color")
                cd["light"] = {"intensity": c.get_editor_property("intensity"), "color": [col.r, col.g, col.b]}
            ad["components"].append(cd)
        # landscape bounds
        if cls in ("Landscape", "LandscapeProxy"):
            origin, extent = a.get_actor_bounds(False)
            ad["bounds"] = {"origin": [origin.x, origin.y, origin.z], "extent": [extent.x, extent.y, extent.z]}
            log("landscape bounds %s %s" % (origin, extent))
            # sample heights by line traces
            step = 100.0
            x0 = origin.x - extent.x; y0 = origin.y - extent.y
            nx = int(math.ceil(2 * extent.x / step)) + 1; ny = int(math.ceil(2 * extent.y / step)) + 1
            ztop = origin.z + extent.z + 1000.0; zbot = origin.z - extent.z - 1000.0
            heights = []
            for j in range(ny):
                row = []
                for i in range(nx):
                    x = x0 + i * step; y = y0 + j * step
                    hit = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(x, y, ztop), unreal.Vector(x, y, zbot),
                        unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [], unreal.DrawDebugTrace.NONE, True)
                    h = None
                    if hit is not None:
                        hr = hit.to_tuple() if hasattr(hit, "to_tuple") else None
                        try:
                            blocking = hr[0]; loc = hr[4]
                            hit_actor = hr[9] if len(hr) > 9 else None
                            if blocking:
                                h = loc.z
                        except Exception as e:
                            if j == 0 and i == 0:
                                log("trace parse " + str(e) + " " + str(hr))
                    row.append(h)
                heights.append(row)
            ad["heightfield"] = {"x0": x0, "y0": y0, "step": step, "nx": nx, "ny": ny, "heights": heights}
            try:
                mats = a.get_editor_property("landscape_material")
                ad["landscape_material"] = mat_info(mats)
            except Exception as e:
                log("landscape mat " + str(e))
        out["actors"].append(ad)
    out["used_meshes"] = sorted(used_meshes)
    json.dump(out, open(OUT_JSON, "w"), indent=1)
    log("wrote json, meshes: %s" % sorted(used_meshes))

    # export used static meshes
    for p in sorted(used_meshes):
        fn = OUT_ASSETS + p.replace("/Game", "") + ".fbx"
        if os.path.exists(fn) or not p.startswith("/Game"):
            log("skip mesh %s" % p); continue
        os.makedirs(os.path.dirname(fn), exist_ok=True)
        t = unreal.AssetExportTask()
        t.object = unreal.load_asset(p); t.filename = fn; t.automated = True; t.prompt = False; t.replace_identical = True
        t.exporter = unreal.StaticMeshExporterFBX()
        o = unreal.FbxExportOption(); o.collision = False; o.level_of_detail = False; o.vertex_color = True
        t.options = o
        log("export mesh %s %s" % (p, unreal.Exporter.run_asset_export_task(t)))
except Exception as e:
    import traceback
    log("ERROR " + traceback.format_exc())
logf.close()
unreal.SystemLibrary.quit_editor()
