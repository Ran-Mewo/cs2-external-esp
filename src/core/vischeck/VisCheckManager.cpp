#include "VisCheckManager.h"

#include "PathUtil.h"
#include "VpkMapExtractor.h"

#include <algorithm>
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
