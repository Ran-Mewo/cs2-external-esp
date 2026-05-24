#pragma once

#include "core/engine/cache/Cache.hpp"

class Triggerbot {
public:
	static void Tick(const Cache& cache);
	static bool IsHeld();
};
