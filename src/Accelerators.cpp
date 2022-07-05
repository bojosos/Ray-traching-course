#include "Primitive.h"
#include "threading.hpp"
#include "RenderLog.h"

#include <algorithm>
#include <functional>

#include <iostream>
#include <bitset>

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

// HLBVH
struct BVHTree : IntersectionAccelerator {

	struct PrimInfo
	{
		PrimInfo(size_t idx, BBox bounds) : primitiveIdx(idx), boundingBox(bounds), centroid(.5f * bounds.min + .5f * bounds.max)
		{
		}
		size_t primitiveIdx;
		BBox boundingBox;
		vec3 centroid;
	};

	struct Node
	{
		void initLeaf(int first, int n, const BBox& b)
		{
			firstPrimOffset = first;
			primitiveCount = n;
			bounds = b;
			children[0] = children[1] = nullptr;
		}

		void initInterior(int axis, Node* child1, Node* child2)
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
		Node* children[2];
	};

	struct Treelet
	{
		int startIdx, primitiveCount;
		Node* nodes;
	};

	struct MortonPrim {
		int primitiveIndex;
		uint64_t mortonCode;
	};

	struct LinearNode
	{
		BBox bounds;
		union
		{
			int primitivesOffset;
			int secondChildOffset;
		};

		uint16_t primitiveCount;
		uint8_t axis; // axis interior nodes were split on
		uint8_t pad[1]; // padding for 32b
	};

	std::vector<PrimInfo> m_Primitives;
	std::vector<Intersectable*> m_OrderedPrims;
	std::vector<Intersectable*> m_FinalPrims;
	LinearNode* m_SearchNodes = nullptr;
	uint32_t m_MaxPrimsPerNode = 1;
	float m_IntersectionCost = 1.0f; // cost of calculating intersection

	uint32_t m_PrimIdx = 0;

	~BVHTree()
	{
		clear();
	}

	void addPrimitive(Intersectable *prim) override
	{
		BBox box;
		prim->expandBox(box);
		m_Primitives.push_back({ m_PrimIdx++, box });
		m_FinalPrims.push_back(prim);
	}

	void clear() override
	{
		delete[] m_SearchNodes;
		m_SearchNodes = nullptr;
	}

	uint64_t weirdShift(uint64_t x) // pbr book, but this is with 64 bits
	{
		x = (x | (x << 32)) & 0x001f00000000ffff; // 0000000000011111000000000000000000000000000000001111111111111111
		x = (x | (x << 16)) & 0x001f0000ff0000ff; // 0000000000011111000000000000000011111111000000000000000011111111
		x = (x | (x <<  8)) & 0x100f00f00f00f00f; // 0001000000001111000000001111000000001111000000001111000000000000
		x = (x | (x <<  4)) & 0x10c30c30c30c30c3; // 0001000011000011000011000011000011000011000011000011000100000000
		x = (x | (x <<  2)) & 0x1249249249249249; // 0001001001001001001001001001001001001001001001001001001001001001
		return x;
	}

	uint64_t encodeMorton3(const vec3& val)
	{
		return (weirdShift(val.z) << 2) | (weirdShift(val.y) << 1) | weirdShift(val.x);
	}

	void build(Purpose purpose) override
	{
		if (purpose == Purpose::Instances) // maybe makes a difference?
		{
			m_MaxPrimsPerNode = 1;
			m_IntersectionCost = 2.0f
		}
		else
		{
			m_MaxPrimsPerNode = 4;
			m_IntersectionCost = 1.0f
		}
		Timer timer;
		printf("Building %s BVH with %d primitives\n", purpose == Purpose::Instances ? "instancing" : "mesh", (int)m_Primitives.size());
		BBox bounds;
		for (const auto& prim : m_Primitives) // Bounding box of all primitives
			bounds.add(prim.centroid);
		
		std::vector<MortonPrim> mortonPrims;
		mortonPrims.resize(m_Primitives.size());
		const int mortonBits = 21; // so we can use 21 bits for each axis with int = 3x21 63
		const int mortonScale = 1 << mortonBits; // Multiply by 2^21 since I can fit 21 bits in the morton thing
		for (int i = 0; i < m_Primitives.size(); i++) // pbr book does this in parallel
		{
			mortonPrims[i].primitiveIndex = m_Primitives[i].primitiveIdx;
			vec3 centroidOffset = bounds.offset(m_Primitives[i].centroid);
			mortonPrims[i].mortonCode = encodeMorton3(centroidOffset * mortonScale);
		}

		std::sort(mortonPrims.begin(), mortonPrims.end(), [](const MortonPrim& l, const MortonPrim& r){ return l.mortonCode < r.mortonCode; }); // pbr book uses radix sort here

		std::vector<Treelet> treeletsToBuild;
		int start = 0;
		for (int end = 1; end < (int)mortonPrims.size(); end++)
		{
			uint64_t mask = 0x3ffc000000000000; // top 12 bits, divide them into groups whose top 12 bits match
			if ((mortonPrims[start].mortonCode & mask) != (mortonPrims[end].mortonCode & mask))
			{
				int primitiveCount = end - start;
				int maxBVHNodes = 2 * primitiveCount;
				Node* nodes = new Node[maxBVHNodes];
				treeletsToBuild.push_back({ start, primitiveCount, nodes });
				start = end;
			}
		}

		printf("%lld treelets", treeletsToBuild.size() + 1);

		int primitiveCount = mortonPrims.size() - start;
		int maxBVHNodes = 2 * primitiveCount;
		treeletsToBuild.push_back({ start, primitiveCount, new Node[maxBVHNodes] });

		// Could also do this in parallel
		int orderedPrimsOffset = 0;
		int totalNodes = 0;
		const int firstBitIndex = 62 - 12;
		m_OrderedPrims.resize(m_Primitives.size());
		for (int i = 0; i < treeletsToBuild.size(); i++)
			treeletsToBuild[i].nodes = buildTreelets(treeletsToBuild[i].nodes, &mortonPrims[treeletsToBuild[i].startIdx], treeletsToBuild[i].primitiveCount, totalNodes, orderedPrimsOffset, firstBitIndex);

		std::vector<Node*> finishedTreelets; // Create the rest of the tree using SAH
		finishedTreelets.reserve(treeletsToBuild.size());
		for (Treelet& treelet : treeletsToBuild)
			finishedTreelets.push_back(treelet.nodes);
		Node* root = connectTreelets(finishedTreelets, 0, finishedTreelets.size(), totalNodes);
		m_SearchNodes = new LinearNode[totalNodes];
		m_FinalPrims.swap(m_OrderedPrims);
		m_Primitives.clear();

		std::function<void(Node*, const std::string&, bool)> printBVH = [&](Node* node, const std::string& prefix, bool isLeft) {
			printf("%s", prefix.c_str());
			printf(isLeft ? "|--" : "L--");
			if (node->children[0] != nullptr)
				printf("Interior: %f, %f, %f, %f, %f, %f\n", node->bounds.min.x, node->bounds.min.y, node->bounds.min.z, node->bounds.max.x, node->bounds.max.y, node->bounds.max.z);
			else
				printf("Leaf: %f, %f, %f, %f, %f, %f\n", node->bounds.min.x, node->bounds.min.y, node->bounds.min.z, node->bounds.max.x, node->bounds.max.y, node->bounds.max.z);
			if (node->children[0] != nullptr)
				printBVH(node->children[0], prefix + (isLeft ? "|   " : "    "), true);
			if (node->children[1] != nullptr)
				printBVH(node->children[1], prefix + (isLeft ? "|   " : "    "), false);
		};
		// printBVH(root, "", false);
		int32_t offset = 0;
		flatten(root, offset); // pbr book
		LOG_ACCEL_BUILD(AcceleratorType::BVH, timer.toMs<float>(timer.elapsedNs() / 1000.0f), totalNodes, totalNodes * sizeof(LinearNode) + sizeof(*this) + sizeof(m_FinalPrims[0]) * m_FinalPrims.size());
		printf("Built BVH with %d nodes in %f seconds\n", totalNodes, Timer::toMs<float>(timer.elapsedNs()) / 1000.0f);
	}

	Node* buildTreelets(Node *&buildNodes, MortonPrim* mortonPrims, int primitiveCount, int& totalNodes, int& orderedPrimsOffset, int bitIdx)
	{
		if (bitIdx == -1 || primitiveCount < m_MaxPrimsPerNode) // We need to create a leaf, either because we can fit the nodes left in a single leaf, or because we can't split
		{
			totalNodes++;
			Node* node = buildNodes++;
			BBox bounds;
			int firstPrimOffset = orderedPrimsOffset;
			orderedPrimsOffset += primitiveCount;
			for (int i = 0; i < primitiveCount; i++)
			{
				int primitiveIdx = mortonPrims[i].primitiveIndex;
				m_OrderedPrims[firstPrimOffset + i] = m_FinalPrims[primitiveIdx];
				bounds.add(m_Primitives[primitiveIdx].boundingBox);
			}
			node->initLeaf(firstPrimOffset, primitiveCount, bounds);
			return node;
		}
		else // Create an internal node with two children
		{
			int mask = 1 << bitIdx;
			if ((mortonPrims[0].mortonCode & mask) == ((mortonPrims[primitiveCount - 1].mortonCode & mask))) // Check if all nodes are on the same side of the plane
				return buildTreelets(buildNodes, mortonPrims, primitiveCount, totalNodes, orderedPrimsOffset, bitIdx - 1);
			int l = 0, r = primitiveCount - 1;
			while (l + 1 != r) // binary search for region where bitIdx bit goes from 0 to 1
			{
				int mid = (l + r) / 2;
				// std::cout << std::bitset<32>(mortonPrims[l].mortonCode).to_string() << std::endl;
				// std::cout << std::bitset<32>(mortonPrims[mid].mortonCode).to_string() << std::endl;
				if ((mortonPrims[l].mortonCode & mask) == (mortonPrims[mid].mortonCode & mask))
					l = mid;
				else
					r = mid;
			}
			int splitOffset = r; // Primitives are already on correct sides of plane
			totalNodes++;
			Node* node = buildNodes++;
			Node* node1 = buildTreelets(buildNodes, mortonPrims, splitOffset, totalNodes, orderedPrimsOffset, bitIdx - 1);
			Node* node2 = buildTreelets(buildNodes, &mortonPrims[splitOffset], primitiveCount - splitOffset, totalNodes, orderedPrimsOffset, bitIdx - 1);
			int axis = bitIdx % 3;
			node->initInterior(axis, node1, node2);
			return node;
		}
	}

	Node* connectTreelets(std::vector<Node*>& roots, int start, int end, int& totalNodes) const;

	bool isBuilt() const override { return m_SearchNodes != nullptr; }

	int flatten(Node* node, int& offset)
	{
		// Store the tree in dfs parent left right order
		LinearNode* linearNode = &m_SearchNodes[offset];
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
		int toVisitOffset = 0, currentNodeIndex = 0;
		int nodesToVisit[64];
		bool hit = false;
		while (true)
		{
			const LinearNode* node = &m_SearchNodes[currentNodeIndex];
			if (node->bounds.testIntersect(ray))
			{
				if (node->primitiveCount > 0) // leaf
				{
					for (int i = 0; i < node->primitiveCount; i++)
					{
						if (m_FinalPrims[node->primitivesOffset + i]->intersect(ray, tMin, tMax, intersection))
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
					{
						/*
						Let's say we split on the x axis. Then we want to visit the second child first if the ray is going from right to left, and the first child if the ray is going from left to right.
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
									| 
						*/
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

BVHTree::Node* BVHTree::connectTreelets(std::vector<Node*>& roots, int start, int end, int& totalNodes) const
{
	int nodeCount = end - start;
	if (nodeCount== 1) return roots[start];
	totalNodes++;
	Node* node = new Node();
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
		cost[i] = traversalCost + m_IntersectionCost * (count0 * b0.area() + count1 * b1.area()) / bounds.area();
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
	Node** pmid = std::partition(&roots[start], &roots[end - 1] + 1, [=](const Node* node)
		{
			float centroid = (node->bounds.min[dim] + node->bounds.max[dim]) * 0.5f;
			int b = bucketCount * (((centroid - centroidBounds.min[dim]) / (centroidBounds.max[dim] - centroidBounds.min[dim])));
			if (b >= bucketCount)
				b = bucketCount - 1;
			return b <= minCostBucketIdx;
		});
	int mid = pmid - &roots[0];
	node->initInterior(dim, connectTreelets(roots, start, mid, totalNodes), connectTreelets(roots, mid, end, totalNodes));
	return node;
#if 0
	const int bucketCount = 12; // Put everything in buckets and try to cut between the buckets. Choose the one with the best cost
	float cost[3][bucketCount - 1];
	for (uint32_t dim = 0; dim < 3; dim++)
	{
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
			if (b < 0)
				b = 0;
			buckets[b].count++;
			buckets[b].bounds.add(roots[i]->bounds);
			// printf("%f, %d %d\n", centroid, b);
		}

		const float traversalCost = 0.125f; // cost of figuring out which child to visit
		const float intersectionCost = 1.0f; // cost of calculating intersection, since here it also goes through a few virtual calls it should be slower
		
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
			cost[dim][i] = traversalCost + intersectionCost * (count0 * b0.area() + count1 * b1.area()) / bounds.area();
		}

	}
	uint32_t minDim = 0;
	float minCost = cost[0][0];
	int minCostBucketIdx = 0;
	for (uint32_t d = 0; d < 3; d++)
	{
		for (int i = 1; i < bucketCount - 1; i++)
		{
			if (cost[d][i] < minCost)
			{
				minCost = cost[d][i];
				minDim = d;
				minCostBucketIdx = i;
			}
		}
	}

	Node** pmid = std::partition(&roots[start], &roots[end - 1] + 1, [=](const BVHBuildNode* node)
		{
			float centroid = (node->bounds.min[minDim] + node->bounds.max[minDim]) * 0.5f;
			int b = bucketCount * (((centroid - centroidBounds.min[minDim]) / (centroidBounds.max[minDim] - centroidBounds.min[minDim])));
			if (b >= bucketCount)
				b = bucketCount - 1;
			if (b < 0)
				b = 0;
			return b <= minCostBucketIdx;
		});
	int mid = pmid - &roots[0];
	node->initInterior(minDim, connectTreelets(roots, start, mid, totalNodes), connectTreelets(roots, mid, end, totalNodes));
	return node;
#endif
}

#include "Primitive.h"

const float traversalCost = 1.0f;
const float emptyBonus = 0.5f;

#define uint int

class KDTree : public IntersectionAccelerator
{
	// PBR Book layout
	struct Node
	{
		void initLeaf(uint32_t* prims, int pc, std::vector<uint32_t>& primIds)
		{
			flags = 3;
			primCount |= (pc << 2);
			if (pc == 0)
				onePrim = 0;
			else if (pc == 1)
				onePrim = prims[0];
			else
			{
				primIdxOffset = primIds.size();
				// printf("PrimIds: %d, pc=%d, primCount=%d", (int)primIds.size(), pc, primCount);
				for (uint32_t i = 0; i < pc; i++)
					primIds.push_back(prims[i]);
			}
		}

		float splitPos() const { return split; }
		bool isLeaf() const { return (flags & 3) == 3; }
		uint8_t splitAxis() const { return flags & 3; }
		uint32_t getPrimCount() const { return primCount >> 2; }
		uint32_t getAboveChild() const { return aboveChild >> 2; }

		void initInterior(uint8_t ax, uint32_t aboveCh, float s)
		{
			flags = ax;
			split = s;
			aboveChild |= (aboveCh << 2);
		}

		union {
			float split;
			uint32_t onePrim;
			uint32_t primIdxOffset;
		};
	private:
		union {
			uint32_t flags;
			uint32_t primCount;
			uint32_t aboveChild;
		};
	};

	struct KdToDo
	{
		const Node* node;
		float tMin, tMax;
	};

	struct BoundEdge
	{
		float t;
		int primIdx;
		bool startingEdge;

		BoundEdge() = default;
		BoundEdge(float t, int primIdx, bool starting) : startingEdge(starting), t(t), primIdx(primIdx)
		{

		}
	};

	~KDTree()
	{
		clear();
	}

	virtual void addPrimitive(Intersectable* prim) override
	{
		m_Primitives.push_back(prim);
	}

	virtual void clear() override
	{
		// printf("\nClearing\n");
		// for (uint32_t i = 0; i < m_NextFreeNode; i++)
			// printf("%d ", m_Nodes[i].isLeaf());

		delete[] m_Nodes;
		m_Nodes = nullptr;
		m_Primitives.clear();
	}

	virtual void build(Purpose purpose) override
	{
		if (purpose == Purpose::Instances)
		{
			m_MaxPrimsPerNode = 1;
			m_IntersectionCost = 160.0f;
		}
		else
		{
			m_MaxPrimsPerNode = 4;
			m_IntersectionCost = 80.0f;
		}
		Timer timer;
		printf("Building %s KDTree with %d primitives\n", purpose == Purpose::Instances ? "instancing" : "mesh", (int)m_Primitives.size());
		m_MaxDepth = std::round(8 + 1.3f * std::log2(m_Primitives.size())); // pbr book

		std::vector<BBox> primitiveBounds;
		primitiveBounds.reserve(m_Primitives.size());
		for (auto* prim : m_Primitives)
		{
			BBox b;
			prim->expandBox(b);
			primitiveBounds.push_back(b);
			m_Bounds.add(b);
		}
		BoundEdge* edges[3];
		for (uint32_t i = 0; i < 3; i++)
			edges[i] = new BoundEdge[2 * m_Primitives.size()];
		uint32_t* prims0 = new uint32_t[m_Primitives.size()];
		uint32_t* prims1 = new uint32_t[(m_MaxDepth + 1) * m_Primitives.size()];
		uint32_t* primIds = new uint32_t[m_Primitives.size()];
		for (size_t i = 0; i < (size_t)m_Primitives.size(); i++)
			primIds[i] = i;

		build(0, m_Bounds, primitiveBounds, primIds, m_Primitives.size(), m_MaxDepth, edges, prims0, prims1);

		// printf("Prims %d==%d\n", primCount, m_Primitives.size());
		LOG_ACCEL_BUILD(AcceleratorType::KDTree, timer.toMs<float>(timer.elapsedNs() / 1000.0f), m_NextFreeNode, m_NextFreeNode * sizeof(Node) + sizeof(*this) + sizeof(m_Primitives[0]) * m_Primitives.size());
		printf("Built KDTree with %d nodes in %f seconds\n", m_NextFreeNode, Timer::toMs<float>(timer.elapsedNs()) / 1000.0f);

		delete[] prims0;
		delete[] prims1;
		delete[] primIds;
		delete[] edges[0];
		delete[] edges[1];
		delete[] edges[2];
	}

	void build(uint32_t nodeIdx, const BBox& curBounds, const std::vector<BBox>& bounds, uint32_t* primIds, size_t primCount, uint32_t depthLeft, BoundEdge* edges[3], uint32_t* prims0, uint32_t* prims1, uint32_t badRefines = 0)
	{
		if (m_NextFreeNode == m_Allocated)
		{
			uint32_t alloc = std::max(2 * m_Allocated, 512U);
			Node* n = new Node[alloc];
			if (m_Allocated > 0)
			{
				std::memcpy(n, m_Nodes, m_Allocated * sizeof(Node));
				delete[] m_Nodes;
			}
			m_Nodes = n;
			m_Allocated = alloc;
		}
		
		m_NextFreeNode++;

		if (primCount <= m_MaxPrimsPerNode || depthLeft == 0) // We can create a leaf here
		{
			m_Nodes[nodeIdx].initLeaf(primIds, primCount, m_PrimIds);
			return;
		}

		int bestAxis = -1;
		int bestOffset = -1;
		float bestCost = std::numeric_limits<float>::infinity();
		float oldCost = m_IntersectionCost * primCount;
		float invArea = 1.0f / curBounds.area();
		vec3 diag = curBounds.max - curBounds.min;

		int axis = curBounds.maxExtent();
		int retries = 0;

	tryAxis: // pbr book's fault
		for (int i = 0; i < primCount; i++)
		{
			uint32_t pn = primIds[i];
			edges[axis][2 * i] = BoundEdge(bounds[pn].min[axis], pn, true);
			edges[axis][2 * i + 1] = BoundEdge(bounds[pn].max[axis], pn, false);
		}

		std::sort(&edges[axis][0], &edges[axis][2 * primCount], [](const BoundEdge& a, const BoundEdge& b) {
			if (a.t == b.t) // wtf pbr book?
			{
				int aa = a.startingEdge ? 0 : 1;
				int bb = b.startingEdge ? 0 : 1;
				return aa < bb;
			}
			else
				return a.t < b.t;
			});

		int belowCount = 0, aboveCount = primCount;
		for (uint32_t i = 0; i < 2 * primCount; i++)
		{
			if (!edges[axis][i].startingEdge) aboveCount--;
			float t = edges[axis][i].t;
			if (t > curBounds.min[axis] && t < curBounds.max[axis])
			{
				int otherAxis1 = (axis + 1) % 3, otherAxis2 = (axis + 2) % 3;
				float belowArea = 2 * (diag[otherAxis1] * diag[otherAxis2] + (t - curBounds.min[axis]) * (diag[otherAxis1] + diag[otherAxis2]));
				float aboveArea = 2 * (diag[otherAxis1] * diag[otherAxis2] + (curBounds.max[axis] - t) * (diag[otherAxis1] + diag[otherAxis2]));

				float belowProb = belowArea * invArea;
				float aboveProb = aboveArea * invArea;

				float bonus = (aboveCount == 0 || belowCount == 0) ? emptyBonus : 0;
				float cost = traversalCost + m_IntersectionCost * (1 - bonus) * (belowProb * belowCount + aboveProb * aboveCount);

				if (cost < bestCost)
				{
					bestCost = cost;
					bestAxis = axis;
					bestOffset = i;
				}
			}
			if (edges[axis][i].startingEdge) belowCount++;
		}

		if (bestAxis == -1 && retries < 2)
		{
			retries++;
			axis = (axis + 1) % 3; // check next axis;
			goto tryAxis;
		}
		assert(belowCount == primCount && aboveCount == 0);
		if (bestCost > oldCost) ++badRefines;
		if ((bestCost > 4 * oldCost && primCount < 16) || bestAxis == -1 || badRefines == 3)
		{
			m_Nodes[nodeIdx].initLeaf(primIds, primCount, m_PrimIds);
			return;
		}

		int n0 = 0, n1 = 0;
		for (uint32_t i = 0; i < bestOffset; i++)
		{
			if (edges[bestAxis][i].startingEdge)
				prims0[n0++] = edges[bestAxis][i].primIdx;
		}

		for (uint32_t i = bestOffset + 1; i < 2 * primCount; i++)
		{
			if (!edges[bestAxis][i].startingEdge)
				prims1[n1++] = edges[bestAxis][i].primIdx;
		}

		float tSplit = edges[bestAxis][bestOffset].t;
		BBox bounds0 = curBounds, bounds1 = curBounds;
		bounds0.max[bestAxis] = bounds1.min[bestAxis] = tSplit;
		build(nodeIdx + 1, bounds0, bounds, prims0, n0, depthLeft - 1, edges, prims0, prims1 + primCount, badRefines);

		m_Nodes[nodeIdx].initInterior(bestAxis, m_NextFreeNode, tSplit);
		build(m_NextFreeNode, bounds1, bounds, prims1, n1, depthLeft - 1, edges, prims0, prims1 + primCount, badRefines);
	}

	virtual bool isBuilt() const override
	{
		return m_Nodes != nullptr;
	}

	virtual bool intersect(const Ray& ray, float tMin, float tMax, Intersection& intersection) override
	{
		float min = tMin;
		float max = tMax;

		 if (!m_Bounds.intersectP(ray, tMin, tMax))
			 return false;

		vec3 invDir = ray.dir.inverted();
		const int maxTodos = 64;
		KdToDo todos[maxTodos];
		int todoIdx = 0;

		bool hit = false;
		const Node* node = &m_Nodes[0];
		
		while (node != nullptr)
		{
		 	if (max < tMin)
				break;
			if (!node->isLeaf())
			{
				uint8_t axis = node->splitAxis();
				float plane = (node->splitPos() - ray.origin[axis]) * invDir[axis];

				const Node* firstChild, *secondChild;
				uint32_t below = (ray.origin[axis] < node->splitPos()) || (ray.origin[axis] == node->splitPos() && ray.dir[axis] <= 0);
				if (below)
				{
					firstChild = node + 1;
					secondChild = &m_Nodes[node->getAboveChild()];
				}
				else
				{
					firstChild = &m_Nodes[node->getAboveChild()];
					secondChild = node + 1;
				}

				if (plane > tMax || plane <= 0)
					node = firstChild;
				else if (plane < tMin)
					node = secondChild;
				else
				{
					todos[todoIdx].node = secondChild;
					todos[todoIdx].tMin = plane;
					todos[todoIdx].tMax = tMax;
					todoIdx++;
					node = firstChild;
					tMax = plane;
				}
			}
			else
			{
				uint32_t primCount = node->getPrimCount();
				if (primCount == 1)
				{
					Intersectable* i = m_Primitives[node->onePrim];
					if (i->intersect(ray, min, max, intersection))
					{
						hit = true;
						max = intersection.t;
					}
				}
				else
				{
					for (uint32_t i = 0; i < primCount; i++)
					{
						uint32_t idx = m_PrimIds[node->primIdxOffset + i];
						if (m_Primitives[idx]->intersect(ray, min, max, intersection))
						{
							hit = true;
							max = intersection.t;
						}
					}
				}

				if (todoIdx > 0)
				{
					todoIdx--;
					node = todos[todoIdx].node;
					tMin = todos[todoIdx].tMin;
					tMax = todos[todoIdx].tMax;
				}
				else
					break;
			}
		}

		return hit;
	}

	Node* m_Nodes;
	std::vector<uint32_t> m_PrimIds;
	BBox m_Bounds;
	uint32_t m_MaxDepth;
	uint32_t m_NextFreeNode = 0, m_Allocated = 0;
	std::vector<Intersectable*> m_Primitives;
	uint32_t m_MaxPrimsPerNode = 2;
	float m_IntersectionCost = 80.0f;
};

AcceleratorPtr makeAccelerator(AcceleratorType acceleratorType) {
	switch (acceleratorType)
	{
	case AcceleratorType::Octtree: return AcceleratorPtr(new OctTree());

	// ~3x faster in debug, ~5x in release
	case AcceleratorType::BVH: return AcceleratorPtr(new BVHTree());
	case AcceleratorType::KDTree: return AcceleratorPtr(new KDTree());
	default: return AcceleratorPtr(new OctTree());
	}
}

