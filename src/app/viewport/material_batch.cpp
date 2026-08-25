#include "material_batch.h"

#include <algorithm>
#include <map>
#include <utility>

namespace plnr::viewport {

BucketResult bucketTrianglesByMaterial(const std::vector<TriangleMaterialKey>& keys) {
    BucketResult result;
    if (keys.empty()) return result;

    // Pass 1: bucket triangles by (front,back) key, preserving first-appearance
    // order of buckets and of members within each bucket. std::map since
    // std::pair<geo::Id,geo::Id> has operator< but no default std::hash.
    std::vector<std::pair<geo::Id, geo::Id>> bucketKeys;
    std::map<std::pair<geo::Id, geo::Id>, std::size_t> bucketIndexForKey;
    std::vector<std::vector<std::size_t>> bucketMembers;

    for (std::size_t i = 0; i < keys.size(); ++i) {
        const auto pairKey = std::make_pair(keys[i].frontMaterialId, keys[i].backMaterialId);
        const auto it = bucketIndexForKey.find(pairKey);
        std::size_t bucketIdx;
        if (it == bucketIndexForKey.end()) {
            bucketIdx = bucketKeys.size();
            bucketIndexForKey.emplace(pairKey, bucketIdx);
            bucketKeys.push_back(pairKey);
            bucketMembers.emplace_back();
        } else {
            bucketIdx = it->second;
        }
        bucketMembers[bucketIdx].push_back(i);
    }

    // Pass 2: concatenate each bucket's members (order-stable from pass 1)
    // into the flat output, deriving each range's first/count/centroid.
    result.order.reserve(keys.size());
    result.ranges.reserve(bucketKeys.size());
    int vertexOffset = 0;
    for (std::size_t b = 0; b < bucketKeys.size(); ++b) {
        const std::vector<std::size_t>& members = bucketMembers[b];
        geo::Vec3 centroidSum{};
        for (std::size_t triIdx : members) {
            result.order.push_back(triIdx);
            centroidSum = centroidSum + keys[triIdx].centroid;
        }

        MaterialRange range;
        range.first = vertexOffset;
        range.count = static_cast<int>(members.size()) * 3;
        range.frontMaterialId = bucketKeys[b].first;
        range.backMaterialId = bucketKeys[b].second;
        range.centroid = centroidSum * (1.0 / static_cast<double>(members.size()));
        result.ranges.push_back(range);

        vertexOffset += range.count;
    }

    return result;
}

PartitionedRanges partitionRangesByOpacity(std::vector<MaterialRange> ranges,
                                            const std::function<double(geo::Id)>& materialOpacity) {
    PartitionedRanges result;
    for (auto& range : ranges) {
        const double opacity = materialOpacity ? materialOpacity(range.frontMaterialId) : 1.0;
        if (opacity >= 1.0) {
            result.opaque.push_back(std::move(range));
        } else {
            result.transparent.push_back(std::move(range));
        }
    }
    return result;
}

void orderTransparentRangesBackToFront(std::vector<MaterialRange>& ranges, const geo::Vec3& eye) {
    std::stable_sort(ranges.begin(), ranges.end(), [&eye](const MaterialRange& a, const MaterialRange& b) {
        return geo::lengthSq(a.centroid - eye) > geo::lengthSq(b.centroid - eye);  // farthest first
    });
}

}  // namespace plnr::viewport
