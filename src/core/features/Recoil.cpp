#include "Recoil.hpp"

#include "core/engine/Engine.hpp"
#include "core/offsets/Dumper.hpp"

#include <cmath>
#include <random>

namespace {
	constexpr float kAimScale = 3.2f;
	constexpr float kPunchScale = 2.f;

	struct UtlVec {
		uint32_t count;
		uint32_t pad;
		uint64_t data;
	};

	std::mt19937& Rng() {
		static thread_local std::mt19937 g{ std::random_device{}() };
		return g;
	}

	float RandF(float lo, float hi) {
		return std::uniform_real_distribution{ lo, hi }(Rng());
	}

	int RandI(int lo, int hi) {
		return std::uniform_int_distribution{ lo, hi }(Rng());
	}

	bool GameReady(HWND hwnd) {
		return hwnd && IsWindow(hwnd) && !IsIconic(hwnd);
	}

	uintptr_t ResolveEnt(uintptr_t entity_list, int32_t ent_index) {
		if (ent_index <= 0 || !entity_list)
			return 0;

		auto proc = Engine::GetProcess();
		if (!proc)
			return 0;

		auto bucket = proc->read<uintptr_t>(entity_list + 0x10 + 0x8 * (ent_index >> 9));
		if (!bucket)
			return 0;

		return proc->read<uintptr_t>(bucket + 0x70 * (ent_index & 0x1FF));
	}

	uintptr_t ResolvePawnHandle(uintptr_t entity_list, uint32_t handle) {
		if (!handle || !entity_list)
			return 0;
		return ResolveEnt(entity_list, handle & 0x7FFF);
	}

	uintptr_t LocalPawn(const std::shared_ptr<pProcess>& proc, const ProcessModule& client, const Snapshot& snap) {
		if (snap.local.pawn_addr)
			return snap.local.pawn_addr;

		const uintptr_t controller = proc->read<uintptr_t>(client.base + offsets::localPlayerController);
		if (!controller)
			return 0;

		const uintptr_t entity_list = proc->read<uintptr_t>(client.base + offsets::entityList);
		const uint32_t handle = proc->read<uint32_t>(controller + offsets::controller::m_hPawn);
		return ResolvePawnHandle(entity_list, handle);
	}

	bool ReadCachePunch(const std::shared_ptr<pProcess>& proc, uintptr_t svc, std::ptrdiff_t cache_off, Vec3_t& out) {
		const UtlVec cache = proc->read<UtlVec>(svc + cache_off);
		if (cache.count <= 0 || cache.count >= 512 || !cache.data)
			return false;

		out = proc->read<Vec3_t>(cache.data + (cache.count - 1) * sizeof(Vec3_t));
		return true;
	}

	Vec3_t ReadPunch(const std::shared_ptr<pProcess>& proc, uintptr_t pawn) {
		const uintptr_t svc = proc->read<uintptr_t>(pawn + offsets::pawn::m_pAimPunchServices);
		if (!svc)
			return {};

		Vec3_t punch{};
		if (ReadCachePunch(proc, svc, offsets::aimPunch::m_cache, punch))
			return punch;
		if (ReadCachePunch(proc, svc, offsets::aimPunch::m_cacheAlt, punch))
			return punch;

		const Vec3_t pred = proc->read<Vec3_t>(svc + offsets::aimPunch::m_predictableBaseAngle);
		const Vec3_t unpred = proc->read<Vec3_t>(svc + offsets::aimPunch::m_unpredictableBaseAngle);
		return pred + unpred;
	}

	float SprayFactor(int shots) {
		if (shots <= 4)
			return RandF(0.9f, 1.f);
		if (shots <= 9)
			return RandF(0.72f, 0.9f) - (shots - 4) * 0.025f;
		if (shots <= 16)
			return RandF(0.42f, 0.68f) - (shots - 9) * 0.02f;
		if (shots <= 24)
			return RandF(0.15f, 0.35f);
		return RandF(0.04f, 0.14f);
	}

	float HoldFactor(steady_clock::duration held) {
		const auto ms = duration_cast<milliseconds>(held).count();
		if (ms < 350)
			return 1.f;
		if (ms < 850)
			return 1.f - (ms - 350) * 0.00045f;
		return RandF(0.3f, 0.5f);
	}
}

void Recoil::Tick(const Snapshot& snap) {
	static Vec3_t last_punch{};
	static steady_clock::time_point spray_start{};
	static bool primed = false;

	if (!cfg::enabled || !cfg::rcs::enabled) {
		last_punch = {};
		primed = false;
		return;
	}

	auto proc = Engine::GetProcess();
	const auto client = Engine::GetClient();
	if (!proc || !client.base || !GameReady(proc->hwnd_))
		return;

	const uintptr_t pawn = LocalPawn(proc, client, snap);
	if (!pawn || snap.local.health <= 0)
		return;

	const int shots = proc->read<int>(pawn + offsets::pawn::m_iShotsFired);
	if (shots <= 0) {
		last_punch = {};
		primed = false;
		return;
	}

	const Vec3_t punch = ReadPunch(proc, pawn);
	if (!primed) {
		spray_start = steady_clock::now();
		last_punch = punch;
		primed = true;
		return;
	}

	Vec3_t delta = punch - last_punch;
	last_punch = punch;

	if (delta.x == 0.f && delta.y == 0.f)
		return;

	if (RandI(0, 99) < cfg::rcs::miss_chance)
		return;

	const float factor = cfg::rcs::strength * SprayFactor(shots) * HoldFactor(steady_clock::now() - spray_start) * RandF(0.84f, 0.97f);
	const float pitch = -delta.x * kPunchScale * factor;
	const float yaw = delta.y * kPunchScale * factor;

	int dx = (int)(yaw / kAimScale) + RandI(-1, 1);
	int dy = (int)(pitch / kAimScale) + RandI(-1, 1);
	dx = std::clamp(dx, -cfg::rcs::max_step, cfg::rcs::max_step);
	dy = std::clamp(dy, -cfg::rcs::max_step, cfg::rcs::max_step);

	if (!dx && !dy)
		return;

	INPUT move{ .type = INPUT_MOUSE, .mi = { .dx = dx, .dy = dy, .dwFlags = MOUSEEVENTF_MOVE } };
	SendInput(1, &move, sizeof(INPUT));
}

bool Recoil::IsShooting() {
	return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
}
