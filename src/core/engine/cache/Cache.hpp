#pragma once
#include "core/engine/classes/Game.hpp"
#include "core/engine/classes/Bomb.hpp"
#include "core/engine/classes/Player.hpp"
#include "core/engine/classes/Globals.hpp"

#include <atomic>
#include <chrono>
#include <memory>

using namespace std::chrono;

struct Snapshot {
	Game game;
	Bomb bomb;
	Player local;
	Globals globals;
	std::vector<Player> players;
};

class Cache {
public:
	Game game;
	Bomb bomb;
	Player local;
	Globals globals;
	std::vector<Player> players;
public:
	static Cache& Get()
	{
		static Cache instance{};
		return instance;
	}

	static std::shared_ptr<const Snapshot> GetSnapshot();

	static bool Refresh();
private:
	std::atomic<std::shared_ptr<const Snapshot>> published_{};
	milliseconds duration{1};
	steady_clock::time_point last{};
	steady_clock::time_point last_spotted_{};
	steady_clock::time_point last_bones_{};
	bool refresh_bones_{true};
public:
	static bool ShouldRefreshBones() { return Get().refresh_bones_; }
private:
	bool RefreshImpl();
	static void CopySpottedFlags(const Snapshot& prev, std::vector<Player>& players);
	static void CopyBoneData(const Snapshot& prev, std::vector<Player>& players);
};
