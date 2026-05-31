#include "Cache.hpp"

#include <cstring>

#include "config/Current.hpp"
#include "core/engine/Engine.hpp"
#include "core/offsets/Dumper.hpp"
#include "core/vischeck/VisCheckManager.h"

namespace {
	bool spotted_enabled() {
		return cfg::esp::spotted::box || cfg::esp::spotted::skeleton
			|| cfg::esp::spotted::head_tracker || cfg::esp::spotted::head_tracker_eye_line;
	}
}

std::shared_ptr<const Snapshot> Cache::GetSnapshot() {
	return Get().published_.load(std::memory_order_acquire);
}

bool Cache::Refresh() {
	return Get().RefreshImpl();
}

void Cache::MergeFromPrev(const Snapshot& prev, std::vector<Player>& players, bool bones, bool spotted) {
	const Player* by_index[64]{};
	for (const auto& p : prev.players) {
		if (p.index >= 0 && static_cast<size_t>(p.index) < std::size(by_index))
			by_index[p.index] = &p;
	}

	for (auto& p : players) {
		if (p.index < 0 || static_cast<size_t>(p.index) >= std::size(by_index))
			continue;
		const auto* op = by_index[p.index];
		if (!op)
			continue;
		if (spotted)
			p.spotted_can_engage = op->spotted_can_engage;
		if (bones && p.alive)
			p.bone_list = op->bone_list;
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

	refresh_bones_ = now - last_bones_ >= 20ms;
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
		Player player(i, game.entity_list, game.list_entry);
		if (!player.Update())
			continue;

		if (player.localplayer)
			local = player;

		// TODO: Handle or at least alert, in case of multiple lp
		//if (player.localplayer && (this->local.index == -1 || this->local.index == player.index))
		//    this->local = player;
		//else if (player.localplayer)
		//    LOGF(FATAL, "Offset missmatch, initial({}) current({}) there are more than one local players, update needed", this->local.index, player.index);

		scan.push_back(std::move(player));
	}

	const bool refresh_spotted = spotted_enabled() && now - last_spotted_ >= 50ms;

	if (prev)
		MergeFromPrev(*prev, scan, !refresh_bones_, !refresh_spotted);

	if (refresh_spotted) {
		VisCheckManager::UpdateSpotted(local, scan);
		last_spotted_ = now;
	}

	auto snap = std::make_shared<Snapshot>(Snapshot{ game, bomb, local, globals, std::move(scan) });
	published_.store(std::move(snap), std::memory_order_release);

	duration = duration_cast<milliseconds>(now - last);
	last = now;
	return true;
}
