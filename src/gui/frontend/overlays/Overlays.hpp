#pragma once

#include "core/engine/cache/Cache.hpp"

class Overlays {
public:
    ~Overlays() = default;
    Overlays(const Overlays&) = delete;
    Overlays(Overlays&&) = delete;
    Overlays& operator=(const Overlays&) = delete;
    Overlays& operator=(Overlays&&) = delete;

    static bool Init();
    static void Render(const Snapshot& snapshot);

private:
    ImFont* font;
    ImFont* font_alt;

    size_t vel_index = 0;
    std::vector<int> vel_buffer;
    std::vector<ImVec2> vel_points;
    float vel_accumulator = 0.0f;
private:
    Overlays() {};

    static Overlays& GetInstance()
    {
        static Overlays i{};
        return i;
    }

    bool InitImpl();
    void RenderImpl(const Snapshot& snapshot);

    void RenderNotice();
    void RenderWatermark(const Snapshot& snapshot);
    void RenderSpeedChart(const Snapshot& snapshot);
    void RenderDebugWindow(const Snapshot& snapshot);
    void RenderSpectatorList(const Snapshot& snapshot);
};