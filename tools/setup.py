#!/usr/bin/env python3
"""One-shot interactive installer: GASP (UE 5.5.4) project -> exported data -> Godot runtime data -> native GDExtension -> smoke test.

Run it with no arguments from a normal terminal (PowerShell / Windows Terminal / cmd):

    python tools/setup.py

It asks for the GASP .uproject, the UE 5.5 folder, the Godot 4.6.x executable and Visual Studio 2022 (auto-detected
defaults are offered, press Enter to accept), validates every answer and offers to remember them in tools/.setup.json
(git-ignored). The Python running this script is the one used for the build scripts (it needs numpy; the installer
offers to `pip install numpy`). Standard library only.

Resolution order per setting: command line > environment variable > saved answer > interactive prompt.
For scripts / AI agents use --non-interactive (missing or invalid settings then fail instead of prompting).

Every step is skipped when its outputs already exist (re-running is cheap and resumable). --force redoes everything,
--only a,b / --from NAME select steps, --list prints them. Logs: tools/ue/out/logs/<step>.log.
Environment variables: GASP_UPROJECT, UE_ROOT, GODOT, VSROOT.
"""
import argparse
import glob
import json
import os
import re
import shutil
import string
import subprocess
import sys
import time

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
OUT = os.path.join(REPO, "tools", "ue", "out")
DATA = os.path.join(REPO, "godot", "data")
LOG_DIR = os.path.join(OUT, "logs")
CONFIG = os.path.join(REPO, "tools", ".setup.json")
GODOT_CPP_COMMIT = "27d9dd23c83871e0619fca5dc2cddfbfd69e926a"

CHT = "{OUT}/t3d/Characters/UEFN_Mannequin/Animations/"

# --------------------------------------------------------------------------------------------------------------- steps
# kind: ue = UnrealEditor-Cmd (headless commandlet), editor = full UnrealEditor (FBX / textures / map load; the scripts
#       call quit_editor()), py = python script in tools/build, godot = Godot headless, fn = python function here.
# expect: files/dirs (relative to the repo) that exist once the step is done. always: run every time (success = exit 0).
STEPS = [
    dict(n="inventory", k="ue", s="inventory.py", e=["tools/ue/out/inventory.json"]),
    dict(n="export_data", k="ue", s="export_data.py", e=["tools/ue/out/t3d/Blueprints/ABP_SandboxCharacter.t3d"]),
    dict(n="export_meta", k="ue", s="export_meta.py", e=["tools/ue/out/anim_meta.json"]),
    dict(n="export_notifies", k="ue", s="export_notifies.py", e=["tools/ue/out/notifies.json"]),
    dict(n="export_posesearch", k="ue", s="export_posesearch.py", e=["tools/ue/out/posesearch.json"]),
    dict(n="bake_anims", k="ue", s="bake_anims.py", e=["tools/ue/out/anims/anims.bin", "tools/ue/out/anims/anims.json"]),
    dict(n="export_additive", k="ue", s="export_additive.py", e=["tools/ue/out/additive.json"]),
    dict(n="export_sm_notifies", k="ue", s="export_sm_notifies.py", e=["tools/ue/out/sm_notifies.json"]),
    dict(n="export_skel_t3d", k="ue", s="export_skel_t3d.py", e=["tools/ue/out/SK_UEFN_Mannequin.t3d"]),
    dict(n="export_uefn_mi", k="ue", s="export_uefn_mi.py", e=["tools/ue/out/uefn_materials.json"]),
    dict(n="export_foley_cdo", k="ue", s="export_foley_cdo.py", e=["godot/data/foley_notifies.json"]),
    dict(n="export_widget_cdo", k="ue", s="export_widget_cdo.py", e=["godot/data/widget_defaults.json"]),
    dict(n="export_widgets", k="ue", s="export_widgets.py", e=["tools/ue/out/widgets.json"]),
    dict(n="export_characters", k="ue", s="export_characters.py", e=["tools/ue/out/characters/characters.json"]),
    dict(n="export_retargeters", k="ue", s="export_retargeters.py", e=["tools/ue/out/characters/retargeters.json"]),
    dict(n="export_pp_abps", k="ue", s="export_pp_abps.py", e=["tools/ue/out/characters/t3d/ABP_Echo_PostProcess.t3d"]),
    dict(n="export_echo_rigs", k="ue", s="export_echo_rigs.py", e=["tools/ue/out/echo_rigs/ABP_Echo_Hair.t3d"]),
    dict(n="export_audio", k="editor", s="export_audio.py", e=["tools/ue/out/t3d_audio/export_log.json"]),
    dict(n="export_fbx", k="editor", s="export_fbx.py",
         e=["godot/assets/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin.fbx", "godot/assets/LevelPrototyping/Meshes/SM_Cube.fbx"]),
    dict(n="export_char_meshes", k="editor", s="export_char_meshes.py", e=["tools/ue/out/characters/meshes.json"]),
    dict(n="export_level", k="editor", s="export_level.py", e=["godot/data/level.json"]),
    dict(n="t3d_to_utf8", k="py", s="t3d_to_utf8.py",
         e=["tools/ue/out/t3d_utf8/Blueprints/ABP_SandboxCharacter.t3d", "tools/ue/out/characters/t3d/u/ABP_Echo_PostProcess.t3d"]),
    # done only once Godot has really imported the FBX (an empty godot/.godot folder from an earlier run must not count)
    dict(n="godot_import", k="godot", a=["--headless", "--path", "godot", "--import"],
         e=["godot/assets/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin.fbx.import"]),
    dict(n="dump_skeleton", k="godot", a=["--headless", "--path", "godot", "--script", "res://tools/dump_skeleton.gd"],
         e=["godot/data/godot_skeleton.json"], timeout=5),
    dict(n="build_anims", k="py", s="build_anims.py", e=["godot/data/anims/anims.bin", "godot/data/notifies.json"]),
    dict(n="build_mm", k="py", s="build_mm.py", e=["godot/data/mm/mm.json"]),
    dict(n="build_blend_profiles", k="py", s="build_blend_profiles.py", e=["godot/data/blend_profiles.json"]),
    dict(n="build_blendspaces", k="py", s="build_blendspaces.py", p=["{OUT}/t3d", "{DATA}/blendspaces.json"], e=["godot/data/blendspaces.json"]),
    dict(n="build_montages", k="py", s="build_montages.py", p=["{OUT}/t3d_utf8", "{DATA}/montages.json"], e=["godot/data/montages.json"]),
    dict(n="build_chooser_mm", k="py", s="build_chooser.py",
         p=[CHT + "MotionMatchingData/CHT_PoseSearchDatabases_Dense.t3d", "{DATA}/chooser_mm.json"], e=["godot/data/chooser_mm.json"]),
    dict(n="build_chooser_mm_sparse", k="py", s="build_chooser.py",
         p=[CHT + "MotionMatchingData/CHT_PoseSearchDatabases_Sparse.t3d", "{DATA}/chooser_mm_sparse.json"], e=["godot/data/chooser_mm_sparse.json"]),
    dict(n="build_chooser_traversal", k="py", s="build_chooser.py",
         p=[CHT + "Traversal/CHT_TraversalAnims.t3d", "{DATA}/chooser_traversal.json"], e=["godot/data/chooser_traversal.json"]),
    dict(n="build_chooser_sm", k="py", s="build_chooser_sm.py",
         p=[CHT + "ExperimentalStateMachineData/CHT_AnimationsForStateMachine.t3d", "{DATA}/chooser_sm.json"], e=["godot/data/chooser_sm.json"]),
    dict(n="build_foley", k="py", s="build_foley.py", e=["godot/data/foley.json"]),
    dict(n="build_widgets", k="py", s="build_widgets.py", e=["godot/data/widgets.json"]),
    dict(n="build_retarget", k="py", s="build_retarget.py", e=["godot/data/retarget/RTG_UEFN_to_Echo.json"]),
    dict(n="bp2text_echo", k="fn", e=["tools/ue/out/characters/ABP_Echo_PostProcess.txt", "tools/ue/out/characters/ABP_Echo_Hair.txt"]),
    dict(n="build_post_process", k="py", s="build_post_process.py", e=["godot/data/post_process/ABP_Echo_PostProcess.json"]),
    dict(n="build_notify_thresholds", k="py", s="build_notify_thresholds.py",
         p=["{CONTENT}", "{DATA}/notifies.json", "{DATA}/notify_thresholds.json"], e=["godot/data/notify_thresholds.json"]),
    dict(n="finalize_data", k="py", s="finalize_data.py",
         e=["godot/data/additive.json", "godot/data/sm_notifies.json", "godot/data/characters/meshes.json",
            "godot/data/characters/uefn_materials.json", "godot/data/characters/uefn_masked_regions.json"]),
    dict(n="godot_cpp", k="fn", e=["native/godot-cpp/SConstruct"], native=True),
    dict(n="native_build", k="native", e=["godot/bin/gasp_native.dll"], native=True),
    dict(n="godot_reimport", k="godot", a=["--headless", "--path", "godot", "--import"], always=True),
    dict(n="smoke_test", k="godot", a=["--headless", "--path", "godot", "--script", "res://tools/test_run.gd"], always=True),
]

# exporters open() their output without creating the folder
PRECREATE = ["godot/data/anims", "godot/data/mm", "godot/data/characters", "godot/data/retarget", "godot/data/post_process",
             "godot/assets", "tools/ue/out/anims", "tools/ue/out/characters/t3d", "tools/ue/out/echo_rigs", "tools/ue/out/t3d_audio"]


# ------------------------------------------------------------------------------------------------------------ helpers
def fail(msg):
    print("ERROR: " + msg, file=sys.stderr)
    sys.exit(1)


def clean_path(v):
    """Tolerate quoted / 'Copy as path' input: "C:\\a b\\c", 'C:\\a b\\c', smart quotes, stray spaces."""
    if v is None:
        return ""
    return v.strip().strip("\"'\u201c\u201d\u2018\u2019").strip()


def ue_exe(ue_dir, name):
    return os.path.join(ue_dir, "Engine", "Binaries", "Win64", name)


# ---- validators: return None when valid, else a message
def test_uproject(p):
    if not p:
        return "empty"
    if not os.path.isfile(p):
        return "file not found"
    if not p.lower().endswith(".uproject"):
        return "not a .uproject file"
    if not os.path.isdir(os.path.join(os.path.dirname(p), "Content")):
        return "no Content folder next to it"
    return None


def test_uedir(p):
    if not p:
        return "empty"
    if not os.path.isfile(ue_exe(p, "UnrealEditor-Cmd.exe")):
        return "Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe not found (pick the folder that contains 'Engine', e.g. ...\\Epic Games\\UE_5.5)"
    vf = os.path.join(p, "Engine", "Build", "Build.version")
    try:
        v = json.load(open(vf, encoding="utf-8"))
    except Exception:
        return "Engine\\Build\\Build.version not found / unreadable"
    if v.get("MajorVersion") != 5 or v.get("MinorVersion") != 5:
        return "UE %s.%s.%s found; this project is verified with UE 5.5.4 only" % (v.get("MajorVersion"), v.get("MinorVersion"), v.get("PatchVersion"))
    return None


def godot_version(p):
    """4.6.2.stable... from `--version` (console build), else guessed from the file name; None if unknown."""
    con = console_godot(p)
    try:
        r = subprocess.run([con, "--version"], capture_output=True, text=True, timeout=30)
        m = re.match(r"(\d+\.\d+(?:\.\d+)?)", r.stdout.strip())
        if m:
            return m.group(1)
    except Exception:
        pass
    m = re.search(r"(\d+\.\d+(?:\.\d+)?)[-_.]?stable", os.path.basename(p).lower())
    return m.group(1) if m else None


def console_godot(p):
    """The *_console.exe sibling gives reliable stdout/exit codes."""
    con = re.sub(r"\.exe$", "_console.exe", p, flags=re.I)
    return con if os.path.isfile(con) and not p.lower().endswith("_console.exe") else p


def test_godot(p):
    if not p:
        return "empty"
    if not os.path.isfile(p):
        return "file not found"
    v = godot_version(p)
    if v and not v.startswith("4.6"):
        return "Godot %s found; this project needs 4.6.x (verified with 4.6.2)" % v
    return None


def test_vs(p):
    if not p:
        return "empty"
    if not os.path.isfile(os.path.join(p, "VC", "Auxiliary", "Build", "vcvars64.bat")):
        return ("VC\\Auxiliary\\Build\\vcvars64.bat not found (pick the Visual Studio 2022 install folder that has the "
                "'Desktop development with C++' workload)")
    return None


# ---- auto-detection of defaults
def find_uproject():
    if os.environ.get("GASP_UPROJECT"):
        return os.environ["GASP_UPROJECT"]
    base = os.path.dirname(REPO)
    for root, dirs, files in os.walk(base):
        if root[len(base):].count(os.sep) >= 3:
            dirs[:] = []
            continue
        dirs[:] = [d for d in dirs if d not in (".git", "node_modules", "Intermediate", "DerivedDataCache", "Saved")]
        if "GameAnimationSample.uproject" in files:
            return os.path.join(root, "GameAnimationSample.uproject")
    return ""


def find_uedir():
    cands = [os.environ.get("UE_ROOT")]
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\EpicGames\Unreal Engine\5.5") as k:
            cands.append(winreg.QueryValueEx(k, "InstalledDirectory")[0])
    except Exception:
        pass
    for d in string.ascii_uppercase:
        cands += [d + ":\\Program Files\\Epic Games\\UE_5.5", d + ":\\Epic Games\\UE_5.5"]
    return next((c for c in cands if c and not test_uedir(c)), "")


def find_godot():
    cands = [os.environ.get("GODOT")]
    for d in os.environ.get("PATH", "").split(os.pathsep):
        cands += glob.glob(os.path.join(d, "godot*.exe"))
    return next((c for c in cands if c and os.path.isfile(c) and not test_godot(c)), "")


def find_vs():
    cands = [os.environ.get("VSROOT")]
    vw = os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"), "Microsoft Visual Studio", "Installer", "vswhere.exe")
    if os.path.isfile(vw):
        try:
            r = subprocess.run([vw, "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                                "-property", "installationPath"], capture_output=True, text=True, timeout=60)
            cands += [l.strip() for l in r.stdout.splitlines() if l.strip()]
        except Exception:
            pass
    return next((c for c in cands if c and not test_vs(c)), "")


# ---- settings resolution (parameter > env > saved > prompt)
class Settings:
    def __init__(self, args):
        self.non_interactive = args.non_interactive
        self.prompted = False
        self.prev = {}
        if os.path.isfile(CONFIG):
            try:
                self.prev = json.load(open(CONFIG, encoding="utf-8"))
            except Exception:
                pass
        self.saved = {} if args.reconfigure else self.prev

    def resolve(self, key, label, hint, cur, detect, test):
        cur = clean_path(cur)
        if cur:
            err = test(cur)
            if not err:
                return cur
            print("  %s: '%s' rejected - %s" % (label, cur, err))
        if self.non_interactive:
            fail("%s is missing or invalid (--non-interactive). %s" % (label, hint))
        self.prompted = True
        default = self.prev.get(key) or detect()
        print("\n" + label)
        print("  " + hint)
        while True:
            try:
                ans = input("  path [%s]: " % default if default else "  path: ")
            except EOFError:
                fail("No input available (stdin closed). Run it in a real terminal, or pass the settings with --non-interactive.")
            ans = clean_path(ans) or default
            err = test(ans)
            if not err:
                return ans
            print("  x " + err)


def ask_yes(prompt, default_yes=True):
    try:
        a = input(prompt + (" [Y/n] " if default_yes else " [y/N] ")).strip().lower()
    except EOFError:
        return False
    return (a != "n") if default_yes else (a == "y")


def check_numpy(non_interactive):
    def has():
        r = subprocess.run([sys.executable, "-c", "import numpy"], capture_output=True)
        return r.returncode == 0
    if sys.version_info < (3, 11):
        fail("Python 3.11+ required (running %s)" % sys.version.split()[0])
    if has():
        return
    if non_interactive or not ask_yes("numpy is missing for %s. Run 'pip install numpy' now?" % sys.executable):
        fail("numpy is required: %s -m pip install numpy" % sys.executable)
    subprocess.run([sys.executable, "-m", "pip", "install", "numpy"])
    if not has():
        fail("numpy still not importable")


# ------------------------------------------------------------------------------------------------------ step runners
def run_logged(cmd, log, timeout_min, cwd, env=None):
    """Runs cmd (list or string) with output redirected to `log`; kills the process tree after the timeout. Returns the exit code."""
    with open(log, "wb") as lf:
        p = subprocess.Popen(cmd, cwd=cwd, env=env, stdout=lf, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
        try:
            return p.wait(timeout=timeout_min * 60)
        except subprocess.TimeoutExpired:
            subprocess.run(["taskkill", "/F", "/T", "/PID", str(p.pid)], capture_output=True)
            p.wait()
            return 124


def expand(a, content):
    return a.replace("{OUT}", OUT).replace("{DATA}", DATA).replace("{CONTENT}", content).replace("\\", "/")


def is_done(step):
    if step.get("always"):
        return False
    return all(os.path.exists(os.path.join(REPO, e)) for e in step["e"])


def outputs_ok(step):
    return all(os.path.exists(os.path.join(REPO, e)) for e in step.get("e", []))


def step_bp2text(ctx, log):
    code = 0
    with open(log, "wb") as lf:
        for n in ("ABP_Echo_PostProcess", "ABP_Echo_Hair"):
            r = subprocess.run([sys.executable, os.path.join(REPO, "tools", "ue", "bp2text.py"),
                                os.path.join(OUT, "characters", "t3d", "u", n + ".t3d"), os.path.join(OUT, "characters", n + ".txt")],
                               stdout=lf, stderr=subprocess.STDOUT, env=ctx["env"])
            code = code or r.returncode
    return code


def step_godot_cpp(ctx, log):
    dst = os.path.join(REPO, "native", "godot-cpp")
    with open(log, "wb") as lf:
        if not os.path.isdir(dst):
            r = subprocess.run(["git", "clone", "--branch", "4.5", "https://github.com/godotengine/godot-cpp", dst],
                               stdout=lf, stderr=subprocess.STDOUT)
            if r.returncode:
                return r.returncode
        return subprocess.run(["git", "-C", dst, "checkout", GODOT_CPP_COMMIT], stdout=lf, stderr=subprocess.STDOUT).returncode


def run_step(step, ctx):
    log = os.path.join(LOG_DIR, step["n"] + ".log")
    k = step["k"]
    env = ctx["env"]
    if k == "ue" or k == "editor":
        exe = ctx["ue_cmd"] if k == "ue" else ctx["ue_editor"]
        script = os.path.join(REPO, "tools", "ue", step["s"])
        # one command line string, UE parses the quoted paths itself
        if k == "ue":
            line = '"%s" "%s" -run=pythonscript -script="%s" -EnablePlugins=PythonScriptPlugin -unattended -nop4 -nosplash -stdout' % (exe, ctx["uproject"], script)
        else:
            line = '"%s" "%s" -ExecutePythonScript="%s" -unattended -nop4 -nosplash -stdout' % (exe, ctx["uproject"], script)
        return run_logged(line, log, ctx["editor_timeout"], REPO, env)
    if k == "py":
        cmd = [sys.executable, step["s"]] + [expand(a, ctx["content"]) for a in step.get("p", [])]
        return run_logged(cmd, log, 60, os.path.join(REPO, "tools", "build"), env)
    if k == "godot":
        return run_logged([ctx["godot"]] + step["a"], log, step.get("timeout", 30), REPO, env)
    if k == "native":
        return run_logged(["cmd.exe", "/c", os.path.join(REPO, "native", "build.bat")], log, 60, REPO, env)
    if k == "fn":
        return {"bp2text_echo": step_bp2text, "godot_cpp": step_godot_cpp}[step["n"]](ctx, log)
    raise ValueError(k)


# ------------------------------------------------------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser(description="Interactive installer for the GASP -> Godot pipeline (see the module docstring).")
    ap.add_argument("--uproject", help="GameAnimationSample.uproject (UE 5.5 version of the Game Animation Sample)")
    ap.add_argument("--ue-dir", help="Unreal Engine 5.5 install folder (contains 'Engine')")
    ap.add_argument("--godot", help="Godot 4.6.x executable")
    ap.add_argument("--vs-root", help="Visual Studio 2022 install folder (C++ workload)")
    ap.add_argument("--only", default="", help="comma separated step names")
    ap.add_argument("--from", dest="from_step", default="", help="start at this step")
    ap.add_argument("--force", action="store_true", help="re-run steps even when their outputs exist")
    ap.add_argument("--skip-native", action="store_true", help="skip godot-cpp clone and C++ build")
    ap.add_argument("--list", action="store_true", help="list the steps and exit")
    ap.add_argument("--non-interactive", action="store_true", help="never prompt; fail on missing/invalid settings")
    ap.add_argument("--reconfigure", action="store_true", help="ignore saved answers and ask again")
    ap.add_argument("--editor-timeout-min", type=int, default=30)
    args = ap.parse_args()

    if args.list:
        for s in STEPS:
            print("%-26s %s" % (s["n"], s["k"]))
        return
    only = [x for x in args.only.split(",") if x]
    names = [s["n"] for s in STEPS]
    for x in only + ([args.from_step] if args.from_step else []):
        if x not in names:
            fail("unknown step '%s' (see --list)" % x)

    st = Settings(args)
    print("GASP -> Godot installer")
    if st.saved:
        print("Using saved answers from tools\\.setup.json (run with --reconfigure to change).")
    uproject = st.resolve("UProject", "1/4  GASP project file (GameAnimationSample.uproject)",
                          "The Game Animation Sample Project for UE 5.5 (Fab/Epic Launcher). Enter the full path of the .uproject.",
                          args.uproject or os.environ.get("GASP_UPROJECT") or st.saved.get("UProject"), find_uproject, test_uproject)
    ue_dir = st.resolve("UEDir", "2/4  Unreal Engine 5.5 install folder",
                        "Folder containing 'Engine', e.g. C:\\Program Files\\Epic Games\\UE_5.5  (must be 5.5.x; verified with 5.5.4).",
                        args.ue_dir or os.environ.get("UE_ROOT") or st.saved.get("UEDir"), find_uedir, test_uedir)
    godot = st.resolve("Godot", "3/4  Godot 4.6.x executable",
                       "e.g. D:\\godot\\Godot_v4.6.2-stable_win64.exe  (the standard build, not the .NET/mono one; verified with 4.6.2).",
                       args.godot or os.environ.get("GODOT") or st.saved.get("Godot"), find_godot, test_godot)
    vs_root = ""
    if not args.skip_native:
        vs_root = st.resolve("VsRoot", "4/4  Visual Studio 2022 install folder (C++ workload)",
                             "e.g. C:\\Program Files\\Microsoft Visual Studio\\2022\\Community  (needed to compile the native extension; "
                             "use --skip-native to skip).",
                             args.vs_root or os.environ.get("VSROOT") or st.saved.get("VsRoot"), find_vs, test_vs)
    check_numpy(args.non_interactive)

    if st.prompted and not args.non_interactive:
        print()
        if ask_yes("Save these answers to tools\\.setup.json for next time?"):
            json.dump({"UProject": uproject, "UEDir": ue_dir, "Godot": godot, "VsRoot": vs_root}, open(CONFIG, "w", encoding="utf-8"), indent=2)
            print("Saved.")

    uproject = os.path.abspath(uproject)
    env = dict(os.environ)
    env["GASP_REPO"] = REPO.replace("\\", "/")
    env["PYTHON"] = sys.executable
    if vs_root:
        env["VSROOT"] = vs_root
    env["PYTHONIOENCODING"] = "utf-8"
    ctx = dict(uproject=uproject, content=os.path.join(os.path.dirname(uproject), "Content"),
               ue_cmd=ue_exe(ue_dir, "UnrealEditor-Cmd.exe"), ue_editor=ue_exe(ue_dir, "UnrealEditor.exe"),
               godot=console_godot(godot), env=env, editor_timeout=args.editor_timeout_min)
    bv = json.load(open(os.path.join(ue_dir, "Engine", "Build", "Build.version"), encoding="utf-8"))
    print("\nUE %s.%s.%s | Godot %s | VS %s | Python %s" % (bv["MajorVersion"], bv["MinorVersion"], bv["PatchVersion"],
                                                          godot_version(godot) or "?", vs_root or "(skipped)", sys.version.split()[0]))

    for d in PRECREATE:
        os.makedirs(os.path.join(REPO, d), exist_ok=True)
    os.makedirs(LOG_DIR, exist_ok=True)

    started = not args.from_step
    for s in STEPS:
        if not started:
            if s["n"] == args.from_step:
                started = True
            else:
                continue
        if only and s["n"] not in only:
            continue
        if args.skip_native and s.get("native"):
            continue
        if not args.force and is_done(s):
            print("[skip] %s" % s["n"])
            continue
        print("[run ] %s ..." % s["n"], end="", flush=True)
        t0 = time.time()
        code = run_step(s, ctx)
        sec = int(time.time() - t0)
        # UE can crash while shutting down after the script finished; the expected outputs are the real success criterion
        ok = (code == 0) if s.get("always") else outputs_ok(s)
        if ok:
            print(" ok (%ds%s)" % (sec, ", exit %s ignored" % code if code else ""))
        else:
            print(" FAILED (exit %s, %ds) -> %s" % (code, sec, os.path.join(LOG_DIR, s["n"] + ".log")))
            print("Stopped at '%s'. Fix the cause (see its log), then re-run: finished steps are skipped." % s["n"])
            sys.exit(1)
    print('Done. Run the game: "%s" --path godot' % ctx["godot"])


if __name__ == "__main__":
    main()
