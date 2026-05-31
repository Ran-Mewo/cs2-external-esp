#include "VisCheckManager.h"

#include "PathUtil.h"
#include "VpkMapExtractor.h"
#include "config/Current.hpp"
#include "core/engine/classes/Bones.hpp"
#include "core/engine/classes/Player.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>

namespace {
	std::string TrimMapName(const char* mapName) {
		std::string map(mapName);
		if (const auto nul = map.find('\0'); nul != std::string::npos)
			map.resize(nul);
		if (map.starts_with("maps/"))
			map.erase(0, 5);
		if (map.ends_with(".vpk"))
			map.resize(map.size() - 4);
		return map;
	}

	bool IsValidMap(const std::string& map) {
		return !map.empty() && map.front() != '<' && map != "lobby";
	}

	bool IsTri2(const std::filesystem::path& path) {
		std::ifstream in(path, std::ios::binary);
		char magic[4]{};
		in.read(magic, 4);
		return std::memcmp(magic, "TRI2", 4) == 0;
	}

	bool SpottedHeadOnly() {
		return cfg::esp::spotted::box && !cfg::esp::spotted::skeleton
			&& !cfg::esp::spotted::head_tracker && !cfg::esp::spotted::head_tracker_eye_line;
	}

	const Vec3_t& TargetPos(const Player& player) {
		if (player.bone_list.size() > bone_index::head)
			return player.bone_list[bone_index::head].pos;
		return player.pos;
	}
}

VisCheckManager& VisCheckManager::Get() {
	static VisCheckManager inst;
	return inst;
}

void VisCheckManager::OnMapChanged(const char* mapName) {
	const auto map = TrimMapName(mapName);
	if (!IsValidMap(map))
		return;
	Get().LoadAsync(map);
}

bool VisCheckManager::IsReady() {
	auto& self = Get();
	std::shared_lock lock(self.mtx_);
	return self.vis_ && self.vis_->IsReady();
}

bool VisCheckManager::IsVisible(const Vec3_t& from, const Vec3_t& to, float weaponPen) {
	auto& self = Get();
	std::shared_lock lock(self.mtx_);
	if (!self.vis_ || !self.vis_->IsReady())
		return false;

	return self.vis_->Visible({ from.x, from.y, from.z }, { to.x, to.y, to.z }, weaponPen);
}

void VisCheckManager::UpdateSpotted(const Player& local, std::vector<Player>& players) {
	const bool any_spotted = cfg::esp::spotted::box || cfg::esp::spotted::skeleton
		|| cfg::esp::spotted::head_tracker || cfg::esp::spotted::head_tracker_eye_line;
	if (!any_spotted)
		return;

	auto& self = Get();
	std::shared_lock lock(self.mtx_);
	if (!self.vis_ || !self.vis_->IsReady())
		return;

	const Vector3 eye{ local.pos.x, local.pos.y, local.pos.z + local.view_offset_z };
	const float weaponPen = local.weapon.penetration;
	const bool head_only = SpottedHeadOnly();
	static constexpr std::array kBones{ bone_index::head, bone_index::neck, bone_index::chest };

	for (auto& player : players) {
		player.spotted_can_engage = false;
		if (!player.alive || player.localplayer)
			continue;
		if (!cfg::esp::team && player.team == local.team)
			continue;

		if (head_only) {
			const auto& pos = TargetPos(player);
			player.spotted_can_engage = self.vis_->CanEngage(eye, { pos.x, pos.y, pos.z }, weaponPen);
			continue;
		}

		if (player.bone_list.size() <= bone_index::head)
			continue;

		for (const auto idx : kBones) {
			if (player.bone_list.size() <= idx)
				continue;
			const auto& pos = player.bone_list[idx].pos;
			if (self.vis_->CanEngage(eye, { pos.x, pos.y, pos.z }, weaponPen)) {
				player.spotted_can_engage = true;
				break;
			}
		}
	}
}

void VisCheckManager::SetVisCheck(std::unique_ptr<VisCheck> vis) {
	std::unique_lock lock(mtx_);
	vis_ = std::move(vis);
}

void VisCheckManager::LoadAsync(std::string map) {
	{
		std::unique_lock lock(mtx_);
		if (pendingMap_ == map && (loading_ || (vis_ && vis_->IsReady())))
			return;
		pendingMap_ = map;
	}

	if (worker_.joinable())
		worker_.join();

	loading_ = true;
	worker_ = std::thread([this, map = std::move(map)]() {
		const auto cache = PathUtil::MapCachePath(map);
		if (!IsTri2(cache)) {
			std::filesystem::remove(cache);
			if (const auto err = VpkMapExtractor::EnsureTriCache(map))
				LOGF(WARNING, "VisCheck: {} — spotted ESP disabled", *err);
		}

		if (IsTri2(cache)) {
			auto vis = std::make_unique<VisCheck>(cache.string());
			if (vis->IsReady()) {
				LOGF(INFO, "VisCheck: loaded map '{}' ({} triangles)", map, vis->TriangleCount());
				SetVisCheck(std::move(vis));
			} else {
				std::filesystem::remove(cache);
				SetVisCheck(nullptr);
			}
		} else
			SetVisCheck(nullptr);

		loading_ = false;
	});
}
