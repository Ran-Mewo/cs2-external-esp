#pragma once
#include "core/engine/types/Weapons.hpp"

class Weapon {
public:
	Weapon() = default;
	Weapon(uintptr_t entity_list, int slot_index)
		: entity_list(entity_list), slot_index(slot_index) {}

	bool Update();

	short item_index = -1;
	std::string name = "Invalid";
	int32_t ammo = 0;
	bool is_reloading = false;
	float penetration = 0.f;

private:
	const char* ToString() const;

	uintptr_t entity_list = 0;
	int slot_index = 0;
};

