#include "ObserverServices.hpp"
#include "core/engine/Engine.hpp"
#include "core/offsets/Dumper.hpp"

bool ObserverServices::Update() {
	auto p = Engine::GetProcess();
	if (!p || !address)
		return false;

	struct { ObserverMode mode; int target; } block{};
	if (!p->read_raw(address + offsets::observerServices::m_iObserverMode, &block, sizeof(block)))
		return false;

	mode = block.mode;
	target = block.target;
	return true;
}

void ObserverServices::SetAddress(DWORD64 address) {
	this->address = address;
}

const char* ObserverServices::ToString() const {
	switch (this->mode)
	{
	case ObserverMode::Alive:   return "Self";
	case ObserverMode::Unknown:   return "1";
	case ObserverMode::First: return "First person";
	case ObserverMode::Third: return "Third person";
	case ObserverMode::Free: return "Free Roam";
	default:      return "Unknown";
	}
}