#pragma once

#include "core/engine/cache/Cache.hpp"

class Recoil {
public:
	static void Tick(const Snapshot& snap);
	static bool IsShooting();
};
