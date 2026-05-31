#include "Triggerbot.hpp"

#include "core/engine/Engine.hpp"
#include "core/engine/types/Matrix.hpp"
#include "core/offsets/Dumper.hpp"

#include <cmath>
#include <random>

namespace {
	constexpr auto kDropTarget = 450ms;
	constexpr float kPxToMouse = 0.22f;
	constexpr float kFlickMaxPx = 120.f;
	constexpr float kTrackMaxPx = 240.f;

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

	float RandF(float lo, float hi) {
		return std::uniform_real_distribution{ lo, hi }(Rng());
	}

	int RandMs(int lo, int hi) {
		return std::uniform_int_distribution{ lo, hi }(Rng());
	}

	int ReactionMs() {
		return RandMs(55, 115);
	}

	// Snipers lose first-shot accuracy while the view is moving, so we never auto-aim them.
	bool IsSniper(short id) {
		return id == weapon_awp || id == weapon_ssg08 || id == weapon_scar20 || id == weapon_g3sg1;
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

	Vec2_t ScreenSize(HWND hwnd) {
		RECT rc{};
		if (!hwnd || !GetClientRect(hwnd, &rc))
			return { 1920.f, 1080.f };
		return { (float)(rc.right - rc.left), (float)(rc.bottom - rc.top) };
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

	void SoftAim(const view_matrix_t& matrix, const Vec2_t& screen, const Vec3_t& head, bool tracking) {
		Vec2_t target;
		if (!matrix.wts(head, screen, target, false))
			return;

		const float cx = screen.x * 0.5f, cy = screen.y * 0.5f;
		float px = target.x - cx, py = target.y - cy;
		const float dist = std::hypot(px, py);
		const float max_px = tracking ? kTrackMaxPx : kFlickMaxPx;
		if (dist < 1.5f || dist > max_px)
			return;

		const float t = (tracking ? 0.38f : 0.55f) * RandF(0.9f, 1.1f);
		float mx = px * t * kPxToMouse;
		float my = py * t * kPxToMouse;

		// Bias each step sideways so the path arcs and trembles instead of snapping dead-straight.
		const float curve = RandF(-0.18f, 0.18f);
		const float bx = mx, by = my;
		mx -= by * curve;
		my += bx * curve;

		const float cap = tracking ? 14.f : 10.f;
		if (const float mag = std::hypot(mx, my); mag > cap) {
			mx *= cap / mag;
			my *= cap / mag;
		}

		int dx = (int)std::lround(mx);
		int dy = (int)std::lround(my);
		if (!dx && std::abs(mx) >= 1.f)
			dx = mx > 0.f ? 1 : -1;
		if (!dy && std::abs(my) >= 1.f)
			dy = my > 0.f ? 1 : -1;
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
	static steady_clock::time_point ready_at{}, next_shot{}, miss_since{}, next_aim{};

	auto reset = [&] {
		locked_pawn = 0;
		ready_at = next_shot = miss_since = next_aim = {};
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

	if (cfg::triggerbot::soft_aim && !IsSniper(snap.local.weapon.item_index) && now >= next_aim) {
		SoftAim(snap.game.view_matrix, ScreenSize(proc->hwnd_), AimPos(proc, track, locked_pawn), !on_crosshair);
		next_aim = now + milliseconds(RandMs(5, 10));
	}

	if (!on_crosshair || now < ready_at || now < next_shot)
		return;

	MouseClick();
	next_shot = now + milliseconds(RandMs(100, 220));
}

bool Triggerbot::IsHeld() {
	return SideButtonHeld();
}
