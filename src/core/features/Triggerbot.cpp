#include "Triggerbot.hpp"

#include "core/engine/Engine.hpp"
#include "core/offsets/Dumper.hpp"

#include <random>

namespace {
	constexpr auto kMissGrace = 100ms;

	bool SideButtonHeld() {
		return (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) || (GetAsyncKeyState(VK_XBUTTON2) & 0x8000);
	}

	std::mt19937& Rng() {
		static thread_local std::mt19937 g{ std::random_device{}() };
		return g;
	}

	int RandMs(int lo, int hi) {
		return std::uniform_int_distribution{ lo, hi }(Rng());
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

	void MouseClick() {
		INPUT down{ .type = INPUT_MOUSE, .mi = { .dwFlags = MOUSEEVENTF_LEFTDOWN } };
		INPUT up{ .type = INPUT_MOUSE, .mi = { .dwFlags = MOUSEEVENTF_LEFTUP } };

		SendInput(1, &down, sizeof(INPUT));
		Sleep(RandMs(10, 22));
		SendInput(1, &up, sizeof(INPUT));
	}
}

void Triggerbot::Tick(const Snapshot& snap) {
	static int32_t locked_ent = -1;
	static steady_clock::time_point ready_at{}, next_shot{}, miss_since{};

	auto reset = [&] {
		locked_ent = -1;
		ready_at = next_shot = miss_since = {};
	};

	if (!cfg::enabled || !cfg::triggerbot::enabled || !SideButtonHeld()) {
		reset();
		return;
	}

	auto proc = Engine::GetProcess();
	auto client = Engine::GetClient();
	if (!proc || !client.base || !proc->hwnd_ || GetForegroundWindow() != proc->hwnd_)
		return;

	const auto& local = snap.local;
	if (!local.alive || !local.pawn_addr)
		return;

	const int32_t ent_index = proc->read<int32_t>(local.pawn_addr + offsets::pawn::m_iIDEntIndex);
	const auto now = steady_clock::now();

	auto lose_target = [&] {
		if (miss_since == steady_clock::time_point{})
			miss_since = now;
		if (now - miss_since < kMissGrace)
			return;
		reset();
	};

	if (ent_index <= 0) {
		lose_target();
		return;
	}

	const uintptr_t entity_list = proc->read<uintptr_t>(client.base + offsets::entityList);
	const uintptr_t target_pawn = ResolveEnt(entity_list, ent_index);
	if (!target_pawn) {
		lose_target();
		return;
	}

	const int health = proc->read<int>(target_pawn + offsets::pawn::m_iHealth);
	const int team = proc->read<int>(target_pawn + offsets::pawn::m_iTeamNum);
	if (health <= 0 || health > 100 || team == local.team) {
		lose_target();
		return;
	}

	miss_since = {};

	if (locked_ent != ent_index) {
		locked_ent = ent_index;
		ready_at = now + milliseconds(RandMs(45, 110));
	}

	if (now < ready_at || now < next_shot)
		return;

	MouseClick();
	next_shot = now + milliseconds(RandMs(80, 210));
}

bool Triggerbot::IsHeld() {
	return SideButtonHeld();
}
