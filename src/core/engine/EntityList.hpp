#pragma once

#include "core/engine/Engine.hpp"
#include "core/engine/classes/Player.hpp"
#include "core/engine/types/Vec3.hpp"

inline uintptr_t ResolveEntity(uintptr_t entity_list, int32_t ent_index) {
	if (ent_index <= 0 || !entity_list)
		return 0;

	auto proc = Engine::GetProcess();
	if (!proc)
		return 0;

	const int32_t idx = ent_index & 0x7FFF;
	auto bucket = proc->read<uintptr_t>(entity_list + 0x10 + 0x8 * (idx >> 9));
	if (!bucket)
		return 0;

	return proc->read<uintptr_t>(bucket + 0x70 * (idx & 0x1FF));
}

inline bool CrosshairOnLocal(uintptr_t entity_list, int32_t crosshair_index, const Player& local) {
	if (crosshair_index <= 0 || !local.pawn_addr)
		return false;

	const int32_t idx = crosshair_index & 0x7FFF;
	if (idx == (local.pawn_controller_addr & 0x7FFF))
		return true;

	const uintptr_t ent = ResolveEntity(entity_list, crosshair_index);
	if (!ent)
		return false;

	return ent == local.pawn_addr || (local.controller && ent == local.controller);
}

inline bool AnglesOnLocal(const Player& enemy, const Player& local, float fov_deg = 6.f) {
	if (enemy.pos.zero() || local.pos.zero())
		return false;

	Vec3_t to = (local.pos + Vec3_t(0.f, 0.f, local.view_offset_z))
		- (enemy.pos + Vec3_t(0.f, 0.f, enemy.view_offset_z));
	if (to.length_sqr() < 1.f)
		return false;

	to.normalize();
	Vec3_t forward = Vec3_t::FromAngle(enemy.eye_angles);
	forward.normalize();

	const float cos_fov = std::cos(fov_deg * std::numbers::pi_v<float> / 180.f);
	return forward.dot(to) >= cos_fov;
}
