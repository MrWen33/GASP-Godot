# AGENTS.md

GASP（UE 5.5 Game Animation Sample）→ Godot 4.6 的原样复刻：Motion Matching、BlendStack、Traversal、FootPlacement/LegIK、IK Retargeter、相机、Foley、UI 等。**范围边界：只关注 MM 与动作动画，MetaHuman（角色/Groom/RigLogic）已剔除，不要加回。**

UE 5.5 引擎与插件 C++ 源码齐全（Engine/Source、Plugins/Animation/PoseSearch、AnimationWarping、BlendStack…），所有算法**照 UE 源码逐行移植**，行为存疑时先读源码/蓝图伪代码，不要凭印象。

## 目录与产物
- `godot/`：Godot 工程（Jolt 物理，主场景 `res://scenes/main.tscn`）。`scripts/` 运行时 GDScript，`tools/` 无头测试/诊断脚本，`shaders/`。
- `native/`：C++ GDExtension（godot-cpp 4.5 分支，克隆到 `native/godot-cpp`，不入 git）。输出 `godot/bin/gasp_native.dll`，描述文件 `godot/bin/gasp_native.gdextension`。
- `tools/ue/`：在 UE 里运行的导出脚本（输出到 `tools/ue/out/`，不入 git）；`tools/build/`：把导出物转成 Godot 运行时数据（输出到 `godot/data/`、`godot/assets/`，不入 git）。
- `godot/assets`、`godot/data`、`tools/ue/out` 均由 GASP（Epic 内容）生成，**不提交**，需要时重新生成。
- 脚本中的路径用环境变量 `GASP_REPO`（仓库根，默认 `.`）；GASP 工程目录、UE 目录、Godot exe 由本机提供。

## 常用命令
- **一键重建全部生成物**：在终端里运行 `python tools/setup.py`（交互式，由人回答路径；幂等可续跑，`--list` 列步骤；版本矩阵见 README.md，步骤与产物见 `tools/setup.py` 的 STEPS 表）。新增/改动导出或构建脚本时同步更新该 STEPS 表。
- 构建 C++：`native/build.bat`（需设 `VSROOT`=VS2022 目录，可选 `PYTHON`；用 PowerShell `cmd /c build.bat` 调用）。改 C++ 后必须重建；新增 GDScript `class_name` 后需 `godot --headless --path godot --import` 刷新全局类缓存。
- 运行测试（Godot headless）：`godot --headless --path godot --script res://tools/test_xxx.gd`。窗口模式截图存到 `%APPDATA%/Godot/app_userdata/GASP Godot/`，高刷新率截图需 `--fixed-fps 60`。主要测试：`test_run`（MM 选库）、`test_traversal`（环境变量 TRAV_CASE/TRAV_JERK/TRAV_TRACE…；`VARIANT=SetupComparison`）、`test_foley`、`test_ui`、`test_feet`、`test_characters`、`test_sm`/`test_sm_toggle`、`test_comparison`、`test_lod`、`test_turn`、`test_wall`、`test_query`、`test_perf`。
- UE 无头 Python：`UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<py> -EnablePlugins=PythonScriptPlugin -unattended -nop4 -nosplash -stdout`
  - commandlet 下 `unreal.log` 无输出 → 结果写文件。
  - 骨骼网格 FBX 导出在 commandlet 崩溃 → 用完整编辑器 `UnrealEditor.exe -ExecutePythonScript=<py>`，脚本末尾 `unreal.SystemLibrary.quit_editor()`。
  - PIE 探针：`UnrealEditor.exe <uproject> -ExecCmds="py <脚本>"`（`-ExecutePythonScript` 不会执行脚本就退出），见 `tools/ue/pie_wall_probe.py`、`compare_pie_trav.py`。
  - InstancedStruct（数据库条目、Chooser 行）Python 读不到 → 用 T3D（`unreal.ObjectExporterT3D`）；T3D 有的 UTF-16 有的 ASCII（按 `file` 判断再 iconv）；T3D 会省略「空 instanced struct 的数组元素」→ 按索引补空行。Python 属性名去 b 前缀。
  - 用户枚举的数值顺序 ≠ NewEnumeratorN（E_ExperimentalStateMachineState、E_MovementDirection 等），要从 .uasset 尾部名字表读真实值。
- 数据重建：`tools/build/` 下 `build_anims.py`、`build_mm.py`、`build_blendspaces.py`、`build_montages.py`（含 curves）、`build_chooser.py`、`build_chooser_sm.py`、`build_foley.py`、`build_widgets.py`、`build_retarget.py`、`build_notify_thresholds.py`（直接只读解析 .uasset）。

## 坐标系（已验证，勿改）
- **运行时数据保持 UE 空间**（cm，UE 轴向；MM 的 MinFeatureMeanDeviation=0.1 依赖 cm）。`anims.bin` 为 UE 局部空间关键帧，仅在写入 Skeleton3D 时转换：骨骼局部 p'=(x,-y,z)*0.01，q'=(-x,y,-z,w)。骨骼顺序与 UE 相同（88 根），无需重定向。
- 世界：UE (x,y,z)cm ↔ Godot (x,z,y)*0.01；四元数 UE→Godot=(-x,-z,-y,w)（反射，角度取反）；网格朝 Godot +Z。MM 查询：Godot 网格空间向量 g → UE (g.x,g.z,g.y)*100（方向不乘 100）。
- `scripts/ue.gd` 是 UE 空间数学，Godot Quaternion 直接当 UE 四元数用。Godot 骨架空间 = UE 组件空间 Y 取反。

## 运行时架构
- C++（`native/src/`）：`anim_graph.*`（MM 节点 + 两个 BlendStack：主 MM 栈与 SMStack；播放器子图 OrientationWarping→ResetRoot→Steering×2；之后 Lean/AO 加性层 → Slot(Montage) → OffsetRootBone → FootPlacement → LegIK → PoseHistory）、`mm.*`（PCAKD/BruteForce 搜索）、`blend_stack.*`、`blendspace.*`、`montage.*`（MontagePlayer + MotionWarping）、`foot_placement.*`、`pbik.*`/`retarget.*`/`gasp_retargeter.*`（IK Retargeter 与 PBIK 完整移植）、`pose_interpolator.*`、`cs_pose.h`（FCSPose 语义）、`uemath.h`（UE 语义 Vec3/Quat/Xform）、`anim_lib.*`（曲线索引用**小写键**，FName 不分大小写；`CollectNotifies`）。
- Godot 暴露类：`GaspAnimator`（initialize/reset/update(dict)/apply_to_skeleton/get_state/set_physics_space…）、`GaspRetargeter`、`GaspPoseInterpolator`。
- GDScript：`sandbox_character.gd`（CMC 行走/下落/旋转、CBP 步态、Traversal 全流程、ABP 变量 `ABP_FIELDS` 按实例保存）、`trajectory.gd`、`chooser.gd`/`chooser_multi.gd`、`experimental_state_machine.gd`、`follow_camera.gd`（GameplayCameras 9 个 rig）、`foley_audio.gd`、`level_builder.gd`、`umg_builder.gd`+`gasp_ui.gd`、`cvars.gd`、`debug_draw.gd`、`character_visual.gd`/`character_materials.gd`。
- 无法从 T3D 取到的值是估计的：IsPivoting 阈值（Orient 45° / Strafe 30°）、ShouldTurnInPlace 取 WantsToAim、Get_AOValue 取控制器旋转、AO 的 BlendListByBool+DeadBlending 用逐骨定时交叉淡化近似。

## 关键 UE 行为（实现/调试时对照）
- MM 搜索：PCAKDTree = 4 主成分取 200 近邻（排除 BlockTransition 与 NonSelectable）再算全代价；Traversal 库为 BruteForce。代价=Σ((q-v)·wsqrt)²+cost_addend+续播偏置。disable_reselection 排除整个源资产；PoseReselectHistory 0.3s；PoseJumpThresholdTime=(0,0)（允许同片段回跳）。每帧搜索（SearchThrottleTime=0）；WantedPlayRate=clamp(…,0.85,1.15)。
- MM 库来自 Chooser（CHT_PoseSearchDatabases：MMDatabaseLOD 0→Dense、1→Sparse，其它值无库）；表为多结果模式，FloatRange 默认 Min=Max=0。
- 采样复刻 FAssetIndexer：根骨锁定、根运动=Root(t)·Root(0)⁻¹、**用 float32 算采样时间**（float64 会产生 -2.8e-17 负时间导致落地特征错误）。
- BlendProfile：照 `BlendProfile.cpp CalculateBoneWeight`；无 Mode 行=WeightFactor，BlendScale=0 条目在 T3D 中被省略。
- Traversal：IA_Jump Started/Triggered 都先 TryTraversalAction，失败才 Jump；检测→ledge→Chooser(CHT_TraversalAnims, MultiEnum 位掩码)→Montage+MotionWarping；结束 Vault→Falling，其余→Walking（Flying 在 ABP 中算 OnGround）；`MovingTraversal` 曲线在 **montage 资产自身**（非动画序列），决定 JustTraversed。
- **蒙太奇/Slot**：Slot 的 bAlwaysUpdateSourcePose=false，源权重不相关时整条运动分支（MM 搜索、混合栈时间、通知）**冻结**；蒙太奇一开始淡出就不再给根运动；被打断时 OnInterrupted 在淡出开始即触发；旧蒙太奇需作为独立实例继续淡出（连续翻越）；Slot 激活时 OffsetRoot 平移/旋转都 Release；蒙太奇满权重且源未更新时槽位姿势不带根运动，OffsetRootBone 的 clamp 会失效，需照 UE 处理。
- BlendStack：PopLastAnimPlayer 后 stored pose 要**丢弃**（UE `MovePoseContextTo` 之后 `StoredBones.Reset()`），前一播放器继续自己的动画，否则 MM 无 continuing pose 画面冻结。通知过滤：同名本帧一次 + NotifyRecencyTimeOut（Walk/Run 0.2，Sprint 0.16）；脚步通知有 TriggerWeightThreshold（Run 多为 0.2），播放器权重低于阈值不入队，MM 路径再乘槽源权重。
- CMC 细节：行走时胶囊离地约 2.2 cm（中心=地面+86+2.2）；PhysWalking「Make velocity reflect actual move」使撞墙时水平速度=实际位移/dt；SetMovementMode(Walking) 立即 FindFloor+AdjustFloorHeight（traversal 结束别经 Falling）；传送器只 K2_TeleportTo，不重置动画实例/MM/姿势/轨迹历史（用 `level_teleport_to_ue`，不要用会 reset 的 `teleport_to_ue`）。
- FootPlacement：cvar `a.AnimNode.FootPlacement.Enable`；CurrentDatabaseTags 含 "Stops" 用 PlantSettings_Stops，否则 Default；世界查询经 `set_physics_space`（PhysicsServer3D cast_motion + get_rest_info）。
- 高刷新率/物理插值：项目开了 physics_interpolation，姿势只在 60 Hz 物理帧更新。相机必须读角色的渲染插值位置（`_render_location_ue()`），骨骼姿势经 `GaspPoseInterpolator` 逐渲染帧插值，否则相机/翻越时人物会抖。
- 实验状态机（SM）默认关闭（DDCVar.ExperimentalStateMachine），MM 与 SM 可由 UI 切换；SetupComparison 每个网格组件一个 ABP 实例。

## 构建/环境坑
- cl 中文界面输出 GBK 的「注意: 包含文件:」→ ninja 不追踪头文件依赖 → 改头文件后部分 .cpp 未重编、结构体布局不一致而崩溃。`native/tools_fix_deps_prefix.py` 在 `build.bat` 每次构建前改写 rules.ninja 的 msvc_deps_prefix，不要绕过。
- GDScript 字符串里 `\d` 是非法转义；Bash 工具里含某些引号的 heredoc 会报 "unexpected EOF"，改用 Write 写脚本文件再执行。

## 对照与验证方法
- UE PIE 对照（`pie_wall_probe.py`）：编辑器视口不渲染时网格不更新姿势 → 放置时设 VisibilityBasedAnimTickOption=AlwaysTickPoseAndRefreshBones、关 URO，并重建动画实例（否则 OffsetRootBone 保留旧朝向）。环境变量 PIE_CASES/PIE_SECONDS/PIE_OUT/PIE_PROBE/PIE_CMDS。
- 抖动度量：`test_traversal` 的 `TRAV_JERK=<阈值>` 逐帧算主要骨骼二阶差分；`TRAV_JERK_FULL=1` 附 get_state；`TRAV_NOFP=1` 关 FootPlacement。get_state 提供 query/pose_features/schema/continuing_cost/can_advance/players 供调试。

## 待办
可选：Echo PoseDriver、RigidBody 马尾；MI_UEFN_Mannequin_Blue 区域遮罩着色。
