#pragma once
#include "core/engine/classes/Bones.hpp"
#include "core/engine/classes/Weapon.hpp"
#include "core/engine/classes/ObserverServices.hpp"

class Player {
public:
    Player() = default;
    Player(int index, uintptr_t entity_list, uintptr_t list_entry)
        : index(index), entity_list(entity_list), list_entry(list_entry) {}

    bool Update();
    bool GetBounds(view_matrix_t matrix, Vec2_t size, std::pair<Vec2_t, Vec2_t>& bounds) const;

    int8_t index = -1; // To use as invalid/un-initialize check

    Vec3_t pos;
    Vec3_t eye_angles;
    Vec3_t vel;
    float view_offset_z = 0.f;

    int ping = 0;
    int team = 0;
    int health = 0;
    int armor = 0;
    int money = 0;

    bool bot = true;
    bool alive = false;
    bool scoped = false;
    bool flashed = false;
    bool defusing = false;
    bool localplayer = false;
    bool spotted_can_engage = false;

    int32_t crosshair_ent_index = -1;
    uintptr_t pawn_addr = 0;
    int pawn_controller_addr = 0;

    char name[32]{};
    //std::string name;
    uint64_t steam_id = 0;

    Weapon weapon;
    int32_t ammo = 0;
    bool is_reloading = false;

    std::vector<bone_pos> bone_list;
    ObserverServices observer_services;

private:
    uintptr_t list_entry = 0;
    uintptr_t entity_list = 0;
    uintptr_t pawn = 0;
    uintptr_t controller = 0;

    bool GetPawn();
    bool GetController();
    bool UpdatePawn();
    bool UpdateWeapon();
    bool UpdateSkeleton();
    bool UpdateController();
    bool UpdateObserverServices();
};
