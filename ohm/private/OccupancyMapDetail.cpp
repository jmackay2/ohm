// Copyright (c) 2018
// Commonwealth Scientific and Industrial Research Organisation (CSIRO)
// ABN 41 687 119 230
//
// Author: Kazys Stepanas
#include "OccupancyMapDetail.h"

#include "DefaultLayer.h"
#include "MapLayer.h"
#include "MapLayout.h"
#include "MapRegionCache.h"
#include "OccupancyMap.h"
#include "VoxelLayout.h"
#include "VoxelOccupancy.h"

#include <algorithm>
#include <cstring>

namespace ohm
{
OccupancyMapDetail::~OccupancyMapDetail()
{
  delete gpu_cache;
}


void OccupancyMapDetail::moveKeyAlongAxis(Key &key, int axis, int step, const glm::ivec3 &region_voxel_dimensions)
{
  const glm::ivec3 local_limits = region_voxel_dimensions;

  if (step == 0)
  {
    // No change.
    return;
  }

  // We first step within the chunk region. If we can't then we step the region and reset
  // stepped local axis value.
  glm::i16vec3 region_key = key.regionKey();
  glm::ivec3 local_key = key.localKey();
  if (step > 0)
  {
    // Positive step.
    local_key[axis] += step;
    region_key[axis] += local_key[axis] / local_limits[axis];
    local_key[axis] %= local_limits[axis];
  }
  else
  {
    // Negative step.
    local_key[axis] += step;
    // Create a region step which simulates a floating point floor.
    region_key[axis] += ((local_key[axis] - (local_limits[axis] - 1)) / local_limits[axis]);
    if (local_key[axis] < 0)
    {
      // This is nuts. In C/C++, the % operator is not actually a modulus operator.
      // It's a "remainder" operator. A modulus operator should only give positive results,
      // but in C a negative input will generate a negative output. Through the magic of
      // StackOverflow, here's a good explanation:
      //  https://stackoverflow.com/questions/11720656/modulo-operation-with-negative-numbers
      // This means that here, given localKey[axis] is negative, the modulus:
      //    localKey[axis] % LocalLimits[axis]
      // will give results in the range (-LocalLimits[axis], 0]. So, lets set the limit
      // to 4, then we get the following: like follows:
      //
      // i  i%4   4 - i%4
      //  0  0    4
      // -1 -1    3
      // -2 -2    2
      // -3 -3    1
      // -4  0    4
      // -5 -1    3
      // -6 -2    2
      // -7 -3    1
      // -8  0    4
      //
      // The last column above shows the results of the following line of code.
      // This generates the wrong results in that the '4' results in the last
      // column should be 0. We could apply another "% LocalLimits[axis]" to
      // the result or just add the if statement below.
      local_key[axis] = local_limits[axis] + local_key[axis] % local_limits[axis];
      if (local_key[axis] == local_limits[axis])
      {
        local_key[axis] = 0;
      }
    }
    // else
    // {
    //   localKey[axis] %= LocalLimits[axis];
    // }
  }

  key = Key(region_key, local_key.x, local_key.y, local_key.z);
}


void OccupancyMapDetail::setDefaultLayout(MapFlag init_flags)
{
  // Setup the default layers
  layout.clear();

  addOccupancy(layout);

  if ((init_flags & MapFlag::kVoxelMean) != MapFlag::kNone)
  {
    addVoxelMean(layout);
    flags |= MapFlag::kVoxelMean;
  }
  else
  {
    flags &= ~MapFlag::kVoxelMean;
  }

  if ((init_flags & MapFlag::kTraversal) != MapFlag::kNone)
  {
    addTraversal(layout);
    flags |= MapFlag::kTraversal;
  }
  else
  {
    flags &= ~MapFlag::kTraversal;
  }

  if ((init_flags & MapFlag::kTouchTime) != MapFlag::kNone)
  {
    addTouchTime(layout);
    flags |= MapFlag::kTouchTime;
  }
  else
  {
    flags &= ~MapFlag::kTouchTime;
  }

  if ((init_flags & MapFlag::kIncidentNormal) != MapFlag::kNone)
  {
    addIncidentNormal(layout);
    flags |= MapFlag::kIncidentNormal;
  }
  else
  {
    flags &= ~MapFlag::kIncidentNormal;
  }
}


void OccupancyMapDetail::copyFrom(const OccupancyMapDetail &other)
{
  origin = other.origin;
  region_spatial_dimensions = other.region_spatial_dimensions;
  region_voxel_dimensions = other.region_voxel_dimensions;
  resolution = other.resolution;
  stamp = other.stamp;
  occupancy_threshold_value = other.occupancy_threshold_value;
  hit_value = other.hit_value;
  miss_value = other.miss_value;
  min_voxel_value = other.min_voxel_value;
  max_voxel_value = other.max_voxel_value;
  saturate_at_min_value = other.saturate_at_min_value;
  saturate_at_max_value = other.saturate_at_max_value;
  layout = MapLayout(other.layout);
  flags = other.flags;
}
}  // namespace ohm
