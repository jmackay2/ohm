// Copyright (c) 2021
// Commonwealth Scientific and Industrial Research Organisation (CSIRO)
// ABN 41 687 119 230
//
// Author: Kazys Stepanas
#include "DecayRateTest.h"

#include <gtest/gtest.h>

#include <ohm/Key.h>
#include <ohm/OccupancyMap.h>
#include <ohm/RayMapper.h>
#include <ohm/Voxel.h>

#include <glm/vec3.hpp>


namespace ohmtestcommon
{
namespace decay
{
void testInto(ohm::OccupancyMap &map, ohm::RayMapper &mapper, std::function<void()> pre_validation)
{
  ASSERT_GT(map.layout().decayRateLayer(), 0);

  // Offset the map so that we can cast rays through the origin without incurring floating point ambituity on the target
  // voxel.
  map.setOrigin(glm::dvec3(-0.5f * map.resolution()));

  // Create the rays we are to cast.
  const std::vector<glm::dvec3> rays = {
    glm::dvec3(-1, 0, 0), glm::dvec3(0, 0, 0), glm::dvec3(0, -1, 0), glm::dvec3(0, 0, 0),
    glm::dvec3(0, 0, -1), glm::dvec3(0, 0, 0), glm::dvec3(1, 0, 0),  glm::dvec3(0, 0, 0),
    glm::dvec3(0, 1, 0),  glm::dvec3(0, 0, 0), glm::dvec3(0, 0, 1),  glm::dvec3(0, 0, 0),
  };

  ohm::Voxel<const float> decay_rate_voxel(&map, map.layout().decayRateLayer());
  ASSERT_TRUE(decay_rate_voxel.isLayerValid());

  // Integrate and test one ray at a time.
  float expected_decay_rate = 0;
  for (size_t i = 0; i < rays.size(); i += 2)
  {
    mapper.integrateRays(&rays[i], 2);
    if (pre_validation)
    {
      pre_validation();
    }
    expected_decay_rate += float(0.5 * map.resolution());
    const auto key = map.voxelKey(glm::dvec3(0));
    decay_rate_voxel.setKey(key);
    ASSERT_TRUE(decay_rate_voxel.isValid());
    EXPECT_NEAR(decay_rate_voxel.data(), expected_decay_rate, 1e-3f);
  }
}

void testThrough(ohm::OccupancyMap &map, ohm::RayMapper &mapper, std::function<void()> pre_validation)
{
  ASSERT_GT(map.layout().decayRateLayer(), 0);

  // Offset the map so that we can cast rays through the origin without incurring floating point ambituity on the target
  // voxel.
  map.setOrigin(glm::dvec3(-0.5f * map.resolution()));

  // Create the rays we are to cast.
  const std::vector<glm::dvec3> rays = { glm::dvec3(-1, 0, 0), glm::dvec3(1, 0, 0),  glm::dvec3(0, -1, 0),
                                         glm::dvec3(0, 1, 0),  glm::dvec3(0, 0, -1), glm::dvec3(0, 0, 1) };

  ohm::Voxel<const float> decay_rate_voxel(&map, map.layout().decayRateLayer());
  ASSERT_TRUE(decay_rate_voxel.isLayerValid());

  // Integrate and test one ray at a time.
  float expected_decay_rate = 0;
  for (size_t i = 0; i < rays.size(); i += 2)
  {
    mapper.integrateRays(&rays[i], 2);
    if (pre_validation)
    {
      pre_validation();
    }
    expected_decay_rate += float(map.resolution());
    const auto key = map.voxelKey(glm::dvec3(0));
    decay_rate_voxel.setKey(key);
    ASSERT_TRUE(decay_rate_voxel.isValid());
    EXPECT_NEAR(decay_rate_voxel.data(), expected_decay_rate, 1e-3f);
  }
}
}  // namespace decay
}  // namespace ohmtestcommon
