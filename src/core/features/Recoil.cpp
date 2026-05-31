#include "Recoil.hpp"

#include "core/engine/Engine.hpp"
#include "core/offsets/Dumper.hpp"

#include <cmath>
#include <random>

namespace {
	constexpr float kAimScale = 3.2f;
	constexpr float kPunchScale = 2.f;

	struct UtlVec { uint32_t count, pad; uint64_t data; };

	std::mt19937& Rng() {
		static thread_local std::mt19937 g{ std::random_device{}() };
		return g;
	}

	float RandF(float lo, float hi) { return std::uniform_real_distribution{ lo, hi }(Rng()); }
	int   RandI(int lo, int hi)     { return std::uniform_int_distribution{ lo, hi }(Rng()); }

	bool GameReady(HWND hwnd) { return hwnd && IsWindow(hwnd) && !IsIconic(hwnd); }

	bool ReadCachedPunch(const std::shared_ptr<pProcess>& proc, uintptr_t svc, std::ptrdiff_t off, Vec3_t& out) {
		const auto cache = proc->read<UtlVec>(svc + off);
		if (cache.count == 0 || cache.count >= 512 || !cache.data)
			return false;
		out = proc->read<Vec3_t>(cache.data + (cache.count - 1) * sizeof(Vec3_t));
		return true;
	}

	Vec3_t ReadPunch(const std::shared_ptr<pProcess>& proc, uintptr_t pawn) {
		const auto svc = proc->read<uintptr_t>(pawn + offsets::pawn::m_pAimPunchServices);
		if (!svc)
			return {};

		Vec3_t punch{};
		if (ReadCachedPunch(proc, svc, offsets::aimPunch::m_cache, punch))    return punch;
		if (ReadCachedPunch(proc, svc, offsets::aimPunch::m_cacheAlt, punch)) return punch;

		return proc->read<Vec3_t>(svc + offsets::aimPunch::m_predictableBaseAngle)
			 + proc->read<Vec3_t>(svc + offsets::aimPunch::m_unpredictableBaseAngle);
	}

	float SprayFactor(int shots) {
		if (shots <= 4)  return RandF(0.9f, 1.f);
		if (shots <= 9)  return RandF(0.72f, 0.9f) - (shots - 4) * 0.025f;
		if (shots <= 16) return RandF(0.42f, 0.68f) - (shots - 9) * 0.02f;
		if (shots <= 24) return RandF(0.15f, 0.35f);
		return RandF(0.04f, 0.14f);
	}

	float HoldFactor(steady_clock::duration held) {
		const auto ms = duration_cast<milliseconds>(held).count();
		if (ms < 350) return 1.f;
		if (ms < 850) return 1.f - (ms - 350) * 0.00045f;
		return RandF(0.3f, 0.5f);
	}
}

void Recoil::Tick(const Snapshot& snap) {
	static Vec3_t last_punch{};
	static steady_clock::time_point spray_start{};
	static bool primed = false;

	const auto reset = [&] { last_punch = {}; primed = false; };

	if (!cfg::enabled || !cfg::rcs::enabled) {
		reset();
		return;
	}

	auto proc = Engine::GetProcess();
	if (!proc || !GameReady(proc->hwnd_))
		return;

	const auto pawn = snap.local.pawn_addr;
	if (!pawn || snap.local.health <= 0)
		return;

	const int shots = proc->read<int>(pawn + offsets::pawn::m_iShotsFired);
	if (shots <= 0) {
		reset();
		return;
	}

	const Vec3_t punch = ReadPunch(proc, pawn);
	if (!primed) {
		spray_start = steady_clock::now();
		last_punch = punch;
		primed = true;
		return;
	}

	const Vec3_t delta = punch - last_punch;
	last_punch = punch;

	if ((delta.x == 0.f && delta.y == 0.f) || RandI(0, 99) < cfg::rcs::miss_chance)
		return;

	const float factor = cfg::rcs::strength * SprayFactor(shots) * HoldFactor(steady_clock::now() - spray_start) * RandF(0.84f, 0.97f);
	const int max = cfg::rcs::max_step;
	const int dx = std::clamp(int( delta.y * kPunchScale * factor / kAimScale) + RandI(-1, 1), -max, max);
	const int dy = std::clamp(int(-delta.x * kPunchScale * factor / kAimScale) + RandI(-1, 1), -max, max);
	if (!dx && !dy)
		return;

	INPUT move{ .type = INPUT_MOUSE, .mi = { .dx = dx, .dy = dy, .dwFlags = MOUSEEVENTF_MOVE } };
	SendInput(1, &move, sizeof(INPUT));
}

bool Recoil::IsShooting() {
	return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
}
