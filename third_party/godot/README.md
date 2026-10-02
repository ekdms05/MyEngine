# Godot source attribution

Reference revision: `12c17c187e88efa23e6bbd6689630e256eca6523`, inspected 2026-10-02.

`engine/scene/src/phys/PhysicsWorld3D.cpp` adapts the `Vector3::slide` projection from `core/math/vector3.h`. CharacterBody3D grounded slide/snap/slope stopping, SpringArm3D cast distance and SceneReplicationInterface authority validation informed the implementation. The ECS, collision sweeps, camera input and UDP protocol are MyEngine integrations, not imported Godot subsystems.

Upstream: <https://github.com/godotengine/godot/tree/12c17c187e88efa23e6bbd6689630e256eca6523>.
Copyright holders and the complete MIT permission notice are preserved in LICENSE.txt and AUTHORS.md. Both are included in binary distributions. No Godot runtime/build dependency was added.
