#pragma once

#include "VisCheck.h"
#include "core/engine/types/Vec3.hpp"

class Player;

#include <atomic>
#include <memory>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

class VisCheckManager {
public:
    static void OnMapChanged(const char* mapName);
    static bool IsReady();
    static bool IsVisible(const Vec3_t& from, const Vec3_t& to, float weaponPen = 0.f);
    static void UpdateSpotted(const Player& local, std::vector<Player>& players);

private:
    static VisCheckManager& Get();

    void LoadAsync(std::string map);
    void SetVisCheck(std::unique_ptr<VisCheck> vis);

    std::shared_mutex mtx_;
    std::unique_ptr<VisCheck> vis_;
    std::atomic<bool> loading_{ false };
    std::thread worker_;
    std::string pendingMap_;
};
