# NOTICE / 声明

本项目是**非官方**的社区项目，与 Epic Games, Inc. 无关联，也未获其认可或赞助。
"Unreal Engine"、"Game Animation Sample" 等名称与商标归 Epic Games 所有。

## Epic 内容与 Unreal Engine 源码

- 本仓库**不包含也不分发**任何 Epic 内容（动画、网格、贴图、声音、关卡、蓝图/Chooser 数据等）。这些内容须由使用者本机合法获得的 GASP 工程导出生成，生成物（`godot/assets/`、`godot/data/`、`tools/ue/out/`）已被 `.gitignore` 忽略，请勿提交或再分发。GASP 内容受 Epic 的许可条款约束。
- `native/src/` 中的 Motion Matching（PoseSearch）、BlendStack、AnimationWarping、IK Retargeter、PBIK 等算法参照 Unreal Engine 5.5 源码移植。Unreal Engine 源码受 [Unreal Engine EULA](https://www.unrealengine.com/eula) 约束，**MIT 许可只覆盖本仓库作者自己的原创部分，不改变 Epic 对其源码及衍生内容享有的任何权利**。使用、再分发移植部分前，请自行确认符合 Epic 的条款。

## 第三方组件

- [Godot Engine](https://godotengine.org/)：MIT License
- [godot-cpp](https://github.com/godotengine/godot-cpp)：MIT License（由 `tools/setup.py` 克隆到 `native/godot-cpp`，不随仓库提交）
- Jolt Physics（经 Godot 集成）：MIT License
