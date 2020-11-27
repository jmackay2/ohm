// Copyright (c) 2018
// Commonwealth Scientific and Industrial Research Organisation (CSIRO)
// ABN 41 687 119 230
//
// Author: Kazys Stepanas
#include "Heightmap.h"

#include "private/HeightmapDetail.h"

#include "Aabb.h"
#include "DefaultLayer.h"
#include "HeightmapUtil.h"
#include "HeightmapVoxel.h"
#include "Key.h"
#include "MapChunk.h"
#include "MapCoord.h"
#include "MapInfo.h"
#include "MapLayer.h"
#include "MapLayout.h"
#include "OccupancyMap.h"
#include "OccupancyType.h"
#include "PlaneFillWalker.h"
#include "PlaneWalker.h"
#include "VoxelData.h"

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "CovarianceVoxel.h"

#include <algorithm>
#include <cstring>
#include <iostream>

#define PROFILING 0
#include <ohmutil/Profile.h>

#include <3esservermacros.h>

#include <cassert>

namespace ohm
{
namespace
{
/// Helper structure for managing voxel data access from the source heightmap.
struct SrcVoxel
{
  Voxel<const float> occupancy;             ///< Occupancy value (required)
  Voxel<const VoxelMean> mean;              ///< Voxel mean layer (optional)
  Voxel<const CovarianceVoxel> covariance;  ///< Covariance layer used for surface normal estimation (optional)
  float occupancy_threshold;                ///< Occupancy threshold cached from the source map.

  SrcVoxel(const OccupancyMap &map, bool use_voxel_mean)
    : occupancy(&map, map.layout().occupancyLayer())
    , mean(&map, use_voxel_mean ? map.layout().meanLayer() : -1)
    , covariance(&map, map.layout().covarianceLayer())
    , occupancy_threshold(map.occupancyThresholdValue())
  {}

  /// Set the key, but only for the occupancy layer.
  inline void setKey(const Key &key) { occupancy.setKey(key); }

  /// Sync the key from the occupancy layer to the other layers.
  inline void syncKey()
  {
    // Chain the occupancy values which maximise data caching.
    covariance.setKey(mean.setKey(occupancy));
  }

  /// Query the target map.
  inline const OccupancyMap &map() const { return *occupancy.map(); }

  /// Query the occupancy classification of the current voxel.
  inline OccupancyType occupancyType() const
  {
    float value = unobservedOccupancyValue();
#ifdef __clang_analyzer__
    if (occupancy.voxelMemory())
#else   // __clang_analyzer__
    if (occupancy.isValid())
#endif  // __clang_analyzer__
    {
      occupancy.read(&value);
    }
    OccupancyType type = (value >= occupancy_threshold) ? kOccupied : kFree;
    type = value != unobservedOccupancyValue() ? type : kUnobserved;
    return occupancy.chunk() ? type : kNull;
  }

  /// Query the voxel position. Must call @c syncKey() first if using voxel mean.
  inline glm::dvec3 position() const
  {
    glm::dvec3 pos = occupancy.map()->voxelCentreGlobal(occupancy.key());
#ifdef __clang_analyzer__
    if (mean.voxelMemory())
#else   // __clang_analyzer__
    if (mean.isValid())
#endif  // __clang_analyzer__
    {
      VoxelMean mean_info;
      mean.read(&mean_info);
      pos += subVoxelToLocalCoord<glm::dvec3>(mean_info.coord, occupancy.map()->resolution());
    }
    return pos;
  }

  /// Query the voxel centre for the current voxel.
  inline glm::dvec3 centre() const { return occupancy.map()->voxelCentreGlobal(occupancy.key()); }
};

/// A utility for tracking the voxel being written in the heightmap.
struct DstVoxel
{
  /// Occupancy voxel in the heightmap: writable
  Voxel<float> occupancy;
  /// Heightmap extension data.
  Voxel<HeightmapVoxel> heightmap;
  /// Voxel mean (if being used.)
  Voxel<VoxelMean> mean;

  DstVoxel(OccupancyMap &map, int heightmap_layer, bool use_mean)
    : occupancy(&map, map.layout().occupancyLayer())
    , heightmap(&map, heightmap_layer)
    , mean(&map, use_mean ? map.layout().meanLayer() : -1)
  {}

  inline void setKey(const Key &key) { mean.setKey(heightmap.setKey(occupancy.setKey(key))); }

  /// Get the target (height)map
  inline const OccupancyMap &map() const { return *occupancy.map(); }

  /// Query the position from the heightmap.
  inline glm::dvec3 position() const
  {
    glm::dvec3 pos = occupancy.map()->voxelCentreGlobal(occupancy.key());
    if (mean.isLayerValid())
    {
      VoxelMean mean_info;
      mean.read(&mean_info);
      pos += subVoxelToLocalCoord<glm::dvec3>(mean_info.coord, occupancy.map()->resolution());
    }
    return pos;
  }

  /// Set the position in the heightmap
  inline void setPosition(const glm::dvec3 &pos)
  {
    if (mean.isValid())
    {
      VoxelMean voxel_mean;
      mean.read(&voxel_mean);
      voxel_mean.coord = subVoxelCoord(pos - mean.map()->voxelCentreGlobal(mean.key()), mean.map()->resolution());
      voxel_mean.count = 1;
      mean.write(voxel_mean);
    }
  }

  inline glm::dvec3 centre() const { return occupancy.map()->voxelCentreGlobal(occupancy.key()); }

  inline void debugDraw(int level, int up_axis, double up_scale = 1.0)
  {
    (void)level;
    (void)up_axis;
    (void)up_scale;
#ifdef TES_ENABLE
    if (occupancy.isValid() && g_tes)
    {
      // Generate an ID for the voxel.
      static uint32_t next_id = 1000;  // This will do for now. May have aliasing issues. Better to come from the key.
      glm::dvec3 voxel_pos = occupancy.map()->voxelCentreGlobal(occupancy.key());
      voxel_pos[up_axis] += up_scale * heightmap.data().height;
      tes::Box voxel(tes::Id(next_id),
                     tes::Transform(glm::value_ptr(voxel_pos), tes::Vector3d(occupancy.map()->resolution())));
      voxel.setReplace(true);
      // voxel.setTransparent(true);
      voxel.setColour(tes::Colour::Colours[tes::Colour::Green]);
      g_tes->create(voxel);

      // Create a line for the clearance height.
      const double clearance_height = heightmap.data().clearance;
      glm::dvec3 clearance_dir(0);
      clearance_dir[up_axis] = up_scale;

      tes::Arrow clearance(tes::Id(next_id),
                           tes::Directional(tes::Vector3d(glm::value_ptr(voxel_pos)),
                                            tes::Vector3d(glm::value_ptr(clearance_dir)), 0.005, clearance_height));
      clearance.setColour(tes::Colour::Colours[tes::Colour::Orange]);
      clearance.setReplace(true);
      g_tes->create(clearance);

      next_id++;
    }
#endif  // TES_ENABLE
  }
};

inline float relativeVoxelHeight(double absolute_height, const Key &key, const OccupancyMap &map, const glm::dvec3 &up)
{
  const float relative_height = float(absolute_height - glm::dot(map.voxelCentreGlobal(key), up));
  return relative_height;
}


inline OccupancyType sourceVoxelHeight(glm::dvec3 *voxel_position, double *height, SrcVoxel &voxel,
                                       const glm::dvec3 &up)
{
  OccupancyType voxel_type = voxel.occupancyType();
  if (voxel_type == ohm::kOccupied)
  {
    // Determine the height offset for voxel.
    voxel.syncKey();
    *voxel_position = voxel.position();
  }
  else
  {
    // Return the voxel centre. Voxel may be invalid, so use the map interface on the key.
    *voxel_position = voxel.map().voxelCentreGlobal(voxel.occupancy.key());
  }
  *height = glm::dot(*voxel_position, up);

  return voxel_type;
}

/// A secondary operation for @c findNearestSupportingVoxel() which finds the first occupied or virtual voxel in the
/// column of @p from_key. This function can search either up or down from @p from_key until a candidate is found, the
/// @p step_limit number of voxels have been considered or ofter @c to_key has been considered - whichever condition is
/// met first.
///
/// A valid candidate voxel is one which is occupied or a virtual surface voxel. See the virtual surfaces section of the
/// @c Heightmap class documentation.
///
/// @param voxel Configured to allow access to various aspects of the source map voxels.
/// @param from_key Voxel key identifying where to start the search from. Only voxels in the same column are considered
///   up to @c to_key .
/// @param to_key The final key to consider. This must be in the same column as @c from_key.
/// @param up_axis_index Index of the up axis: 0=>x, 1=>y, 2=>z.
/// @param step_limit Limits the number of voxels to be visited. Zero to visit all voxels in the range
/// `[from_key, to_key]`
/// @param search_up A flag used to indicate if we are searching up a column or not. The sign between @c from_key and
/// cannot be used to infer this as this information is is defined by the heightmap primary axis, for which up may be
/// alinged with an increasting, negative magnitude.
/// @param allow_virtual_surface True to consider virtual surface voxels - free voxels "supported" by unobserved voxels
///   representig the best possible surface estimate.
/// @param[out] offset On success, set the number of voxels between @c from_key and the selected voxel. Undefined on
/// failure.
/// @param[out] is_virtual On success, set to true if the selected voxel is a virtual surface voxel. False when the
/// selected voxel is an occupied voxel. Undefined on failure.
/// @return The first voxel found which is either occupied or a virtual surface voxel when @c allow_virtual_surface is
/// true. The result is a null key on failure to find such a voxel within the search limits.
Key findNearestSupportingVoxel2(SrcVoxel &voxel, const Key &from_key, const Key &to_key, int up_axis_index,
                                int step_limit, bool search_up, bool allow_virtual_surface, int *offset,
                                bool *is_virtual)
{
  // Calculate the vertical range we will be searching.
  // Note: the vertical_range sign may not be what you expect. It will match search_up (true === +, false === -)
  // when the up axis is +X, +Y, or +Z. It will not match when the up axis is -X, -Y, or -Z.
  int vertical_range = voxel.map().rangeBetween(from_key, to_key)[up_axis_index] + 1;
  // Step direction is based on the vertical_range sign.
  const int step = (vertical_range >= 0) ? 1 : -1;
  vertical_range = (vertical_range >= 0) ? vertical_range : -vertical_range;
  if (step_limit > 0)
  {
    vertical_range = std::min(vertical_range, step_limit);
  }

  Key best_virtual(nullptr);
  bool last_unknown = true;
  bool last_free = true;

  Key last_key(nullptr);
  Key current_key = from_key;
  for (int i = 0; i < vertical_range; ++i)
  {
    // We bias the offset up one voxel for upward searches. The expectation is that the downward search starts
    // at the seed voxel, while the upward search starts one above that without overlap.
    *offset = i + !!search_up;
    voxel.setKey(current_key);

    // This line yields performance issues likely due to the stochastic memory access.
    // For a true performance gain we'd have to access chunks linearly.
    // Read the occupancy value for the voxel.
    float occupancy = unobservedOccupancyValue();
    if (voxel.occupancy.chunk())
    {
      voxel.occupancy.read(&occupancy);
    }
    // Categorise the voxel.
    const bool occupied = occupancy >= voxel.occupancy_threshold && occupancy != unobservedOccupancyValue();
    const bool free = occupancy < voxel.occupancy_threshold;

    if (occupied)
    {
      // Voxel is occupied. We've found our candidate.
      *is_virtual = false;
      return current_key;
    }

    // No occupied voxel. Update the best (virtual) voxel.
    // We either keep the current best_virtual, or we select the current_voxel as a new best candidate.
    // We split this work into two. The first check is for the upward search where always select the first viable
    // virtual surface voxel and will not overwrite it. The conditions for the upward search are:
    // - virtual surface is allowed
    // - searching up
    // - the current voxel is free
    // - the previous voxel was unknown
    // - we do not already have a virtual voxel
    best_virtual = (allow_virtual_surface && search_up && free && last_unknown && best_virtual.isNull()) ? current_key :
                                                                                                           best_virtual;

    // This is the case for searching down. In this case we are always looking for the lowest virtual voxel.
    // We progressively select the last voxel as the new virtual voxel provided it was considered free and the current
    // voxel is unknown (not free and not occupied). We only need to check free as we will have exited on an occupied
    // voxel. The conditions here are:
    // - virtual surface is allowed
    // - searching down (!search_up)
    // - the last voxel was free
    // - the current voxel is unknown - we only need check !free at this point
    // Otherwise we keep the current candidate
    best_virtual = (allow_virtual_surface && !search_up && last_free && !free) ? last_key : best_virtual;

    // Cache values for the next iteration.
    last_unknown = !occupied && !free;
    last_free = free;
    last_key = current_key;

    // Calculate the next voxel.
    int next_step = step;
    if (!voxel.occupancy.chunk())
    {
      // The current voxel is an empty chunk implying all unknown voxels. We will skip to the last voxel in this
      // chunk. We don't skip the whole chunk to allow the virtual voxel calculation to take effect.
      next_step = (step > 0) ? voxel.occupancy.layerDim()[up_axis_index] - current_key.localKey()[up_axis_index] :
                               -(1 + current_key.localKey()[up_axis_index]);
      i += std::abs(next_step) - 1;
    }

    // Single step in the current region.
    voxel.map().moveKeyAlongAxis(current_key, up_axis_index, next_step);
  }

  if (best_virtual.isNull())
  {
    if (allow_virtual_surface && !search_up && last_free)
    {
      best_virtual = last_key;
    }
    else
    {
      *offset = -1;
    }
  }

  *is_virtual = !best_virtual.isNull();

  // We only get here if we haven't found an occupied voxel. Return the best virtual one.
  return best_virtual;
}

/// Search the column containing @p seed_key in the source occupancy map for a potential supporting voxel.
///
/// A supporting voxel is one which is either occupied or a virtual surface voxel (if enabled). The source map details
/// are contained in the @c SrcVoxel structure passed via @p voxel. That structure is configured to reference the
/// relevant voxel layers. The actual voxels referenced by @c voxel will be modified by this function, starting at
/// @c seed_key .
///
/// The search process searches above and below @c seed_key - with up defined by @c up_axis - for an occupied or
/// virtual surface voxel. The final voxel selection is guided by several factors:
///
/// - Prefer occupied voxels over virtual surface voxels
///   - Except where @c promote_virtual_below is true
/// - Prefer below to above.
////  - Except where the distance between the candidates below and above is less than @p
///     clearance_voxel_count_permissive.
/// - Limit the search expance to search up @c voxel_ceiling voxels (this is a voxel count value).
/// - Limit the search down to the map extents.
///
/// The resulting key can be used to identify the voxel from which to start searching for an actual ground candidate
/// with consideration given to clearance above.
///
/// The selected key is expected to be used as the seed for @c findGround().
///
/// @param voxel Configured to allow access to various aspects of the source map voxels.
/// @param seed_key Voxel key identifying where to start the search from. Only voxels in the same column are considered.
/// @param up_axis Identifies the up axis - XYZ - and direction.
/// @param min_key Min extents limits. Searches are bounded by this value.
/// @param max_key Max extents limits. Searches are bounded by this value.
/// @param voxel_ceiling The number of voxels the function is allowed to consider above the @p seed_key.
/// @param clearance_voxel_count_permissive The number of voxels required to pass the clearance value. Used to
///   discriminate the voxel below when the candidate above is within this range, but non-virtual.
/// @param allow_virtual_surface True to consider virtual surface voxels - free voxels "supported" by unobserved voxels
///   representig the best possible surface estimate.
/// @param promote_virtual_below Report virtual surface voxels below the @p seed_key which are virtual in preference to
///   real voxels above.
/// @return A new seed key from which to start searching for a valid ground voxel (@c findGround()).
inline Key findNearestSupportingVoxel(SrcVoxel &voxel, const Key &seed_key, UpAxis up_axis, const Key &min_key,
                                      const Key &max_key, int voxel_ceiling, int clearance_voxel_count_permissive,
                                      bool allow_virtual_surface, bool promote_virtual_below)
{
  PROFILE(findNearestSupportingVoxel);
  Key above;
  Key below;
  int offset_below = -1;
  int offset_above = -1;
  bool virtual_below = false;
  bool virtual_above = false;

  const int up_axis_index = (int(up_axis) >= 0) ? int(up_axis) : -int(up_axis) - 1;
  const Key &search_down_to = (int(up_axis) >= 0) ? min_key : max_key;
  const Key &search_up_to = (int(up_axis) >= 0) ? max_key : min_key;
  below = findNearestSupportingVoxel2(voxel, seed_key, search_down_to, up_axis_index, 0, false, allow_virtual_surface,
                                      &offset_below, &virtual_below);
  above = findNearestSupportingVoxel2(voxel, seed_key, search_up_to, up_axis_index, voxel_ceiling, true,
                                      allow_virtual_surface, &offset_above, &virtual_above);

  const bool have_candidate_below = offset_below >= 0;
  const bool have_candidate_above = offset_above >= 0;

  // Ignore the fact that the voxel below is virtual when prefer_virtual_below is set.
  virtual_below = have_candidate_below && virtual_below && !promote_virtual_below;

  // Prefer non-virtual over virtual. Prefer the closer result.
  if (have_candidate_below && virtual_above && !virtual_below)
  {
    return below;
  }

  if (have_candidate_above && !virtual_above && virtual_below)
  {
    return above;
  }

  // We never allow virtual voxels above as this generates better heightmaps. Virtual surfaces are more interesting
  // when approaching a slope down than any such information above.
  if (have_candidate_below && virtual_above && virtual_below)
  {
    return below;
  }

  // When both above and below have valid candidates. We prefer the lower one if there is sufficient clearance from
  // it to the higher one (should be optimistic). Otherwise we prefer the one which has had less searching.
  if (have_candidate_below && (!have_candidate_above || offset_below <= offset_above ||
                               have_candidate_below && have_candidate_above && !virtual_above &&
                                 offset_below + offset_above >= clearance_voxel_count_permissive))
  {
    return below;
  }

  return above;
}

/// Search for the best ground voxel for the column containing @p seed_key . The search begins at @p seed_key, normally
/// generated by @c findNearestSupportingVoxel(). This function considers the configured
/// @c HeightmapDetail::min_clearance from @p imp and may also consider virtual surface voxels if configured to do so.
///
/// @param[out] height_out Set to the height of the selected ground voxel. This will is based on the voxel mean position
/// (if enabled) for occupied voxels where available, otherwise using the centre of the selected voxel. The value is
/// undefined if the return value is a null key.
/// @param[out] clearance_out Set to the available clearance above the selected voxel if available. A -1 value indicates
/// the height cannot be calculated such as at the limit of the map extents or at the limit of the search extents.
/// Undefined if the result is a null key.
/// @param voxel Configured to allow access to various aspects of the source map voxels.
/// @param seed_key Voxel key identifying where to start the search from. Only voxels in the same column are considered.
/// @param min_key Min extents limits. Searches are bounded by this value.
/// @param max_key Max extents limits. Searches are bounded by this value.
/// @param imp Impelementation details of the heightmap object being operated on.
/// @return The first viable ground candidate found from @c seed_key or a null key if no such voxel can be bound.
Key findGround(double *height_out, double *clearance_out, SrcVoxel &voxel, const Key &seed_key, const Key &min_key,
               const Key &max_key, const HeightmapDetail &imp)
{
  PROFILE(findGround);
  // Start with the seed_key and look for ground. We only walk up from the seed key.
  double column_height = std::numeric_limits<double>::max();
  double column_clearance_height = column_height;

  // Start walking the voxels in the source map.
  // glm::dvec3 column_reference = heightmap.voxelCentreGlobal(target_key);

  // Walk the src column up.
  const int up_axis_index = imp.vertical_axis_index;
  // Select walking direction based on the up axis being aligned with the primary axis or not.
  const int step_dir = (int(imp.up_axis_id) >= 0) ? 1 : -1;
  glm::dvec3 sub_voxel_pos(0);
  glm::dvec3 column_voxel_pos(0);
  double height = 0;
  OccupancyType candidate_voxel_type = ohm::kNull;
  OccupancyType last_voxel_type = ohm::kNull;

  Key ground_key = Key::kNull;
  for (Key key = seed_key; key.isBounded(up_axis_index, min_key, max_key);
       voxel.map().stepKey(key, up_axis_index, step_dir))
  {
    // PROFILE(column);
    voxel.setKey(key);

    const OccupancyType voxel_type = sourceVoxelHeight(&sub_voxel_pos, &height, voxel, imp.up);

    // We check the clearance and consider a new candidate if we have encountered an occupied voxel, or
    // we are considering virtual surfaces. When considering virtual surfaces, we also check clearance where we
    // have transitioned from unobserved to free and we do not already have a candidate voxel. In this way
    // only occupied voxels can obstruct the clearance value and only the lowest virtual voxel will be considered as
    // a surface.
    const bool last_is_unobserved = last_voxel_type == ohm::kUnobserved || last_voxel_type == ohm::kNull;
    if (voxel_type == ohm::kOccupied || imp.generate_virtual_surface && last_is_unobserved &&
                                          voxel_type == ohm::kFree && candidate_voxel_type == ohm::kNull)
    {
      if (candidate_voxel_type != ohm::kNull)
      {
        // Branch condition where we have a candidate ground_key, but have yet to check or record its clearance.
        // Clearance height is the height of the current voxel associated with key.
        column_clearance_height = height;
        if (column_clearance_height - column_height >= imp.min_clearance)
        {
          // Found our heightmap voxels.
          // We have sufficient clearance so ground_key is our ground voxel.
          break;
        }

        // Insufficient clearance. The current voxel becomes our new base voxel; keep looking for clearance.
        column_height = column_clearance_height = height;
        column_voxel_pos = sub_voxel_pos;
        // Current voxel becomes our new ground candidate voxel.
        ground_key = key;
        candidate_voxel_type = voxel_type;
      }
      else
      {
        // Branch condition only for the first voxel in column.
        ground_key = key;
        column_height = column_clearance_height = height;
        column_voxel_pos = sub_voxel_pos;
        candidate_voxel_type = voxel_type;
      }
    }

    last_voxel_type = voxel_type;
  }

  // Did we find a valid candidate?
  if (candidate_voxel_type != ohm::kNull)
  {
    *height_out = height;
    *clearance_out = column_clearance_height - column_height;
    return ground_key;
  }

  return Key::kNull;
}


void onVisitPlaneFill(PlaneFillWalker &walker, const HeightmapDetail &imp, const Key &candidate_key,
                      const Key &ground_key)
{
// Add neighbours for walking.
#if 1
  PlaneFillWalker::Revisit revisit_behaviour = PlaneFillWalker::Revisit::kNone;
  if (!candidate_key.isNull())
  {
    // revisit_behaviour = int(imp.up_axis_id) >= 0 ? PlaneFillWalker::Revisit::kLower :
    // PlaneFillWalker::Revisit::kHigher;
    revisit_behaviour = PlaneFillWalker::Revisit::kHigher;
  }
#else   // #
  // Testing revisit behaviour.
  PlaneFillWalker::Revisit revisit_behaviour = PlaneFillWalker::Revisit::kNone;
  if (!candidate_key.isNull())
  {
    revisit_behaviour = PlaneFillWalker::Revisit::kAll;
  }
#endif  // #
  std::array<Key, 8> neighbours;
  size_t added_count = walker.addNeighbours(ground_key, neighbours, revisit_behaviour);
#ifdef TES_ENABLE
  if (g_tes)
  {
    for (size_t i = 0; i < added_count; ++i)
    {
      const Key &nkey = neighbours[i];
      const glm::dvec3 pos = imp.occupancy_map->voxelCentreGlobal(nkey);
      tes::Box neighbour(
        tes::Id(0), tes::Transform(tes::Vector3d(glm::value_ptr(pos)), tes::Vector3d(imp.heightmap->resolution())));
      neighbour.setColour(tes::Colour::Colours[tes::Colour::CornflowerBlue]).setWireframe(true);
      g_tes->create(neighbour);
    }
  }
#endif  // neighbours
}
}  // namespace

Heightmap::Heightmap()
  : Heightmap(0.2, 2.0, UpAxis::kZ)  // NOLINT(readability-magic-numbers)
{}


Heightmap::Heightmap(double grid_resolution, double min_clearance, UpAxis up_axis, unsigned region_size)
  : imp_(new HeightmapDetail)
{
  region_size = region_size ? region_size : kDefaultRegionSize;

  imp_->min_clearance = min_clearance;

  if (up_axis < UpAxis::kNegZ || up_axis > UpAxis::kZ)
  {
    std::cerr << "Unknown up axis ID: " << int(up_axis) << std::endl;
    up_axis = UpAxis::kZ;
  }

  // Cache the up axis normal.
  imp_->up_axis_id = up_axis;
  imp_->updateAxis();

  // Use an OccupancyMap to store grid cells. Each region is 1 voxel thick.
  glm::u8vec3 region_dim(region_size);
  region_dim[int(imp_->vertical_axis_index)] = 1;
  imp_->heightmap = std::make_unique<OccupancyMap>(grid_resolution, region_dim);
  // The multilayer heightmap expects more entries. Default to having room for N layers per chunk.
  region_dim[int(imp_->vertical_axis_index)] = 4;
  imp_->multilayer_heightmap = std::make_unique<OccupancyMap>(grid_resolution, region_dim);

  imp_->heightmap_layer = heightmap::setupHeightmap(*imp_->heightmap, *imp_);
  heightmap::setupHeightmap(*imp_->multilayer_heightmap, *imp_);
}


Heightmap::~Heightmap() = default;


void Heightmap::setOccupancyMap(OccupancyMap *map)
{
  imp_->occupancy_map = map;
}


OccupancyMap *Heightmap::occupancyMap() const
{
  return imp_->occupancy_map;
}


OccupancyMap &Heightmap::heightmap() const
{
  return *imp_->heightmap;
}


void Heightmap::setCeiling(double ceiling)
{
  imp_->ceiling = ceiling;
}


double Heightmap::ceiling() const
{
  return imp_->ceiling;
}


void Heightmap::setMinClearance(double clearance)
{
  imp_->min_clearance = clearance;
}


double Heightmap::minClearance() const
{
  return imp_->min_clearance;
}


void Heightmap::setIgnoreVoxelMean(bool ignore)
{
  imp_->ignore_voxel_mean = ignore;
}


bool Heightmap::ignoreVoxelMean() const
{
  return imp_->ignore_voxel_mean;
}


void Heightmap::setGenerateVirtualSurface(bool enable)
{
  imp_->generate_virtual_surface = enable;
}


bool Heightmap::generateVirtualSurface() const
{
  return imp_->generate_virtual_surface;
}


void Heightmap::setPromoteVirtualBelow(bool enable)
{
  imp_->promote_virtual_below = enable;
}


bool Heightmap::promoteVirtualBelow() const
{
  return imp_->promote_virtual_below;
}


void Heightmap::setUseFloodFill(bool flood_fill)
{
  imp_->use_flood_fill = flood_fill;
}


bool Heightmap::useFloodFill() const
{
  return imp_->use_flood_fill;
}


UpAxis Heightmap::upAxis() const
{
  return UpAxis(imp_->up_axis_id);
}


int Heightmap::upAxisIndex() const
{
  return int(imp_->vertical_axis_index);
}


const glm::dvec3 &Heightmap::upAxisNormal() const
{
  return imp_->up;
}


int Heightmap::surfaceAxisIndexA() const
{
  return HeightmapDetail::surfaceIndexA(imp_->up_axis_id);
}


const glm::dvec3 &Heightmap::surfaceAxisA() const
{
  return HeightmapDetail::surfaceNormalA(imp_->up_axis_id);
}


int Heightmap::surfaceAxisIndexB() const
{
  return HeightmapDetail::surfaceIndexB(imp_->up_axis_id);
}


const glm::dvec3 &Heightmap::surfaceAxisB() const
{
  return HeightmapDetail::surfaceNormalB(imp_->up_axis_id);
}


const glm::dvec3 &Heightmap::upAxisNormal(UpAxis axis_id)
{
  return HeightmapDetail::upAxisNormal(axis_id);
}


const glm::dvec3 &Heightmap::surfaceAxisA(UpAxis axis_id)
{
  return HeightmapDetail::surfaceNormalA(axis_id);
}


const glm::dvec3 &Heightmap::surfaceAxisB(UpAxis axis_id)
{
  return HeightmapDetail::surfaceNormalB(axis_id);
}


int Heightmap::heightmapVoxelLayer() const
{
  return imp_->heightmap_layer;
}


bool Heightmap::buildHeightmap(const glm::dvec3 &reference_pos, const ohm::Aabb &cull_to)
{
  if (!imp_->occupancy_map)
  {
    return false;
  }

  PROFILE(buildHeightmap);

  // 1. Calculate the map extents.
  //  a. Calculate occupancy map extents.
  //  b. Project occupancy map extents onto heightmap plane.
  // 2. Populate heightmap voxels

  const OccupancyMap &src_map = *imp_->occupancy_map;
  ohm::Aabb src_region;
  src_map.calculateExtents(&src_region.minExtentsMutable(), &src_region.maxExtentsMutable());

  // Clip to the cull box.
  for (int i = 0; i < 3; ++i)
  {
    if (cull_to.diagonal()[i] > 0)
    {
      src_region.minExtentsMutable()[i] = cull_to.minExtents()[i];
      src_region.maxExtentsMutable()[i] = cull_to.maxExtents()[i];
    }
  }

  // Generate keys for these extents.
  const Key min_ext_key = src_map.voxelKey(src_region.minExtents());
  const Key max_ext_key = src_map.voxelKey(src_region.maxExtents());

  unsigned processed_count = 0;
  if (!imp_->use_flood_fill)
  {
    const Key planar_key = src_map.voxelKey(reference_pos);
    PlaneWalker walker(src_map, min_ext_key, max_ext_key, imp_->up_axis_id, &planar_key);
    processed_count = buildHeightmapT(walker, reference_pos);
  }
  else
  {
    PlaneFillWalker walker(src_map, min_ext_key, max_ext_key, imp_->up_axis_id, false);
    processed_count = buildHeightmapT(walker, reference_pos, &onVisitPlaneFill);
  }

#if PROFILING
  ohm::Profile::instance().report();
#endif  // PROFILING

  return processed_count;
}


HeightmapVoxelType Heightmap::getHeightmapVoxelInfo(const Key &key, glm::dvec3 *pos, HeightmapVoxel *voxel_info) const
{
  if (!key.isNull())
  {
    Voxel<const float> heightmap_occupancy(imp_->heightmap.get(), imp_->heightmap->layout().occupancyLayer(), key);

    if (heightmap_occupancy.isValid())
    {
      Voxel<const HeightmapVoxel> heightmap_voxel(imp_->heightmap.get(), imp_->heightmap_layer, key);
      Voxel<const VoxelMean> mean_voxel(imp_->heightmap.get(), imp_->heightmap->layout().meanLayer(), key);

      const glm::dvec3 voxel_centre = imp_->heightmap->voxelCentreGlobal(key);
      *pos = mean_voxel.isLayerValid() ? positionSafe(mean_voxel) : voxel_centre;
      float occupancy;
      heightmap_occupancy.read(&occupancy);
      const bool is_uncertain = occupancy == ohm::unobservedOccupancyValue();
      const float heightmap_voxel_value = (!is_uncertain) ? occupancy : -1.0f;
      // isValid() is somewhat redundant, but it silences a clang-tidy check.
      if (!is_uncertain && heightmap_voxel.isValid())
      {
        // Get height info.
        HeightmapVoxel heightmap_info;
        heightmap_voxel.read(&heightmap_info);
        (*pos)[upAxisIndex()] = voxel_centre[upAxisIndex()] + heightmap_info.height;
        if (voxel_info)
        {
          *voxel_info = heightmap_info;
        }

        if (heightmap_voxel_value == 0)
        {
          // kVacant
          return HeightmapVoxelType::kVacant;
        }

        if (heightmap_voxel_value > 0)
        {
          return HeightmapVoxelType::kSurface;
        }
      }

      return (!is_uncertain) ? HeightmapVoxelType::kVirtualSurface : HeightmapVoxelType::kUnknown;
    }
    return HeightmapVoxelType::kUnknown;
  }
  return HeightmapVoxelType::kUnknown;
}


void Heightmap::updateMapInfo(MapInfo &info) const
{
  imp_->toMapInfo(info);
}


Key &Heightmap::project(Key *key) const
{
  key->setRegionAxis(upAxisIndex(), 0);
  key->setLocalAxis(upAxisIndex(), 0);
  return *key;
}


template <typename KeyWalker>
bool Heightmap::buildHeightmapT(KeyWalker &walker, const glm::dvec3 &reference_pos,
                                void (*on_visit)(KeyWalker &, const HeightmapDetail &, const Key &, const Key &))
{
  // Brute force initial approach.
  const OccupancyMap &src_map = *imp_->occupancy_map;
  OccupancyMap &heightmap = *imp_->heightmap;

  updateMapInfo(heightmap.mapInfo());

  // Clear previous results.
  heightmap.clear();

  // Encode the base height of the heightmap in the origin.
  // heightmap.setOrigin(upAxisNormal() * glm::dot(upAxisNormal(), reference_pos));

  // Allow voxel mean positioning.
  const bool use_voxel_mean = src_map.voxelMeanEnabled() && !imp_->ignore_voxel_mean;
  if (use_voxel_mean)
  {
    heightmap.addVoxelMeanLayer();
  }

  PROFILE(walk)

  // Set the initial key.
  Key walk_key = src_map.voxelKey(reference_pos);

  // Bound the walk_key to the search bounds.
  if (!walk_key.isBounded(walker.min_ext_key, walker.max_ext_key))
  {
    walk_key.clampToAxis(surfaceAxisIndexA(), walker.min_ext_key, walker.max_ext_key);
    walk_key.clampToAxis(surfaceAxisIndexB(), walker.min_ext_key, walker.max_ext_key);
  }

  if (!walker.begin(walk_key))
  {
    return false;
  }

  // Walk the 2D extraction region in a spiral around walk_key.
  const glm::dvec3 up_axis_normal = upAxisNormal();
  unsigned populated_count = 0;
  const int voxel_ceiling = ohm::pointToRegionCoord(imp_->ceiling, src_map.resolution());
  const int clearance_voxel_count_permissive =
    std::max(1, ohm::pointToRegionCoord(imp_->min_clearance, src_map.resolution()) - 1);

  SrcVoxel src_voxel(src_map, use_voxel_mean);
  DstVoxel hm_voxel(heightmap, imp_->heightmap_layer, use_voxel_mean);

  const glm::dvec3 debug_pos(2.05, 0.75, 0);
  bool abort = false;
  do
  {
    const glm::dvec3 ref_pos = src_map.voxelCentreGlobal(walk_key);
    if (std::abs(ref_pos.x - debug_pos.x) < 0.5 * src_map.resolution() &&
        std::abs(ref_pos.y - debug_pos.x) < 0.5 * src_map.resolution())
    {
      int stopme = 1;
    }

    // Find the nearest voxel to the current key which may be a ground candidate.
    // This is key closest to the walk_key which could be ground. This will be either an occupied voxel, or virtual
    // ground voxel.
    // Virtual ground is where a free is supported by an uncertain or null voxel below it.
    Key candidate_key = findNearestSupportingVoxel(src_voxel, walk_key, upAxis(), walker.min_ext_key,
                                                   walker.max_ext_key, voxel_ceiling, clearance_voxel_count_permissive,
                                                   imp_->generate_virtual_surface, imp_->promote_virtual_below);

    // Walk up from the candidate to find the best heightmap voxel.
    double height = 0;
    double clearance = 0;
    // Walk the column of candidate_key to find the first occupied voxel with sufficent clearance. A virtual voxel
    // with sufficient clearance may be given if there is no valid occupied voxel.
    const Key ground_key = (!candidate_key.isNull()) ? findGround(&height, &clearance, src_voxel, candidate_key,
                                                                  walker.min_ext_key, walker.max_ext_key, *imp_) :
                                                       walk_key;

    if (on_visit)
    {
      (*on_visit)(walker, *imp_, candidate_key, ground_key);
    }

    // Write to the heightmap.
    src_voxel.setKey(ground_key);
    src_voxel.syncKey();
    const OccupancyType voxel_type = src_voxel.occupancyType();

    // We use the voxel centre for lookup in the local cache for better consistency. Otherwise lateral drift in
    // subvoxel positioning can result in looking up the wrong cell.
    glm::dvec3 src_voxel_centre =
      (src_voxel.occupancy.isValid()) ? src_voxel.centre() : src_voxel.map().voxelCentreGlobal(ground_key);
    // We only use voxel mean positioning for occupied voxels. The information is unreliable for free voxels.
    glm::dvec3 voxel_pos = (voxel_type == kOccupied) ? src_voxel.position() : src_voxel_centre;

    if (voxel_type == kOccupied || imp_->generate_virtual_surface)
    {
      // Real or virtual surface.
      float surface_value = (voxel_type == kOccupied) ? kHeightmapSurfaceValue : kHeightmapVirtualSurfaceValue;

      if (voxel_type != kUnobserved && voxel_type != kNull)
      {
        // Cache the height then clear from the position.
        const double src_height = voxel_pos[upAxisIndex()];
        voxel_pos[upAxisIndex()] = 0;

        // Get the heightmap voxel to update.
        Key hm_key = heightmap.voxelKey(voxel_pos);
        project(&hm_key);
        // TODO(KS): Using the Voxel interface here is highly suboptimal. This needs to be modified to access the
        // MapChunk directly for efficiency.
        hm_voxel.setKey(hm_key);
        // We can do a direct occupancy value write with no checks for the heightmap. The value is explicit.
        assert(hm_voxel.occupancy.isValid() && hm_voxel.occupancy.voxelMemory());
        hm_voxel.occupancy.write(surface_value);
        // Set voxel mean position as required. Will be skipped if not enabled.
        hm_voxel.setPosition(voxel_pos);

        // Write the height and clearance values.
        HeightmapVoxel height_info;
        hm_voxel.heightmap.read(&height_info);
        height_info.height = relativeVoxelHeight(src_height, hm_key, heightmap, imp_->up);
        height_info.clearance = float(clearance);
        height_info.normal_x = height_info.normal_y = height_info.normal_z = 0;

        if (voxel_type == kOccupied && src_voxel.covariance.isValid())
        {
          CovarianceVoxel cov;
          src_voxel.covariance.read(&cov);
          glm::dvec3 normal;
          covarianceEstimatePrimaryNormal(&cov, &normal);
          const double flip = (glm::dot(normal, up_axis_normal) >= 0) ? 1.0 : -1.0;
          normal *= flip;
          height_info.normal_x = float(normal.x);
          height_info.normal_y = float(normal.y);
          height_info.normal_z = float(normal.z);
        }
        hm_voxel.heightmap.write(height_info);

        hm_voxel.debugDraw(imp_->debug_level, imp_->vertical_axis_index, int(imp_->up_axis_id) >= 0 ? 1.0 : -1.0);
        TES_SERVER_UPDATE(g_tes, 0.0f);

        ++populated_count;
      }
    }
  } while (!abort && walker.walkNext(walk_key));

  return populated_count != 0;
}
}  // namespace ohm
