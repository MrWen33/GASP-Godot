#pragma once
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/skeleton3d.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/rid.hpp>
#include "anim_graph.h"

namespace godot {

class GaspAnimator : public RefCounted {
	GDCLASS(GaspAnimator, RefCounted);

protected:
	static void _bind_methods();

public:
	bool initialize(const String& data_dir);
	void reset(const Vector3& comp_pos, const Quaternion& comp_rot);
	void update(double delta, const Dictionary& inputs);
	void apply_to_skeleton(Skeleton3D* skeleton);
	double get_curve(const String& name) const;
	Dictionary get_state() const;
	Transform3D get_root_offset_ue() const;
	PackedStringArray get_database_tags() const;
	Array get_fired_notifies() const; // blend stack (filtered) notifies of the last update + montage notifies of the last montage_tick
	Transform3D get_bone_cs_ue(const String& bone) const;
	// montage / traversal
	bool montage_play(const String& montage, double play_rate, double start_position);
	void montage_stop(double blend_time);
	Dictionary montage_tick(double delta, const Dictionary& owner);
	Dictionary montage_state() const;
	void set_warp_target(const String& name, const Vector3& pos, const Quaternion& rot);
	void remove_warp_target(const String& name);
	bool is_anim_looping(const String& anim) const;
	double get_anim_curve(const String& anim, const String& curve, double time) const;
	Dictionary motion_match_anims(const PackedStringArray& anims, const PackedStringArray& databases);
	Dictionary motion_match_montages(const PackedStringArray& montages, const Vector3& interaction_pos, const Quaternion& interaction_rot);
	double get_montage_curve(const String& montage, const String& curve, double time) const;
	Array get_montage_warp_windows(const String& montage, const String& target) const;
	const gasp::AnimGraph& graph() const { return Graph; }
	bool is_ready() const { return bReady; }
	// static helpers
	static Vector3 ue_to_godot_pos(const Vector3& v);
	~GaspAnimator();
	// foot placement ground traces
	void set_physics_space(const RID& space, const RID& exclude);
	Dictionary get_foot_placement_debug() const;
	// component-space scale replacement applied when writing to the skeleton (post-process anim blueprint), UE bone axes
	void set_post_process_scales(const Dictionary& scales);
	static Vector3 godot_to_ue_pos(const Vector3& v);
	static Quaternion ue_to_godot_quat(const Quaternion& q);
	static Quaternion godot_to_ue_quat(const Quaternion& q);

private:
	gasp::AnimGraph Graph;
	bool bReady = false;
	RID TraceSpace, TraceExclude, TraceShape;
	std::vector<std::pair<int, ue::Vec3>> PostScales;
	bool FootTrace(const ue::Vec3& Start, const ue::Vec3& End, double Radius, ue::Vec3& OutP, ue::Vec3& OutN);
	std::vector<int> SkeletonMap;
	Skeleton3D* LastSkeleton = nullptr;
};

} // namespace godot
