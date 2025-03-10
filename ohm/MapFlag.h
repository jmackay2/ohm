// Copyright (c) 2019
// Commonwealth Scientific and Industrial Research Organisation (CSIRO)
// ABN 41 687 119 230
//
// Author: Kazys Stepanas
#ifndef MAPFLAG_H
#define MAPFLAG_H

#include "OhmConfig.h"

#include <type_traits>

namespace ohm
{
/// Flags used to augment initialisation of an @c OccupancyMap.
enum class MapFlag : unsigned
{
  /// No special features.
  kNone = 0u,
  /// Enable voxel mean position tracking.
  kVoxelMean = (1u << 0u),
  /// Maintain compressed voxels in memory. Compression is performed off thread.
  kCompressed = (1u << 1u),
  /// Maintain the traversal in addition to the occupancy layer. See @c default_layer::traversalLayerName() .
  /// The @c kVoxelMean layer should also be enabled to support traversal in order to track the voxel sample count.
  kTraversal = (1u << 2u),
  /// Maintain a (32-bit) touch time stamp for each normal.
  kTouchTime = (1u << 3u),
  /// Maintain an incident normal for each sample voxel.
  kIncidentNormal = (1u << 4u),

  /// Default map creation flags.
  kDefault = kCompressed
};

const char ohm_API *mapFlagToString(MapFlag flag);
MapFlag ohm_API mapFlagFromString(const char *str);
}  // namespace ohm

inline ohm::MapFlag operator|(ohm::MapFlag left, ohm::MapFlag right)
{
  using T = std::underlying_type_t<ohm::MapFlag>;
  return static_cast<ohm::MapFlag>(static_cast<T>(left) | static_cast<T>(right));
}

inline ohm::MapFlag &operator|=(ohm::MapFlag &left, ohm::MapFlag right)
{
  left = left | right;
  return left;
}

inline ohm::MapFlag operator&(ohm::MapFlag left, ohm::MapFlag right)
{
  using T = std::underlying_type_t<ohm::MapFlag>;
  return static_cast<ohm::MapFlag>(static_cast<T>(left) & static_cast<T>(right));
}

inline ohm::MapFlag &operator&=(ohm::MapFlag &left, ohm::MapFlag right)
{
  left = left & right;
  return left;
}

inline ohm::MapFlag operator^(ohm::MapFlag left, ohm::MapFlag right)
{
  using T = std::underlying_type_t<ohm::MapFlag>;
  return static_cast<ohm::MapFlag>(static_cast<T>(left) ^ static_cast<T>(right));
}

inline ohm::MapFlag &operator^=(ohm::MapFlag &left, ohm::MapFlag right)
{
  left = left ^ right;
  return left;
}

inline ohm::MapFlag operator~(ohm::MapFlag value)
{
  using T = std::underlying_type_t<ohm::MapFlag>;
  return static_cast<ohm::MapFlag>(~static_cast<T>(value));
}

#endif  // MAPFLAG_H
