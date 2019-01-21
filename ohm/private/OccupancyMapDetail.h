//
// Author: Kazys Stepanas
//
#ifndef OHM_MAPDETAIL_H
#define OHM_MAPDETAIL_H

#include "OhmConfig.h"

#include <glm/glm.hpp>

#include "MapChunk.h"
#include "MapInfo.h"
#include "MapLayout.h"
#include "MapRegion.h"
#include "RayFilter.h"

#include <mutex>
#include <unordered_map>
#include <vector>

namespace ohm
{
  typedef std::unordered_multimap<unsigned, MapChunk *> ChunkMap;

  class GpuCache;
  class OccupancyMap;

  struct OccupancyMapDetail
  {
    // "sub_voxel"
    static const char *kSubVoxelLayerName;

    glm::dvec3 origin = glm::dvec3(0);
    glm::dvec3 region_spatial_dimensions = glm::dvec3(0);
    glm::u8vec3 region_voxel_dimensions = glm::u8vec3(0);
    double resolution = 0.0;
    double sub_voxel_weighting = 0.3;
    uint64_t stamp = 0;
    float occupancy_threshold_value = 0.0f;
    float occupancy_threshold_probability = 0.0f;
    float hit_value = 0.0f;
    float hit_probability = 0.0f;
    float miss_value = 0.0f;
    float miss_probability = 0.0f;
    float min_voxel_value = 0.0f;
    float max_voxel_value = 0.0f;
    bool saturate_at_min_value = false;
    bool saturate_at_max_value = false;
    MapLayout layout;
    ChunkMap chunks;
    mutable std::mutex mutex;
    // Region count at load time. Useful when only the header is loaded.
    size_t loaded_region_count = 0;

    GpuCache *gpu_cache = nullptr;

    RayFilterFunction ray_filter;

    MapInfo info;  ///< Meta information storage about the map.

    ~OccupancyMapDetail();

    /// A helper function for finding the @c MapChunk for the given @p regionKey.
    /// Deals with having regions with the same hash in the map (though unlikely).
    /// @param region_key They key for the region of interest.
    /// @return The interator in @c chunks to the region of interest or @c chunks.end() when not found.
    ChunkMap::iterator findRegion(const glm::i16vec3 &region_key);

    /// @overload
    ChunkMap::const_iterator findRegion(const glm::i16vec3 &region_key) const;

    /// Move an @c Key along a selected axis.
    /// This is the implementation to @c OccupancyMap::moveKeyAlongAxis(). See that function for details.
    /// @param key The key to adjust.
    /// @param axis Axis ID to move along [0, 2].
    /// @param step How far to move/step.
    void moveKeyAlongAxis(Key &key, int axis, int step) const;

    /// Setup the default @c MapLayout: occupancy layer and clearance layer.
    /// @param enable_sub_voxel_positioning Enable the sub_voxel positioning information?
    void setDefaultLayout(bool enable_sub_voxel_positioning = false);

    /// Copy internal details from @p other. For cloning.
    /// @param other The map detail to copy from.
    void copyFrom(const OccupancyMapDetail &other);

  protected:
    template <typename ITER, typename T>
    static ITER findRegion(T &chunks, const glm::i16vec3 &region_key);
  };


  inline ChunkMap::iterator OccupancyMapDetail::findRegion(const glm::i16vec3 &region_key)
  {
    return findRegion<ChunkMap::iterator>(chunks, region_key);
  }


  inline ChunkMap::const_iterator OccupancyMapDetail::findRegion(const glm::i16vec3 &region_key) const
  {
    return findRegion<ChunkMap::const_iterator>(chunks, region_key);
  }


  template <typename ITER, typename T>
  inline ITER OccupancyMapDetail::findRegion(T &chunks, const glm::i16vec3 &region_key)
  {
    const unsigned region_hash = MapRegion::Hash::calculate(region_key);
    ITER iter = chunks.find(region_hash);
    while (iter != chunks.end() && iter->first == region_hash && iter->second->region.coord != region_key)
    {
      ++iter;
    }

    if (iter != chunks.end() && iter->first == region_hash && iter->second->region.coord == region_key)
    {
      return iter;
    }

    return chunks.end();
  }
}  // namespace ohm

#endif  // OHM_MAPDETAIL_H
