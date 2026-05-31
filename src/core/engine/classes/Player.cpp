#include "Player.hpp"

#include <cstring>

#include "Weapon.hpp"
#include "config/Current.hpp"
#include "core/engine/cache/Cache.hpp"
#include "core/engine/Engine.hpp"
#include "core/offsets/Dumper.hpp"
#include "core/engine/classes/ObserverServices.hpp"

namespace {
	bool needs_bones() {
		return cfg::esp::skeleton || cfg::esp::head_tracker || cfg::esp::head_tracker_eye_line
			|| cfg::esp::spotted::skeleton || cfg::esp::spotted::head_tracker || cfg::esp::spotted::head_tracker_eye_line
			|| (cfg::triggerbot::enabled && cfg::triggerbot::soft_aim);
	}

	bool needs_weapon() { return cfg::esp::flags::weapon || cfg::esp::flags::ammo || cfg::esp::flags::reloading; }
	bool needs_name()   { return cfg::esp::flags::name || cfg::world::spectators::enabled; }

	template <std::ptrdiff_t Base, std::size_t Size>
	struct Block {
		std::byte data[Size];

		template <class T>
		T at(std::ptrdiff_t off) const {
			T v;
			std::memcpy(&v, data + (off - Base), sizeof(T));
			return v;
		}

		const std::byte* bytes(std::ptrdiff_t off) const { return data + (off - Base); }
	};

	namespace buf {
		thread_local Block<0x6B0, 0x830 - 0x6B0> ctrl;
		thread_local Block<0x330, 0x3400 - 0x330> pawn;
		thread_local bone_data bones[30];
	}
}

bool Player::Update() {
	if (!Engine::GetProcess())
		return false;

	if (!GetController()) {
		//LOGF(WARNING, "Failed to GET controller for entity index({})", index);
		return false;
	}

	if (!GetPawn()) {
		//LOGF(WARNING, "Failed to GET pawn for entity index({})", index);
		return false;
	}

	if (!UpdateController()) {
		//LOGF(WARNING, "Failed to UPDATE controller for entity index({})", index);
		return false;
	}

	if (!UpdatePawn()) {
		//LOGF(WARNING, "Failed to UPDATE pawn for entity index({})", index);
		return false;
	}

	return true;
}

bool Player::GetController() {
	auto p = Engine::GetProcess();

	controller = p->read<DWORD64>(list_entry + (index + 1) * 0x70);
	if (!controller)
		return false;

	return p->read_raw(controller + 0x6B0, buf::ctrl.data, sizeof(buf::ctrl.data));
}

bool Player::GetPawn() {
	auto p = Engine::GetProcess();

	const auto handle = buf::ctrl.at<uintptr_t>(offsets::controller::m_hPawn);
	if (!handle)
		return false;

	pawn_controller_addr = handle;

	const auto list = p->read<uintptr_t>(entity_list + 0x10 + 0x8 * ((handle & 0x7FFF) >> 9));
	if (!list)
		return false;

	pawn = p->read<uintptr_t>(list + 0x70 * (handle & 0x1FF));
	pawn_addr = pawn;

	return pawn != 0;
}

bool Player::UpdateController() {
	auto p = Engine::GetProcess();

	steam_id    = buf::ctrl.at<uint64_t>(offsets::controller::m_steamID);
	bot         = steam_id == 0;
	localplayer = buf::ctrl.at<bool>(offsets::controller::m_bIsLocalPlayerController);

	if (needs_name())
		std::memcpy(name, buf::ctrl.bytes(offsets::controller::m_iszPlayerName), sizeof(name));
	else
		name[0] = '\0';

	if (cfg::esp::flags::ping)
		ping = buf::ctrl.at<int>(offsets::controller::m_iPing);

	if (cfg::esp::flags::money) {
		if (const auto svc = buf::ctrl.at<uintptr_t>(offsets::controller::m_pInGameMoneyServices))
			money = p->read<int>(svc + offsets::controller::m_iAccount);
	}

	return true;
}

bool Player::UpdatePawn() {
	auto p = Engine::GetProcess();

	if (!p->read_raw(pawn + 0x330, buf::pawn.data, sizeof(buf::pawn.data)))
		return false;

	health = buf::pawn.at<int>(offsets::pawn::m_iHealth);
	alive  = health != 0;

	if (health < 0 || health > 255)
		LOGF(FATAL, "Health out of range ({}). Game probably updated pawn structure", health);

	if (localplayer || cfg::world::spectators::enabled)
		UpdateObserverServices();

	if (!alive)
		return true;

	pos = buf::pawn.at<Vec3_t>(offsets::pawn::m_vOldOrigin);
	if (pos.zero())
		return false;

	view_offset_z = buf::pawn.at<float>(offsets::pawn::m_vecViewOffsetZ);
	eye_angles    = buf::pawn.at<Vec3_t>(offsets::pawn::m_angEyeAngles);
	vel           = buf::pawn.at<Vec3_t>(offsets::pawn::m_vecAbsVelocity);
	team          = buf::pawn.at<uint8_t>(offsets::pawn::m_iTeamNum);
	armor         = buf::pawn.at<int>(offsets::pawn::m_ArmorValue);
	defusing      = buf::pawn.at<bool>(offsets::pawn::m_bIsDefusing);
	scoped        = buf::pawn.at<bool>(offsets::pawn::m_bIsScoped);
	flashed       = buf::pawn.at<float>(offsets::pawn::m_flFlashOverlayAlpha) > 0;

	if (localplayer)
		crosshair_ent_index = buf::pawn.at<int32_t>(offsets::pawn::m_iIDEntIndex);

	if (needs_bones() && Cache::ShouldRefreshBones()) {
		if (!UpdateSkeleton()) {
			LOGF(FATAL, "Failed to update skeleton");
			return false;
		}
	} else if (!needs_bones()) {
		bone_list.clear();
	}

	if (localplayer || needs_weapon())
		return UpdateWeapon();

	weapon = {};
	ammo = -1;
	is_reloading = false;
	return true;
}

bool Player::UpdateSkeleton() {
	auto p = Engine::GetProcess();

	const auto scene = buf::pawn.at<uintptr_t>(offsets::pawn::m_pGameSceneNode);
	if (!scene)
		return false;

	const auto array = p->read<DWORD64>(scene + offsets::bone::m_modelState + 0x80);
	if (!array || !p->read_raw(array, buf::bones, sizeof(buf::bones)))
		return false;

	bone_list.clear();
	for (const auto& b : buf::bones)
		bone_list.push_back({ b.pos });

	return true;
}

bool Player::UpdateWeapon() {
	auto p = Engine::GetProcess();

	const auto svc = buf::pawn.at<uintptr_t>(offsets::pawn::m_pWeaponServices);
	if (!svc)
		return false;

	const auto slot = p->read<uint32_t>(svc + offsets::pawn::m_hActiveWeapon);
	if (!slot)
		return false;

	Weapon w(entity_list, slot);
	if (!w.Update())
		return false;

	weapon = w;
	ammo = w.ammo;
	is_reloading = w.is_reloading;
	return true;
}

bool Player::GetBounds(view_matrix_t matrix, Vec2_t size, std::pair<Vec2_t, Vec2_t>& bounds) const {
	Vec2_t origin;
	const bool pt1 = matrix.wts(pos, size, origin);

	Vec3_t pos_top;
	if (bone_list.empty())
		pos_top = pos + Vec3_t(0, 0, 65.f); // 75.f
	else
		pos_top = bone_list[bone_index::head].pos;

	//auto head_bone = this->bone_list[bone_index::head];
	//head_bone.pos.z *= 1.09; // little offset to cover the entire head
	//bone_pos head_bone = origin + ImVec3

	Vec2_t top;
	const bool pt2 = matrix.wts(pos_top, size, top);

	const float width = (origin.y - top.y) / 2.4f;
	top.x    -= width / 2;
	origin.x += width / 2;
	top.y    -= width / 4;

	// Top to bottom
	bounds = { top, origin };
	return pt1 || pt2;
}

// Does not update if match is started
bool Player::UpdateObserverServices() {
	const auto address = buf::pawn.at<DWORD64>(offsets::pawn::m_pObserverServices);
	if (!address)
		return false;

	observer_services.SetAddress(address);
	return observer_services.Update();
}
