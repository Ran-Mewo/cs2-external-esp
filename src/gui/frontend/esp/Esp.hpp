#pragma once

#include "core/engine/cache/Cache.hpp"

class Esp {
public:
    ~Esp() = default;
    Esp(const Esp&) = delete;
    Esp(Esp&&) = delete;
    Esp& operator=(const Esp&) = delete;
    Esp& operator=(Esp&&) = delete;

    static bool Init();
    static void Render(const Snapshot& snapshot);

private:
    ImGuiIO io;
    ImFont* font;
    ImDrawList* d;

    // Temporary storage for ease
    view_matrix_t matrix;
private:
    Esp() {};

    static Esp& GetInstance()
    {
        static Esp i{};
        return i;
    }

    bool InitImpl();
    void RenderImpl(const Snapshot& snapshot);

    void RenderPlayer(const Player& player, bool mate = false, bool visible = false);
    void RenderPlayerBones(const Player& player, bool mate, bool visible);
    void RenderPlayerBars(const Player& player, std::pair<Vec2_t, Vec2_t> bounds);
    void RenderPlayerFalgs(const Player& player, std::pair<Vec2_t, Vec2_t> bounds, bool mate = false);
    void RenderPlayerTracker(const Player& player, std::pair<Vec2_t, Vec2_t> bounds, bool mate, bool visible);
    void RenderPlayerTracers(const Player& source, const Player& player, bool mate = false);

	void RenderCrosshair(const Player& local);
    void RenderBomb(const Player& local, const Bomb& bomb);
};