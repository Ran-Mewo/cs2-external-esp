#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Math.hpp"

struct BVHNode {
	AABB bounds;
	std::unique_ptr<BVHNode> left;
	std::unique_ptr<BVHNode> right;
	std::vector<TriangleCombined> triangles;

	bool IsLeaf() const { return !left && !right; }
};

class VisCheck {
public:
	explicit VisCheck(const std::string& triFile);

	bool IsReady() const { return root_ != nullptr; }
	size_t TriangleCount() const { return triCount_; }
	bool Visible(const Vector3& from, const Vector3& to, float weaponPen) const;

private:
	std::unique_ptr<BVHNode> root_;
	std::vector<float> penCosts_;
	size_t triCount_{};

	std::unique_ptr<BVHNode> BuildBVH(const std::vector<TriangleCombined>& tris);
	void CollectHits(const BVHNode* node, const Vector3& origin, const Vector3& dir, float maxDist, std::vector<std::pair<float, uint16_t>>& out) const;
	float MatCost(uint16_t attr) const;
	float SurfaceCost(uint16_t entry, uint16_t exit) const;
	bool Trace(const Vector3& from, const Vector3& to, float weaponPen) const;
	static bool RayTriangle(const Vector3& origin, const Vector3& dir, const TriangleCombined& tri, float& t);
};
