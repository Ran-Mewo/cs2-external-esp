#include "Cache.hpp"

#include <cstring>

#include "config/Current.hpp"
#include "core/engine/Engine.hpp"
#include "core/offsets/Dumper.hpp"
#include "core/vischeck/VisCheckManager.h"

std::shared_ptr<const Snapshot> Cache::GetSnapshot() {
	return Get().published_.load(std::memory_order_acquire);
}

bool Cache::Refresh() {
	return Get().RefreshImpl();
}

void Cache::CopySpottedFlags(const Snapshot& prev, std::vector<Player>& players) {
	for (auto& p : players) {
		for (const auto& op : prev.players) {
			if (op.index == p.index) {
				p.spotted_can_engage = op.spotted_can_engage;
				break;
			}
		}
	}
}

void Cache::CopyBoneData(const Snapshot& prev, std::vector<Player>& players) {
	for (auto& p : players) {
		if (!p.alive)
			continue;
		for (const auto& op : prev.players) {
			if (op.index == p.index) {
				p.bone_list = op.bone_list;
				break;
			}
		}
	}
}

bool Cache::RefreshImpl() {
	auto p = Engine::GetProcess();
	if (!p)
		return false;

	const auto now = steady_clock::now();

#ifdef _DEBUG
	const auto refresh_interval = cfg::dev::cache_refresh_rate * 1ms;
#else
	constexpr auto refresh_interval = 5ms;
#endif

	if (now - last < refresh_interval)
		return true;

	constexpr auto bone_interval = 20ms;
	refresh_bones_ = now - last_bones_ >= bone_interval;
	if (refresh_bones_)
		last_bones_ = now;

	const auto prev = published_.load(std::memory_order_acquire);

	if (!game.Update())
		return false;

	game.UpdateEntityList();
	char prev_map[sizeof(globals.map_name)]{};
	std::memcpy(prev_map, globals.map_name, sizeof(prev_map));
	globals.Update();
	if (std::strncmp(globals.map_name, prev_map, sizeof(globals.map_name)) != 0)
		VisCheckManager::OnMapChanged(globals.map_name);
	bomb.Update();

	std::vector<Player> scan;
	scan.reserve(globals.max_clients);
	for (int i = 0; i < globals.max_clients; i++) {
		auto player = Player(i, game.entity_list, game.list_entry);

		if (!player.Update())
			continue;

		if (player.localplayer)
			this->local = player;

		scan.push_back(player);
	}

	if (!refresh_bones_ && prev)
		CopyBoneData(*prev, scan);

	const bool spotted_on = cfg::esp::spotted::box || cfg::esp::spotted::skeleton
		|| cfg::esp::spotted::head_tracker || cfg::esp::spotted::head_tracker_eye_line;
	constexpr auto spotted_interval = 50ms;

	if (spotted_on && now - last_spotted_ >= spotted_interval) {
		VisCheckManager::UpdateSpotted(local, scan);
		last_spotted_ = now;
	} else if (prev)
		CopySpottedFlags(*prev, scan);

	auto snap = std::make_shared<Snapshot>();
	snap->game = game;
	snap->bomb = bomb;
	snap->local = local;
	snap->globals = globals;
	snap->players = std::move(scan);

	players = snap->players;
	published_.store(snap, std::memory_order_release);

	duration = duration_cast<milliseconds>(now - last);
	last = now;

	return true;
}
