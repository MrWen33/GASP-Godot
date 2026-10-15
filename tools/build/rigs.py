"""Control Rig graphs (dumped with rigvm_graph.py) hand ported to post-process ops evaluated by GaspPostProcess:
  twist:   rel = Initial(driver).local^-1 * Current(driver).local; t = SwingTwist(rel, axis).twist (inverted if "invert");
           each target: SetRotation(LocalSpace, t, weight) -> local rot = Slerp(local rot, t, weight)
  project: FRigUnit_ProjectTransformToNewParent(child/old parent initial, new parent current) rotation, set with
           SetRotation(GlobalSpace, weight, propagate). "slerp_with" blends two projections (MathQuaternionSlerp T) first."""


def _twist(driver, targets, invert):
    return {"op": "twist", "driver": driver, "axis": [1.0, 0.0, 0.0], "invert": invert, "targets": targets}


def _proj(child, parent, weight, other_parent=None, t=0.5):
    op = {"op": "project", "bone": child, "new_parent": parent, "old_parent": parent, "weight": weight, "propagate": True}
    if other_parent:
        op["slerp_with"] = {"new_parent": other_parent, "old_parent": other_parent, "t": t}
    return op


RIGS = {
    # CR_Echo_Twist: upper arm / thigh twist bones counter the parent's twist, lower arm / calf twist bones follow the hand / foot
    "CR_Echo_Twist": [
        _twist("upperarm_l", [["upperarm_twist_01_l", 1.0], ["upperarm_twist_02_l", 0.5]], True),
        _twist("upperarm_r", [["upperarm_twist_01_r", 1.0], ["upperarm_twist_02_r", 0.5]], True),
        _twist("thigh_l", [["thigh_twist_01_l", 1.0], ["thigh_twist_02_l", 0.5]], True),
        _twist("thigh_r", [["thigh_twist_01_r", 1.0], ["thigh_twist_02_r", 0.5]], True),
        _twist("hand_l", [["lowerarm_twist_01_l", 1.0], ["lowerarm_twist_02_l", 0.5]], False),
        _twist("hand_r", [["lowerarm_twist_01_r", 1.0], ["lowerarm_twist_02_r", 0.5]], False),
        _twist("foot_l", [["calf_twist_01_l", 1.0], ["calf_twist_02_l", 0.5]], False),
        _twist("foot_r", [["calf_twist_01_r", 1.0], ["calf_twist_02_r", 0.5]], False),
    ],
    # CR_Echo_Helpers: scarf / skirt roots keep their initial offset to the driving bone
    "CR_Echo_Helpers": [
        _proj("C_Scarf_A_Root_Jnt", "clavicle_l", 1.0),
        _proj("C_Scarf_A_Joint2_Jnt", "spine_04", 0.5),
        _proj("C_Scarf_A_Joint3_Jnt", "spine_04", 0.5),
        _proj("L_Skirt_A_BackRoot_Jnt", "pelvis", 1.0),
        _proj("R_Skirt_A_BackRoot_Jnt", "pelvis", 1.0),
        _proj("C_Skirt_A_BackRoot_Jnt", "pelvis", 1.0),
        _proj("C_Skirt_A_FrontRoot_Jnt", "L_Skirt_A_FrontRoot_Jnt", 1.0, "R_Skirt_A_FrontRoot_Jnt", 0.5),
    ],
}
