#include "Primitive.h"
#include "threading.hpp"
#include "RenderLog.h"

#include <algorithm>
#include <functional>

const int maxPrimsInNode = 2;

struct OctTree : IntersectionAccelerator {
	struct Node {
		BBox box;
		Node *children[8] = {nullptr, };
		std::vector<Intersectable*> primitives;
		bool isLeaf() const {
			return children[0] == nullptr;
		}
	};

	std::vector<Intersectable*> allPrimitives;
	Node *root = nullptr;
	int depth = 0;
	int leafSize = 0;
	int nodes = 0;
	int MAX_DEPTH = 35;
	int MIN_PRIMITIVES = 10;

	void clear(Node *n) {
		if (!n) {
			return;
		}

		for (int c = 0; c < 8; c++) {
			clear(n->children[c]);
			delete n->children[c];
		}
	}

	void clear() {
		clear(root);
		allPrimitives.clear();
	}

	void addPrimitive(Intersectable* prim) override {
		allPrimitives.push_back(prim);
	}

	void build(Node *n, int currentDepth = 0) {
		if (currentDepth >= MAX_DEPTH || n->primitives.size() <= MIN_PRIMITIVES) {
			leafSize = std::max(leafSize, int(n->primitives.size()));
			return;
		}

		depth = std::max(depth, currentDepth);
		BBox childBoxes[8];
		n->box.octSplit(childBoxes);

		for (int c = 0; c < 8; c++) {
			Node *& child = n->children[c];
			child = new Node;
			nodes++;
			memset(child->children, 0, sizeof(child->children));
			child->box = childBoxes[c];
			for (int r = 0; r < n->primitives.size(); r++) {
				if (n->primitives[r]->boxIntersect(child->box)) {
					child->primitives.push_back(n->primitives[r]);
				}
			}
			if (child->primitives.size() == n->primitives.size()) {
				build(child, MAX_DEPTH + 1);
			} else {
				build(child, currentDepth + 1);
			}
		}
		n->primitives.clear();
	}

	void build(Purpose purpose) override {
		const char *treePurpose = "";
		if (purpose == Purpose::Instances) {
			MAX_DEPTH = 5;
			MIN_PRIMITIVES = 4;
			treePurpose = " instances";
		} else if (purpose == Purpose::Mesh) {
			MAX_DEPTH = 35;
			MIN_PRIMITIVES = 20;
			treePurpose = " mesh";
		}

		if (root) {
			clear(root);
			delete root;
		}

		printf("Building%s oct tree with %d primitives... ", treePurpose, int(allPrimitives.size()));
		Timer timer;
		nodes = leafSize = depth = 0;
		root = new Node();
		root->primitives.swap(allPrimitives);
		for (int c = 0; c < root->primitives.size(); c++) {
			root->primitives[c]->expandBox(root->box);
		}
		build(root);
		LOG_ACCEL_BUILD(AcceleratorType::Octtree, timer.toMs<float>(timer.elapsedNs() / 1000.0f), nodes, nodes * sizeof(Node) + sizeof(*this) + allPrimitives.size() * sizeof(allPrimitives[0]));
		allPrimitives.clear();
		printf(" done in %lldms, nodes %d, depth %d, %d leaf size\n", timer.toMs(timer.elapsedNs()), nodes, depth, leafSize);
	}

	bool intersect(Node *n, const Ray& ray, float tMin, float &tMax, Intersection& intersection) {
		bool hasHit = false;

		if (n->isLeaf()) {
			for (int c = 0; c < n->primitives.size(); c++) {
				if (n->primitives[c]->intersect(ray, tMin, tMax, intersection)) {
					tMax = intersection.t;
					hasHit = true;
				}
			}
		} else {
			for (int c = 0; c < 8; c++) {
				if (n->children[c]->box.testIntersect(ray)) {
					if (intersect(n->children[c], ray, tMin, tMax, intersection)) {
						tMax = intersection.t;
						hasHit = true;
					}
				}
			}
		}

		return hasHit;
	}

	bool intersect(const Ray& ray, float tMin, float tMax, Intersection& intersection) override {
		return intersect(root, ray, tMin, tMax, intersection);
	}

	bool isBuilt() const override {
		return root != nullptr;
	}

	~OctTree() override {
		clear();
	}
};

struct KDTree : IntersectionAccelerator {
	void addPrimitive(Intersectable *prim) override {}
	void clear() override {}
	void build(Purpose purpose) override {}
	bool isBuilt() const override { return false; }
	bool intersect(const Ray &ray, float tMin, float tMax, Intersection &intersection) override { return false; }
};

// HLBVH
struct BVHTree : IntersectionAccelerator {

	struct PrimitiveInfo
	{
		PrimitiveInfo(size_t idx, BBox bounds) : primitiveIdx(idx), boundingBox(bounds), centroid(.5f * bounds.min + .5f * bounds.max)
		{
		}
		size_t primitiveIdx;
		BBox boundingBox;
		vec3 centroid;
	};

	std::vector<PrimitiveInfo> allPrimitives;
	std::vector<Intersectable*> orderedPrims;
	std::vector<Intersectable*> primitives;
	
	struct BVHBuildNode
	{
		void InitLeaf(int first, int n, const BBox& b)
		{
			firstPrimOffset = first;
			primitiveCount = n;
			bounds = b;
			children[0] = children[1] = nullptr;
		}

		void InitInterior(int axis, BVHBuildNode* child1, BVHBuildNode* child2)
		{
			bounds.add(child1->bounds);
			bounds.add(child2->bounds);
			splitAxis = axis;
			primitiveCount = 0;
			children[0] = child1;
			children[1] = child2;
		}

		int primitiveCount, firstPrimOffset, splitAxis;
		BBox bounds;
		BVHBuildNode* children[2];
	};

	struct LBVHTreelet
	{
		int startIdx, primitiveCount;
		BVHBuildNode* buildNodes;
	};

	struct MortonPrimitive {
		int primitiveIndex;
		uint64_t mortonCode;
	};

	uint32_t idx = 0;

	~BVHTree()
	{
		clear();
	}

	void addPrimitive(Intersectable *prim) override
	{
		BBox box;
		prim->expandBox(box);
		allPrimitives.push_back({ idx++, box });
		primitives.push_back(prim);
	}

	void clear() override
	{
		delete[] nodes;
		nodes = nullptr;
	}

	uint64_t WeirdShift(uint64_t x) // pbr book, but this is with 64 bits
	{
		x = (x | (x << 32)) & 0x001f00000000ffff;  // 0000000000011111000000000000000000000000000000001111111111111111
		x = (x | (x << 16)) & 0x001f0000ff0000ff;  // 0000000000011111000000000000000011111111000000000000000011111111
		x = (x | (x <<  8)) &  0x100f00f00f00f00f; // 0001000000001111000000001111000000001111000000001111000000000000
		x = (x | (x <<  4)) &  0x10c30c30c30c30c3; // 0001000011000011000011000011000011000011000011000011000100000000
		x = (x | (x <<  2)) &  0x1249249249249249; // 0001001001001001001001001001001001001001001001001001001001001001
		return x;
	}

	uint64_t EncodeMorton3(const vec3& val)
	{
		return (WeirdShift(val.z) << 2) | (WeirdShift(val.y) << 1) | WeirdShift(val.x);
	}


	struct LinearBVHNode
	{
		BBox bounds;
		union
		{
			int primitivesOffset;
			int secondChildOffset;
		};

		uint16_t primitiveCount;
		uint8_t axis; // for interior nodes
		uint8_t pad[1]; // padding
	};

	LinearBVHNode* nodes = nullptr;

	void build(Purpose purpose) override
	{
		Timer timer;
		printf("Building %s BVH with %d primitives\n", purpose == Purpose::Instances ? "instancing" : "mesh", (int)allPrimitives.size());
		BBox bounds;
		for (const auto& prim : allPrimitives) // Bounding box of all primitives
			bounds.add(prim.centroid);
		
		std::vector<MortonPrimitive> mortonPrims;
		mortonPrims.resize(allPrimitives.size());
		const int mortonBits = 21; // so we can use 21 bits for each axis with int = 3x21 63
		const int mortonScale = 1 << mortonBits; // Multiply by 2^21 since I can fit 21 bits in the morton thing
		for (int i = 0; i < allPrimitives.size(); i++) // pbr book does this in parallel
		{
			mortonPrims[i].primitiveIndex = allPrimitives[i].primitiveIdx;
			vec3 centroidOffset = bounds.offset(allPrimitives[i].centroid);
			mortonPrims[i].mortonCode = EncodeMorton3(centroidOffset * mortonScale);
		}

		std::sort(mortonPrims.begin(), mortonPrims.end(), [](const MortonPrimitive& l, const MortonPrimitive& r){ return l.mortonCode < r.mortonCode; }); // pbr book uses radix sort here

		std::vector<LBVHTreelet> treeletsToBuild;
		int start = 0;
		for (int end = 1; end < (int)mortonPrims.size(); end++)
		{
			uint64_t mask = 0x3ffc000000000000; // top 12 bits, divide them into groups whose top 12 bits match
			if ((mortonPrims[start].mortonCode & mask) != (mortonPrims[end].mortonCode & mask))
			{
				int primitiveCount = end - start;
				int maxBVHNodes = 2 * primitiveCount;
				BVHBuildNode* nodes = new BVHBuildNode[maxBVHNodes];
				treeletsToBuild.push_back({ start, primitiveCount, nodes });
				start = end;
			}
		}

		int primitiveCount = mortonPrims.size() - start;
		int maxBVHNodes = 2 * primitiveCount;
		treeletsToBuild.push_back({ start, primitiveCount, new BVHBuildNode[maxBVHNodes] });

		// Could also do this in parallel
		int orderedPrimsOffset = 0;
		int totalNodes = 0;
		const int firstBitIndex = 62 - 12;
		orderedPrims.resize(allPrimitives.size());
		for (int i = 0; i < treeletsToBuild.size(); i++)
		{
			int nodesCreated = 0;
			LBVHTreelet& treelet = treeletsToBuild[i];
			treelet.buildNodes = emitLBVH(treelet.buildNodes, &mortonPrims[treelet.startIdx], treelet.primitiveCount, nodesCreated, orderedPrimsOffset, firstBitIndex);
			totalNodes += nodesCreated;
		}
		std::vector<BVHBuildNode*> finishedTreelets; // Create the reset of the tree using SAH
		finishedTreelets.reserve(treeletsToBuild.size());
		for (LBVHTreelet& treelet : treeletsToBuild)
			finishedTreelets.push_back(treelet.buildNodes);
		BVHBuildNode* root = buildUpperSAH(finishedTreelets, 0, finishedTreelets.size(), totalNodes);
		nodes = new LinearBVHNode[totalNodes];
		primitives.swap(orderedPrims);
		allPrimitives.clear();

		std::function<void(BVHBuildNode*, uint32_t)> printBVH = [&](BVHBuildNode* node, uint32_t tabs) {
			for (int i = 0; i < tabs; i++)
				printf("\t");
			if (node->children[0] != nullptr)
				printf("Interior: %f, %f, %f, %f, %f, %f\n", node->bounds.min.x, node->bounds.min.y, node->bounds.min.z, node->bounds.max.x, node->bounds.max.y, node->bounds.max.z);
			else
				printf("Leaf: %f, %f, %f, %f, %f, %f\n", node->bounds.min.x, node->bounds.min.y, node->bounds.min.z, node->bounds.max.x, node->bounds.max.y, node->bounds.max.z);
			if (node->children[0] != nullptr)
				printBVH(node->children[0], tabs + 1);
			if (node->children[1] != nullptr)
				printBVH(node->children[1], tabs + 1);
		};
		// printBVH(root, 0);
		int32_t offset = 0;
		flatten(root, offset);
		LOG_ACCEL_BUILD(AcceleratorType::BVH, timer.toMs<float>(timer.elapsedNs() / 1000.0f), totalNodes, totalNodes * sizeof(LinearBVHNode) + sizeof(*this) + sizeof(primitives[0]) * primitives.size());
		printf("Built BVH with %d nodes in %f seconds\n", totalNodes, Timer::toMs<float>(timer.elapsedNs()) / 1000.0f);
	}

	BVHBuildNode* emitLBVH(BVHBuildNode *&buildNodes, MortonPrimitive* mortonPrims, int primitiveCount, int& totalNodes, int& orderedPrimsOffset, int bitIdx)
	{
		if (bitIdx == -1 || primitiveCount < maxPrimsInNode) // We can create a leaf
		{
			totalNodes++;
			BVHBuildNode* node = buildNodes++;
			BBox bounds;
			int firstPrimOffset = orderedPrimsOffset;
			orderedPrimsOffset += primitiveCount;
			for (int i = 0; i < primitiveCount; i++)
			{
				int primitiveIdx = mortonPrims[i].primitiveIndex;
				orderedPrims[firstPrimOffset + i] = primitives[primitiveIdx];
				bounds.add(allPrimitives[primitiveIdx].boundingBox);
			}
			node->InitLeaf(firstPrimOffset, primitiveCount, bounds);
			return node;
		}
		else // Create an internal node with two children
		{
			int mask = 1 << bitIdx;
			if ((mortonPrims[0].mortonCode & mask) == ((mortonPrims[primitiveCount - 1].mortonCode & mask)))
				return emitLBVH(buildNodes, mortonPrims, primitiveCount, totalNodes, orderedPrimsOffset, bitIdx - 1);
			int l = 0, r = primitiveCount - 1;
			while (l + 1 != r) // binary search for region
			{
				int mid = (l + r) / 2;
				if ((mortonPrims[l].mortonCode & mask) == (mortonPrims[mid].mortonCode & mask)) // search for region with matching bits
					l = mid;
				else
					r = mid;
			}
			int splitOffset = r;
			totalNodes++;
			BVHBuildNode* node = buildNodes++;
			BVHBuildNode* lbvh[2] = { emitLBVH(buildNodes, mortonPrims, splitOffset, totalNodes, orderedPrimsOffset, bitIdx - 1), emitLBVH(buildNodes, &mortonPrims[splitOffset], primitiveCount - splitOffset, totalNodes, orderedPrimsOffset, bitIdx - 1)};
			int axis = bitIdx % 3;
			node->InitInterior(axis, lbvh[0], lbvh[1]);
			return node;
		}
	}

	BVHBuildNode* buildUpperSAH(std::vector<BVHBuildNode*>& roots, int start, int end, int& totalNodes) const;

	bool isBuilt() const override { return nodes != nullptr; }

	int flatten(BVHBuildNode* node, int& offset)
	{
		LinearBVHNode* linearNode = &nodes[offset];
		linearNode->bounds = node->bounds;
		int myOffset = offset++;
		if (node->primitiveCount > 0)
		{
			linearNode->primitivesOffset = node->firstPrimOffset;
			linearNode->primitiveCount = node->primitiveCount;
		}
		else
		{
			linearNode->axis = node->splitAxis;
			linearNode->primitiveCount = 0;
			flatten(node->children[0], offset);
			linearNode->secondChildOffset = flatten(node->children[1], offset);
		}
		return myOffset;
	}

	bool intersect(const Ray& ray, float tMin, float tMax, Intersection& intersection) override
	{
		if (!isBuilt())
			return false;
		
		vec3 invDir = ray.dir.inverted();
		int negativeDir[3] = { invDir.x < 0, invDir.y < 0, invDir.z < 0 };
			// Offset of next element in stack, offset in nodes list
		int toVisitOffset = 0,                  currentNodeIndex = 0;
		int nodesToVisit[64];
		bool hit = false;
		while (true)
		{
			const LinearBVHNode* node = &nodes[currentNodeIndex];
			if (node->bounds.testIntersect(ray))
			{
				if (node->primitiveCount > 0) // leaf
				{
					for (int i = 0; i < node->primitiveCount; i++)
					{
						if (primitives[node->primitivesOffset + i]->intersect(ray, tMin, tMax, intersection))
						{
							// return true;
							hit = true; // Need to keep going, since there might be closer intersections, so just update
							tMax = intersection.t;
						}
					}
			
					if (toVisitOffset == 0) break;
					currentNodeIndex = nodesToVisit[--toVisitOffset];
				}
				else // interior, so visit child
				{
					if (negativeDir[node->axis]) // if the axis we split on has negative direction, visit the second child. In 2D this is:
						/* Let's say we split on the x axis. Then we want to visit the second child if the ray is going from left to right, and the first child if the ray is going from right to left.
						This way we can easily discard the second child, since it would hit the box on the left and it's closer

				   *
					\
					 \
					  \
					   >

						---------   |
						|       |   |
						|       |   |
						---------   |     ---------
									|     |       |
									|     |       |
									|     ---------
									| */
					{
						nodesToVisit[toVisitOffset++] = currentNodeIndex + 1;
						currentNodeIndex = node->secondChildOffset;
					}
					else
					{
						nodesToVisit[toVisitOffset++] = node->secondChildOffset;
						currentNodeIndex++;
					}
				}
			}
			else
			{
				if (toVisitOffset == 0)
					break;
				currentNodeIndex = nodesToVisit[--toVisitOffset];
			}
		}
		return hit;
	}

};

BVHTree::BVHBuildNode* BVHTree::buildUpperSAH(std::vector<BVHBuildNode*>& roots, int start, int end, int& totalNodes) const
{
	int nodeCount = end - start;
	if (nodeCount== 1) return roots[start];
	totalNodes++;
	BVHBuildNode* node = new BVHBuildNode();
	BBox bounds;
	for (int i = start; i < end; i++)
		bounds.add(roots[i]->bounds);

	// Only considering the centroids of objects. With these scenes the objects are pretty much the same size, but with differently sized objects this wouldn't work as well
	BBox centroidBounds;
	for (int i = start; i < end; i++)
	{
		vec3 centroid = (roots[i]->bounds.min + roots[i]->bounds.max) * 0.5f;
		centroidBounds.add(centroid);
	}
	int dim = centroidBounds.maxExtent(); // Divide on largest axis, Maybe worth checking all 3?
	const int bucketCount = 12; // Put everything in buckets and try to cut between the buckets. Choose the one with the best cost
	struct BucketInfo {
		int count = 0;
		BBox bounds;
	};
	BucketInfo buckets[16];
	for (int i = start; i < end; i++) // Put into buckets
	{
		float centroid = (roots[i]->bounds.min[dim] + roots[i]->bounds.max[dim]) * 0.5f;
		int b = bucketCount * (((centroid - centroidBounds.min[dim]) / (centroidBounds.max[dim] - centroidBounds.min[dim])));
		if (b >= bucketCount)
			b = bucketCount - 1;
		buckets[b].count++;
		buckets[b].bounds.add(roots[i]->bounds);
		// printf("%f, %d %d\n", centroid, b);
	}

	const float traversalCost = 0.125f; // cost of figuring out which child to visit
	const float intersectionCost = 1.0f; // cost of calculating intersection, since here it also goes through a few virtual calls it should be slower

	float cost[bucketCount - 1];
	// printf("Start: %d, end: %d, dim: %d\n", start, end, dim);
	// printf("Centroid: %f, %f, %f\n", centroidBounds.min.x, centroidBounds.min.y, centroidBounds.min.z);
	// printf("Centroid: %f, %f, %f\n", centroidBounds.max.x, centroidBounds.max.y, centroidBounds.max.z);
	for (int i = 0; i < bucketCount - 1; i++)
	{
		// printf("Bucket %d: %d\n", i, buckets[i].count);
		BBox b0, b1;
		int count0 = 0, count1 = 0;
		for (int j = 0; j <= i; j++)
		{
			b0.add(buckets[j].bounds);
			count0 += buckets[j].count;
		}
		for (int j = 0; j <= i; j++)
		{
			b1.add(buckets[j].bounds);
			count1 += buckets[j].count;
		}
		cost[i] = traversalCost + intersectionCost * (count0 * b0.area() + count1 * b1.area()) / bounds.area();
	}

	float minCost = cost[0];
	int minCostBucketIdx = 0;
	for (int i = 1; i < bucketCount - 1; i++)
	{
		if (cost[i] < minCost)
		{
			minCost = cost[i];
			minCostBucketIdx = i;
		}
	}

	// pbr book guys soo smart
	BVHBuildNode** pmid = std::partition(&roots[start], &roots[end - 1] + 1, [=](const BVHBuildNode* node)
		{
			float centroid = (node->bounds.min[dim] + node->bounds.max[dim]) * 0.5f;
			int b = bucketCount * (((centroid - centroidBounds.min[dim]) / (centroidBounds.max[dim] - centroidBounds.min[dim])));
			if (b >= bucketCount)
				b = bucketCount - 1;
			return b <= minCostBucketIdx;
		});
	int mid = pmid - &roots[0];
	node->InitInterior(dim, buildUpperSAH(roots, start, mid, totalNodes), buildUpperSAH(roots, mid, end, totalNodes));
	return node;
}

AcceleratorPtr makeAccelerator(AcceleratorType acceleratorType) {
	switch (acceleratorType)
	{
	case AcceleratorType::Octtree: return AcceleratorPtr(new OctTree());

	// ~3x faster in debug, ~5x in release
	case AcceleratorType::BVH: return AcceleratorPtr(new BVHTree());
	}
}

