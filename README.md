<h1 align="center">GASP → Godot</h1>

<p align="center">
  把 Epic 的 <b>Game Animation Sample（GASP）</b> 按原样复刻到 <b>Godot 4.6</b><br>
  Motion Matching · BlendStack · Traversal · FootPlacement · IK Retargeter
</p>

<p align="center">
  <a href="https://godotengine.org/download/windows/"><img alt="Godot 4.6" src="https://img.shields.io/badge/Godot-4.6-478cbf?logo=godotengine&logoColor=white"></a>
  <a href="https://www.unrealengine.com/"><img alt="UE 5.5" src="https://img.shields.io/badge/Unreal_Engine-5.5-313131?logo=unrealengine&logoColor=white"></a>
  <img alt="Platform" src="https://img.shields.io/badge/platform-Windows-0078d4">
  <img alt="C++ GDExtension" src="https://img.shields.io/badge/C%2B%2B-GDExtension-00599c?logo=cplusplus&logoColor=white">
  <a href="LICENSE"><img alt="MIT License" src="https://img.shields.io/badge/license-MIT-green"></a>
</p>

<p align="center">
  <a href="https://www.bilibili.com/video/BV17Ypz6YEKd"><b>▶ 在 Bilibili 观看演示视频</b></a>
</p>

---

所有算法均**照 UE 5.5 源码逐行移植**为 C++ GDExtension + GDScript，目标是在 Godot 中得到与 GASP 一致的动画表现，而不是“近似效果”。

> [!IMPORTANT]
> **本仓库不含任何 Epic 内容。** 动画、网格、贴图、声音、关卡等都要由你本机的 GASP 工程导出生成（见[快速开始](#-快速开始)）。生成物（`godot/assets/`、`godot/data/`、`tools/ue/out/`）已被 `.gitignore` 忽略，请勿提交或再分发。GASP 内容受 Epic 的许可条款约束。

> [!NOTE]
> 范围：只做 Motion Matching 与动作动画，**MetaHuman 不在范围内**。

## 目录

- [功能](#-功能)
- [环境要求](#-环境要求)
- [快速开始](#-快速开始)
- [操作](#-操作)
- [setup.py 详解](#-setuppy-详解)
- [管线概览](#-管线概览)
- [项目结构](#-项目结构)
- [许可](#-许可)

## ✨ 功能

| 模块 | 内容 |
|---|---|
| **Motion Matching** | PoseSearch 移植：PCAKDTree / BruteForce 搜索、Chooser 选库（Dense/Sparse LOD）、续播偏置、PoseReselectHistory |
| **BlendStack** | 主 MM 栈与状态机栈，BlendProfile 逐骨混合，通知过滤与触发权重阈值 |
| **AnimationWarping** | OrientationWarping、Steering、OffsetRootBone、Lean / AimOffset 加性层 |
| **Traversal** | 翻越 / 攀爬检测 → Chooser 选动画 → Montage + Motion Warping，支持连续翻越 |
| **FootPlacement + LegIK** | 世界地面查询、Stops / Default 两套 Plant 参数 |
| **IK Retargeter** | IK Retargeter 与 PBIK 完整移植，支持多角色切换 |
| **角色移动** | CharacterMovementComponent 行走 / 下落 / 旋转、步态切换 |
| **相机** | GameplayCameras 的 9 个相机 rig |
| **其它** | Foley 脚步声、实验状态机（可与 MM 切换）、UMG UI、CVar、调试绘制 |

## 📋 环境要求

> [!WARNING]
> 以下是**验证过的版本，请严格对齐**。尤其 UE 必须是 5.5.x，`setup.py` 会检查版本并拒绝其它版本。

| 组件 | 版本 | 说明 |
|---|---|---|
| 操作系统 | Windows 11 | 安装脚本为 Python，原生构建为批处理；UE 导出与 MSVC 编译都在 Windows 上验证 |
| Unreal Engine | **5.5.4**（`++UE5+Release-5.5`，CL 40574608） | Epic Games Launcher 安装版。`setup.py` 读 `Engine/Build/Build.version`，非 5.5.x 直接拒绝 |
| GASP | **Game Animation Sample Project，UE 5.5 版** | Fab/Epic 免费示例，工程名 `GameAnimationSample`（识别方式见下） |
| Godot | **4.6.2-stable**（official win64） | 工程特性 `4.6`、Forward Plus、Jolt Physics |
| godot-cpp | 分支 **4.5**，提交 `27d9dd2` | 由 `setup.py` 自动克隆并检出；已验证与 Godot 4.6.2 兼容 |
| C++ 工具链 | Visual Studio 2022 Community（MSVC v143） | 自带 CMake + Ninja，`native/build.bat` 使用 |
| Python | 3.11+ 与 numpy（验证：3.14.3 + numpy 2.4.2） | 运行 `tools/setup.py` 与 `tools/build`；UE 内置 Python 3.11 另行运行导出脚本 |

<details>
<summary><b>如何确认 GASP 工程版本</b></summary>

该示例没有独立版本号，可通过以下信息识别：

- `.uproject` 中 `"EngineAssociation": "5.5"`、`"EpicSampleNameHash": "1673045636"`
- `.uproject` 的 SHA-256：`69e07ca9bb4644722afac9bf39aba4bcb8442ee855fbc405df9d888ab50e3af1`
- 含 `Content/MetaHumans`（本项目不使用）

godot-cpp 完整提交号：`27d9dd23c83871e0619fca5dc2cddfbfd69e926a`
</details>

**磁盘与耗时（参考）**

| 项目 | 大小 / 耗时 |
|---|---|
| `godot/assets` | 约 3.4 GB |
| UE 导出 | 约 10 分钟（无头约 6 分钟 + 完整编辑器四步约 4 分钟） |
| `godot --import` | 约 80 秒 |
| `build_mm.py` | 约 2.5 分钟 |
| 原生编译（含克隆 godot-cpp） | 约 80 秒 |

## 🚀 快速开始

**1. 克隆并运行一键脚本**

在**普通终端**（PowerShell / Windows Terminal / cmd，不是 Claude Code 的 `!` 命令）里：

```powershell
git clone https://github.com/MrWen33/GASP-Godot.git gasp-godot
cd gasp-godot
python tools/setup.py
```

**2. 回答 4 个路径问题**

方括号里是自动探测到的默认值，直接回车即采用：

1. GASP 工程文件 `GameAnimationSample.uproject`（自动在仓库上级目录附近查找）
2. Unreal Engine 5.5 安装目录（含 `Engine` 的那一层，如 `C:\Program Files\Epic Games\UE_5.5`）
3. Godot 4.6.x 可执行文件（标准版，非 .NET）
4. Visual Studio 2022 安装目录（需装有“使用 C++ 的桌面开发”工作负载；`--skip-native` 时不问）

> [!TIP]
> 路径可带引号，资源管理器里“复制为路径”的结果可直接粘贴。输入错误会提示原因并重问。

**3. 等待完成**

脚本按顺序执行全部 40 余个步骤，最后的 `smoke_test` 无头运行 `godot/tools/test_run.gd`，退出码 0 即整套可用。中途失败时修复问题后**原命令重跑即可**，已完成的步骤会跳过。

**4. 启动**

```powershell
& "D:\godot\Godot_v4.6.2-stable_win64.exe" --path godot
```

## 🎮 操作

| 按键 | 动作 |
|---|---|
| `W` `A` `S` `D` | 移动 |
| 鼠标 | 视角 |
| `Shift` | 冲刺 |
| `Ctrl` | 切换走 / 跑 |
| `C` | 蹲 |
| `Space` | 跳 / 翻越（按住连续翻越） |
| `Esc` | 释放 / 捕获鼠标 |

场景里的按钮可切换角色与 MM / 状态机。

## 🔧 setup.py 详解

<details>
<summary><b>命令行参数</b></summary>

| 参数 | 作用 |
|---|---|
| `--list` | 列出全部步骤 |
| `--force` | 全部重做 |
| `--only a,b` | 只做指定步骤 |
| `--from <步骤>` | 从某步起执行 |
| `--skip-native` | 跳过 godot-cpp 与 C++ 编译 |
| `--reconfigure` | 重新询问路径 |

</details>

<details>
<summary><b>配置的保存与优先级</b></summary>

- 回答完会询问是否保存到 `tools/.setup.json`（已被 git 忽略），之后再运行直接复用。
- 取值优先级：**命令行参数 > 环境变量**（`GASP_UPROJECT`、`UE_ROOT`、`GODOT`、`VSROOT`）**> 已保存的回答 > 交互提问**。
- 运行脚本的这个 Python 就是构建步骤用的 Python。标准库即可运行脚本；构建步骤需要 numpy，缺时脚本会询问是否 `pip install numpy`。
- UE 目录从注册表和各盘常见位置探测；Godot 从 PATH 探测；VS 用 `vswhere` 只匹配装有 C++ 工作负载的实例。

</details>

<details>
<summary><b>执行行为与故障排查</b></summary>

- 先逐项校验设置（UE 版本 = 5.5.x、Godot 4.6、VS 含 vcvars64、Python + numpy），再按固定顺序执行。
- **可续跑**：每步若其产物已存在就跳过；失败即停止并指出日志 `tools/ue/out/logs/<步骤>.log`。
- UE 完整编辑器步骤（`export_audio` / `export_fbx` / `export_char_meshes` / `export_level`）结束时 UE 可能以崩溃码（如 `-1073740791`）退出，这是 UE 关闭阶段的已知现象；脚本以**产物是否生成**判定成功。

</details>

## 🧭 管线概览

```
GASP .uproject ──(UE 5.5 Python: tools/ue/*.py)──► tools/ue/out/**  +  godot/assets/**
                                   │ T3D(UTF-16)→UTF-8
Godot --import ─► dump_skeleton.gd ─► godot/data/godot_skeleton.json
tools/ue/out/** ──(tools/build/*.py)──► godot/data/**   (anims.bin、MM 特征库、Chooser、蒙太奇…)
native/ (C++ GDExtension, godot-cpp 4.5) ──► godot/bin/gasp_native.dll
```

## 📁 项目结构

```
godot/            Godot 工程（scripts/ 运行时、tools/ 测试与诊断、shaders/、bin/ 扩展输出）
native/           C++ GDExtension（动画图、MM、BlendStack、Traversal 蒙太奇、FootPlacement/LegIK、重定向、PBIK…）
tools/ue/         UE 内运行的导出脚本
tools/build/      导出物 → Godot 数据
tools/static/     无法导出的静态数据
tools/setup.py    一键脚本
```

## 📄 许可

本仓库原创代码以 [MIT License](LICENSE) 发布。与 Epic Games 的关系、移植自 Unreal Engine 源码部分的说明及第三方组件见 [NOTICE.md](NOTICE.md)。

本项目是**非官方**社区项目，与 Epic Games, Inc. 无关联，也未获其认可或赞助。
