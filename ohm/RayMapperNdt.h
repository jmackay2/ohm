//
// Author: Kazys Stepanas
// Copyright (c) CSIRO 2020
//
#ifndef RAYMAPPERNDT_H
#define RAYMAPPERNDT_H

#include "OhmConfig.h"

#include "CalculateSegmentKeys.h"
#include "KeyList.h"
#include "MapCache.h"
#include "RayFilter.h"
#include "RayFlag.h"
#include "RayMapper.h"
#include "Voxel.h"

#include <glm/vec3.hpp>

namespace ohm
{
  class NdtMap;

  /// A @c RayMapper implementation built around updating a map in CPU. This mapper supports occupancy population
  /// using a normal distributions transform methodology. The given map must support the following layers;
  /// @c MayLayout::occupancyLayer() - float occupancy values - , @c MapLayout::meanLayer() - @c VoxelMean - and
  /// @c MapLayout::covarianceLayer() - @c CovarianceVoxel .
  ///
  /// The @c integrateRays() implementation performs a single threaded walk of the voxels to update and touches
  /// those voxels one at a time, updating their occupancy value. Occupancy values are updated using
  /// @c calculateMissNdt() for voxels the rays pass through and @c calculateHitWithCovariance() for the sample/end
  /// voxels. Sample voxels also have their @c CovarianceVoxel and @c VoxelMean layers updated.
  ///
  /// For reference see:
  /// 3D Normal Distributions Transform Occupancy Maps: An Efficient Representation for Mapping in Dynamic Environments
  class RayMapperNdt : public RayMapper
  {
  public:
    /// Constructor, wrapping the interface around the given @p map .
    ///
    /// @param map The target map. Must outlive this class.
    RayMapperNdt(NdtMap *map);

    /// Destructor
    ~RayMapperNdt();

    /// Has the map been successfully valid?
    /// @return True if valid and @c integrateRays() is safe to call.
    inline bool valid() const { return valid_; }

    /// Performs the ray integration.
    ///
    /// This is updated in a single threaded fashion similar to @c RayMapperOccupancy with modified value updates as
    /// described in the class documentation.
    ///
    /// This function does not support @c RayFlag values.
    ///
    /// Should only be called if @c valid() is true.
    ///
    /// @param rays The array of start/end point pairs to integrate.
    /// @param element_count The number of @c glm::dvec3 elements in @p rays, which is twice the ray count.
    /// @param ray_update_flags Not supported.
    size_t integrateRays(const glm::dvec3 *rays, size_t element_count, unsigned ray_update_flags = kRfDefault) override;

  protected:
    NdtMap *map_;                ///< Target map.
    int occupancy_layer_ = -1;   ///< Cached occupancy layer index.
    int mean_layer_ = -1;        ///< Cached voxel mean layer index.
    int covariance_layer_ = -1;  ///< Cached voxel mean layer index.
    /// Cached occupancy layer voxel dimensions. Voxel mean and covariance layers must exactly match.
    glm::u8vec3 occupancy_dim_{ 0, 0, 0 };
    bool valid_ = false;  ///< Has layer validation passed?
  };

}  // namespace ohm


#endif  // RAYMAPPERNDT_H
