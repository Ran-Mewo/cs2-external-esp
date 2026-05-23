#pragma once

#include "core/engine/cache/Cache.hpp"

class Triggerbot {
public:
	static void Tick(const Snapshot& snap);
	static bool IsHeld();
};
