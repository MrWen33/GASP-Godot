# GASP → Godot 4.6

把 Epic 的 **Game Animation Sample（GASP）** 在 Godot 4.6 中按原样复刻：Motion Matching（PoseSearch）、BlendStack、OrientationWarping/Steering、Traversal（翻越/攀爬，含 Motion Warping）、FootPlacement + LegIK、IK Retargeter（多角色）、相机、Foley 声音、UI/CVar/调试绘制。算法均照 UE 5.5 源码移植为 C++ GDExtension + GDScript。

> **本仓库不含任何 Epic 内容。** 动画、网格、贴图、声音、关卡等都要由你本机的 GASP 工程导出生成（见下），生成物（`godot/assets/`、`godot/data/`、`tools/ue/out/`）已被 `.gitignore` 忽略，请勿提交或再分发。GASP 内容受 Epic 的许可条款约束。
> 范围：只做 Motion Matching 与动作动画，MetaHuman 不在范围内。

## 验证过的版本（请严格对齐）

| 组件 | 版本 | 说明 |
|---|---|---|
| 操作系统 | Windows 11 | 安装脚本为 Python，原生构建为批处理；UE 导出与 MSVC 编译都在 Windows 上验证 |
| Unreal Engine | **5.5.4**（`++UE5+Release-5.5`，CL 40574608） | Epic Games Launcher 安装版。`setup.py` 会读 `Engine/Build/Build.version`，非 5.5.x 直接拒绝 |
| GASP | **Game Animation Sample Project，UE 5.5 版**（Fab/Epic 免费示例，工程名 `GameAnimationSample`） | 该示例无独立版本号；识别方式：`.uproject` 的 `"EngineAssociation": "5.5"`、`"EpicSampleNameHash": "1673045636"`，SHA-256 `69e07ca9bb4644722afac9bf39aba4bcb8442ee855fbc405df9d888ab50e3af1`。含 `Content/MetaHumans`（不使用） |
| Godot | **4.6.2-stable**（official win64） | 工程特性 `4.6`、Forward Plus、Jolt Physics |
| godot-cpp | 分支 **4.5**，提交 `27d9dd23c83871e0619fca5dc2cddfbfd69e926a` | 由 `setup.py` 自动克隆并检出；已验证与 Godot 4.6.2 兼容 |
| C++ 工具链 | Visual Studio 2022 Community（MSVC v143，自带 CMake + Ninja） | `native/build.bat` 使用 |
| Python | 3.14.3 + numpy 2.4.2 | 运行 `tools/setup.py` 与 `tools/build`（Python 3.11+ 均可，numpy 必需）（UE 内置 Python 3.11 另行运行导出脚本） |

磁盘与耗时（参考）：`godot/assets` 约 3.4 GB；UE 导出合计约 10 分钟（无头约 6 分钟 + 完整编辑器的四步约 4 分钟），`godot --import` 约 80 秒，`build_mm.py` 约 2.5 分钟，原生编译（含克隆 godot-cpp）约 80 秒。

## 一键安装

在**普通终端**（PowerShell / Windows Terminal / cmd，不是 Claude Code 的 `!` 命令）里：

```
git clone <本仓库> gasp-godot
cd gasp-godot
python tools/setup.py
```

只需 Python 3.11+（标准库即可运行脚本；构建步骤需要 numpy，缺时脚本会询问是否 `pip install numpy`）。不带参数运行，脚本会依次询问 4 个位置（方括号里是自动探测到的默认值，直接回车即采用；输入错误会提示原因并重问；路径可带引号，资源管理器里“复制为路径”的结果可直接粘贴）：

1. GASP 工程文件 `GameAnimationSample.uproject`（自动在仓库上级目录附近查找）
2. Unreal Engine 5.5 安装目录（含 `Engine` 的那一层，如 `C:\Program Files\Epic Games\UE_5.5`；从注册表和各盘常见位置探测）
3. Godot 4.6.x 可执行文件（标准版，非 .NET；从 PATH 探测）
4. Visual Studio 2022 安装目录（用 `vswhere` 只匹配装有“使用 C++ 的桌面开发”工作负载的实例；`--skip-native` 时不问）

回答完会询问是否保存到 `tools/.setup.json`（已被 git 忽略）；之后再运行直接复用，`--reconfigure` 重新询问。每项的取值优先级：命令行参数 > 环境变量（`GASP_UPROJECT`、`UE_ROOT`、`GODOT`、`VSROOT`）> 已保存的回答 > 交互提问。运行脚本的这个 Python 就是构建步骤用的 Python。

脚本行为：

- 先逐项校验设置（UE 版本 = 5.5.x、Godot 4.6、VS 含 vcvars64、Python+numpy），再按固定顺序执行全部 40 余个步骤（`python tools/setup.py --list` 列出）。
- **可续跑**：每步若其产物已存在就跳过；失败即停止并指出日志 `tools/ue/out/logs/<步骤>.log`，修复后原命令重跑即可。`--force` 全部重做；`--only a,b` 只做指定步骤；`--from <步骤>` 从某步起；`--skip-native` 跳过 godot-cpp 与 C++ 编译。
- UE 完整编辑器步骤（`export_audio`/`export_fbx`/`export_char_meshes`/`export_level`）结束时 UE 可能以崩溃码（如 `-1073740791`）退出，属于 UE 关闭阶段的已知现象；脚本以**产物是否生成**判定成功。
- 最后一步 `smoke_test` 无头运行 `godot/tools/test_run.gd`，退出码 0 即整套可用。

完成后运行：

```powershell
& "D:\godot\Godot_v4.6.2-stable_win64.exe" --path godot
```

操作：WASD 移动、鼠标看、Shift 冲刺、Ctrl 切换走/跑、C 蹲、Space 跳/翻越（按住连续翻越）、Esc 释放/捕获鼠标；场景里的按钮可切换角色与 MM/状态机。

## 管线概览

```
GASP .uproject ──(UE 5.5 Python: tools/ue/*.py)──► tools/ue/out/**  +  godot/assets/**
                                   │ T3D(UTF-16)→UTF-8
Godot --import ─► dump_skeleton.gd ─► godot/data/godot_skeleton.json
tools/ue/out/** ──(tools/build/*.py)──► godot/data/**   (anims.bin、MM 特征库、Chooser、蒙太奇…)
native/ (C++ GDExtension, godot-cpp 4.5) ──► godot/bin/gasp_native.dll
```

## 目录

```
godot/     Godot 工程（scripts/ 运行时、tools/ 测试与诊断、shaders/、bin/ 扩展输出）
native/    C++ GDExtension（动画图、MM、BlendStack、Traversal 蒙太奇、FootPlacement/LegIK、重定向、PBIK…）
tools/ue/      UE 内运行的导出脚本        tools/build/   导出物 → Godot 数据
tools/static/  无法导出的静态数据         tools/setup.py   一键脚本
```

## 许可

本仓库原创代码以 [MIT License](LICENSE) 发布。与 Epic Games 的关系、移植自 Unreal Engine 源码部分的说明及第三方组件见 [NOTICE.md](NOTICE.md)。
