#include "VisCheck.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <utility>
#include <vector>

namespace {
	constexpr size_t kLeafThreshold = 4;

#pragma pack(push, 1)
	struct Tri2Header {
		char magic[4];
		uint32_t tri_count;
		uint32_t attr_count;
	};
#pragma pack(pop)

	bool LoadTri2(const std::string& path, std::vector<TriangleCombined>& tris, std::vector<float>& costs) {
		std::ifstream in(path, std::ios::binary);
		Tri2Header hdr{};
		if (!in.read(reinterpret_cast<char*>(&hdr), sizeof(hdr)) || std::memcmp(hdr.magic, "TRI2", 4))
			return false;

		costs.resize(hdr.attr_count);
		in.read(reinterpret_cast<char*>(costs.data()), hdr.attr_count * sizeof(float));

		tris.resize(hdr.tri_count);
		for (auto& tri : tris) {
			in.read(reinterpret_cast<char*>(&tri.v0), sizeof(Vector3) * 3);
			uint16_t pad{};
			in.read(reinterpret_cast<char*>(&tri.attr), sizeof(tri.attr));
			in.read(reinterpret_cast<char*>(&pad), sizeof(pad));
		}
		return in.good();
	}
}

VisCheck::VisCheck(const std::string& triFile) {
	std::vector<TriangleCombined> tris;
	if (!LoadTri2(triFile, tris, penCosts_))
		return;

	triCount_ = tris.size();
	root_ = BuildBVH(tris);
}

std::unique_ptr<BVHNode> VisCheck::BuildBVH(const std::vector<TriangleCombined>& tris) {
	auto node = std::make_unique<BVHNode>();
	if (tris.empty())
		return node;

	AABB bounds = tris[0].ComputeAABB();
	for (size_t i = 1; i < tris.size(); ++i) {
		const auto a = tris[i].ComputeAABB();
		bounds.min.x = std::min(bounds.min.x, a.min.x);
		bounds.min.y = std::min(bounds.min.y, a.min.y);
		bounds.min.z = std::min(bounds.min.z, a.min.z);
		bounds.max.x = std::max(bounds.max.x, a.max.x);
		bounds.max.y = std::max(bounds.max.y, a.max.y);
		bounds.max.z = std::max(bounds.max.z, a.max.z);
	}
	node->bounds = bounds;

	if (tris.size() <= kLeafThreshold) {
		node->triangles = tris;
		return node;
	}

	const Vector3 diff = bounds.max - bounds.min;
	const int axis = (diff.x > diff.y && diff.x > diff.z) ? 0 : (diff.y > diff.z ? 1 : 2);
	auto sorted = tris;
	std::sort(sorted.begin(), sorted.end(), [axis](const TriangleCombined& a, const TriangleCombined& b) {
		const auto aa = a.ComputeAABB(), ab = b.ComputeAABB();
		const float ca = axis == 0 ? (aa.min.x + aa.max.x) : axis == 1 ? (aa.min.y + aa.max.y) : (aa.min.z + aa.max.z);
		const float cb = axis == 0 ? (ab.min.x + ab.max.x) : axis == 1 ? (ab.min.y + ab.max.y) : (ab.min.z + ab.max.z);
		return ca < cb;
	});

	const size_t mid = sorted.size() / 2;
	node->left = BuildBVH({ sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(mid) });
	node->right = BuildBVH({ sorted.begin() + static_cast<std::ptrdiff_t>(mid), sorted.end() });
	return node;
}

void VisCheck::CollectHits(const BVHNode* node, const Vector3& origin, const Vector3& dir, float maxDist, std::vector<std::pair<float, uint16_t>>& out) const {
	if (!node->bounds.RayIntersects(origin, dir))
		return;

	if (node->IsLeaf()) {
		for (const auto& tri : node->triangles) {
			float t;
			if (RayTriangle(origin, dir, tri, t) && t > 1e-4f && t < maxDist)
				out.emplace_back(t, tri.attr);
		}
		return;
	}

	if (node->left)
		CollectHits(node->left.get(), origin, dir, maxDist, out);
	if (node->right)
		CollectHits(node->right.get(), origin, dir, maxDist, out);
}

float VisCheck::MatCost(uint16_t attr) const {
	if (attr >= penCosts_.size())
		return 220.f;
	const float c = penCosts_[attr];
	return c >= 900.f ? 999.f : c;
}

float VisCheck::SurfaceCost(uint16_t entry, uint16_t exit) const {
	return entry == exit ? MatCost(entry) : (MatCost(entry) + MatCost(exit)) * 0.5f;
}

bool VisCheck::Trace(const Vector3& from, const Vector3& to, float weaponPen) const {
	if (!root_)
		return false;

	const Vector3 delta = to - from;
	const float dist = std::sqrt(delta.dot(delta));
	if (dist < 1.f)
		return true;

	const Vector3 dir = { delta.x / dist, delta.y / dist, delta.z / dist };

	const float pad = std::min(16.f, dist * 0.08f);
	const Vector3 origin = { from.x + dir.x * pad, from.y + dir.y * pad, from.z + dir.z * pad };
	const float rayLen = dist - pad;
	if (rayLen < 1.f)
		return true;

	std::vector<std::pair<float, uint16_t>> hits;
	CollectHits(root_.get(), origin, dir, rayLen, hits);
	if (hits.empty())
		return weaponPen <= 0.f;

	std::sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

	std::vector<std::pair<float, uint16_t>> merged;
	merged.reserve(hits.size());
	for (const auto& h : hits) {
		if (!merged.empty() && h.first - merged.back().first < 1.5f)
			continue;
		merged.push_back(h);
	}

	constexpr float kNearTarget = 8.f;
	std::vector<std::pair<float, uint16_t>> block;
	block.reserve(merged.size());
	for (const auto& h : merged) {
		if (h.first < rayLen - kNearTarget)
			block.push_back(h);
	}

	if (block.empty())
		return weaponPen <= 0.f;

	if (weaponPen <= 0.f)
		return false;

	if (block.size() >= 2 && block.back().first - block.front().first > 42.f)
		return false;

	if (MatCost(block.front().second) >= 250.f || MatCost(block.back().second) >= 250.f)
		return false;

	float budget = weaponPen;
	for (size_t i = 0; i < block.size();) {
		if (i + 1 >= block.size()) {
			if (block.size() != 1 || block.back().first - block.front().first > 22.f)
				return false;
			const float slab = SurfaceCost(block[i].second, block[i].second);
			return MatCost(block[i].second) <= 85.f && slab < 95.f && slab <= budget;
		}

		const float cost = SurfaceCost(block[i].second, block[i + 1].second);
		if (cost >= 900.f || cost > budget)
			return false;

		budget -= cost;
		i += 2;
	}
	return true;
}

bool VisCheck::Visible(const Vector3& from, const Vector3& to, float weaponPen) const {
	if (!Trace(from, to, weaponPen))
		return false;

	const float dx = to.x - from.x, dy = to.y - from.y;
	if (dx * dx + dy * dy < 90.f)
		return true;

	const float drop = std::min(36.f, std::max(0.f, from.z - to.z) * 0.45f);
	const Vector3 flat = { to.x, to.y, from.z - drop };
	return Trace(from, flat, weaponPen);
}

bool VisCheck::RayTriangle(const Vector3& origin, const Vector3& dir, const TriangleCombined& tri, float& t) {
	const Vector3 e1 = tri.v1 - tri.v0, e2 = tri.v2 - tri.v0;
	const Vector3 h = dir.cross(e2);
	const float a = e1.dot(h);
	if (a > -1e-7f && a < 1e-7f)
		return false;

	const float f = 1.f / a;
	const Vector3 s = origin - tri.v0;
	const float u = f * s.dot(h);
	if (u < 0.f || u > 1.f)
		return false;

	const Vector3 q = s.cross(e1);
	const float v = f * dir.dot(q);
	if (v < 0.f || u + v > 1.f)
		return false;

	t = f * e2.dot(q);
	return t > 1e-7f;
}
