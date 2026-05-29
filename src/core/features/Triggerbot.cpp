#include "Triggerbot.hpp"

#include "core/engine/Engine.hpp"
#include "core/offsets/Dumper.hpp"

#include <cmath>
#include <random>

namespace {
	constexpr auto kDropTarget = 450ms;
	constexpr float kSoftAimMaxDeg = 10.f;
	constexpr float kTrackMaxDeg = 22.f;
	constexpr float kAimScale = 3.2f;

	struct Target {
		uintptr_t pawn = 0;
		const Player* player = nullptr;
	};

	bool SideButtonHeld() {
		return (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) || (GetAsyncKeyState(VK_XBUTTON2) & 0x8000);
	}

	bool GameReady(HWND hwnd) {
		return hwnd && IsWindow(hwnd) && !IsIconic(hwnd);
	}

	std::mt19937& Rng() {
		static thread_local std::mt19937 g{ std::random_device{}() };
		return g;
	}

	int RandMs(int lo, int hi) {
		return std::uniform_int_distribution{ lo, hi }(Rng());
	}

	int ReactionMs() {
		return RandMs(55, 115);
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

	bool ValidEnemy(const std::shared_ptr<pProcess>& proc, uintptr_t pawn, int local_team) {
		const int hp = proc->read<int>(pawn + offsets::pawn::m_iHealth);
		const int team = proc->read<int>(pawn + offsets::pawn::m_iTeamNum);
		return hp > 0 && hp <= 100 && team != local_team;
	}

	const Player* FindPlayer(const Snapshot& snap, uintptr_t pawn) {
		for (const auto& p : snap.players) {
			if (p.pawn_addr == pawn)
				return &p;
		}
		return nullptr;
	}

	Vec3_t HeadPos(const Player& p) {
		if (p.bone_list.size() > bone_index::head)
			return p.bone_list[bone_index::head].pos;
		return p.pos + Vec3_t{ 0.f, 0.f, 64.f };
	}

	void NormalizeYaw(float& yaw) {
		while (yaw > 180.f) yaw -= 360.f;
		while (yaw < -180.f) yaw += 360.f;
	}

	Target CrosshairTarget(const std::shared_ptr<pProcess>& proc, const Snapshot& snap, uintptr_t local_pawn) {
		const uintptr_t entity_list = proc->read<uintptr_t>(Engine::GetClient().base + offsets::entityList);
		const int local_team = proc->read<int>(local_pawn + offsets::pawn::m_iTeamNum);
		const int32_t ent_index = proc->read<int32_t>(local_pawn + offsets::pawn::m_iIDEntIndex);

		if (ent_index <= 0)
			return {};

		const uintptr_t pawn = ResolveEnt(entity_list, ent_index);
		if (!pawn || !ValidEnemy(proc, pawn, local_team))
			return {};

		return { pawn, FindPlayer(snap, pawn) };
	}

	Vec3_t AimPos(const std::shared_ptr<pProcess>& proc, const Player* player, uintptr_t pawn) {
		if (player)
			return HeadPos(*player);

		const Vec3_t pos = proc->read<Vec3_t>(pawn + offsets::pawn::m_vOldOrigin);
		return pos + Vec3_t{ 0.f, 0.f, 64.f };
	}

	void SoftAim(const Vec3_t& eye, const Vec3_t& view, const Vec3_t& head, bool tracking) {
		Vec3_t wish = (head - eye).ToAngle();
		float pitch_d = wish.x - view.x;
		float yaw_d = wish.y - view.y;
		NormalizeYaw(yaw_d);

		const float max_deg = tracking ? kTrackMaxDeg : kSoftAimMaxDeg;
		const float dist = std::hypot(pitch_d, yaw_d);
		if (dist < 0.1f || dist > max_deg)
			return;

		const float t = (tracking ? RandMs(14, 28) : RandMs(9, 20)) / 100.f;
		pitch_d *= t;
		yaw_d *= t;

		const int cap = tracking ? 14 : 10;
		int dx = (int)(yaw_d / kAimScale) + RandMs(-1, 1);
		int dy = (int)(-pitch_d / kAimScale) + RandMs(-1, 1);
		dx = std::clamp(dx, -cap, cap);
		dy = std::clamp(dy, -cap, cap);
		if (!dx && !dy)
			return;

		INPUT move{ .type = INPUT_MOUSE, .mi = { .dx = dx, .dy = dy, .dwFlags = MOUSEEVENTF_MOVE } };
		SendInput(1, &move, sizeof(INPUT));
	}

	void MouseClick() {
		INPUT down{ .type = INPUT_MOUSE, .mi = { .dwFlags = MOUSEEVENTF_LEFTDOWN } };
		INPUT up{ .type = INPUT_MOUSE, .mi = { .dwFlags = MOUSEEVENTF_LEFTUP } };
		SendInput(1, &down, sizeof(INPUT));
		Sleep(RandMs(8, 16));
		SendInput(1, &up, sizeof(INPUT));
	}
}

void Triggerbot::Tick(const Snapshot& snap) {
	static uintptr_t locked_pawn = 0;
	static steady_clock::time_point ready_at{}, next_shot{}, miss_since{};

	auto reset = [&] {
		locked_pawn = 0;
		ready_at = next_shot = miss_since = {};
	};

	if (!cfg::enabled || !cfg::triggerbot::enabled || !SideButtonHeld()) {
		reset();
		return;
	}

	auto proc = Engine::GetProcess();
	const auto client = Engine::GetClient();
	if (!proc || !client.base || !GameReady(proc->hwnd_))
		return;

	const uintptr_t local_pawn = LocalPawn(proc, client, snap);
	if (!local_pawn)
		return;

	const auto now = steady_clock::now();
	const int local_team = proc->read<int>(local_pawn + offsets::pawn::m_iTeamNum);
	const Target crosshair = CrosshairTarget(proc, snap, local_pawn);

	if (crosshair.pawn) {
		if (crosshair.pawn != locked_pawn) {
			locked_pawn = crosshair.pawn;
			ready_at = now + milliseconds(ReactionMs());
		}
		miss_since = {};
	} else if (locked_pawn) {
		if (miss_since == steady_clock::time_point{})
			miss_since = now;
		if (now - miss_since >= kDropTarget) {
			reset();
			return;
		}
	} else {
		return;
	}

	if (!ValidEnemy(proc, locked_pawn, local_team)) {
		reset();
		return;
	}

	const Player* track = FindPlayer(snap, locked_pawn);
	const bool on_crosshair = crosshair.pawn == locked_pawn;

	if (cfg::triggerbot::soft_aim) {
		const Vec3_t eye = snap.local.pos + Vec3_t{ 0.f, 0.f, snap.local.view_offset_z };
		const Vec3_t view = proc->read<Vec3_t>(local_pawn + offsets::pawn::m_angEyeAngles);
		SoftAim(eye, view, AimPos(proc, track, locked_pawn), !on_crosshair);
	}

	if (!on_crosshair || now < ready_at || now < next_shot)
		return;

	MouseClick();
	next_shot = now + milliseconds(RandMs(100, 220));
}

bool Triggerbot::IsHeld() {
	return SideButtonHeld();
}
