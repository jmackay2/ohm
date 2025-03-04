// Copyright (c) 2019
// Commonwealth Scientific and Industrial Research Organisation (CSIRO)
// ABN 41 687 119 230
//
// Author: Kazys Stepanas
#include "MapSerialiseV0.h"

#include "private/OccupancyMapDetail.h"
#include "private/SerialiseUtil.h"

#include "DefaultLayer.h"
#include "MapChunk.h"
#include "MapFlag.h"
#include "MapLayer.h"
#include "MapSerialise.h"
#include "Stream.h"
#include "VoxelBlock.h"
#include "VoxelBuffer.h"

namespace ohm
{
namespace v0
{
int load(InputStream &stream, OccupancyMapDetail &detail, SerialiseProgress *progress, const MapVersion & /*version*/,
         size_t region_count)
{
  int err = kSeOk;

  detail.setDefaultLayout(MapFlag::kNone);
  addClearance(detail.layout);

  if (progress)
  {
    if (region_count)
    {
      progress->setTargetProgress(unsigned(region_count));
    }
    else
    {
      progress->setTargetProgress(unsigned(1));
      progress->incrementProgress();
    }
  }

  MapChunk *chunk = nullptr;
  for (unsigned i = 0; i < region_count && (!progress || !progress->quit()); ++i)
  {
    chunk = new MapChunk(detail);
    err = v0::loadChunk(stream, *chunk, detail);
    if (err)
    {
      delete chunk;
      return err;
    }

    // Resolve map chunk details.
    chunk->searchAndUpdateFirstValid(detail.region_voxel_dimensions);
    detail.chunks.insert(std::make_pair(chunk->region.coord, chunk));

    if (progress)
    {
      progress->incrementProgress();
    }
  }

  return err;
}


// Version zero chunk loading
int loadChunk(InputStream &stream, MapChunk &chunk, const OccupancyMapDetail &detail)
{
  bool ok = true;

  const MapLayer *occupancy_layer = chunk.layout().layerPtr(chunk.layout().occupancyLayer());
  const MapLayer *clearance_layer = chunk.layout().layerPtr(chunk.layout().clearanceLayer());
  // Use a hard coded reference to the legacy layer "coarseClearance". The layer was never used anywhere.
  const MapLayer *coarse_clearance_layer = chunk.layout().layer("coarseClearance");

  if (coarse_clearance_layer)
  {
    VoxelBuffer<VoxelBlock> voxel_buffer(chunk.voxel_blocks[coarse_clearance_layer->layerIndex()]);
    memset(voxel_buffer.voxelMemory(), 0, voxel_buffer.voxelMemorySize());
  }

  // Write region details, then nodes. MapChunk members are derived.
  ok = read<int32_t>(stream, chunk.region.coord.x) && ok;
  ok = read<int32_t>(stream, chunk.region.coord.y) && ok;
  ok = read<int32_t>(stream, chunk.region.coord.z) && ok;
  ok = read<double>(stream, chunk.region.centre.x) && ok;
  ok = read<double>(stream, chunk.region.centre.y) && ok;
  ok = read<double>(stream, chunk.region.centre.z) && ok;
  ok = read<double>(stream, chunk.touched_time) && ok;

  const unsigned node_count =
    detail.region_voxel_dimensions.x * detail.region_voxel_dimensions.y * detail.region_voxel_dimensions.z;
  const size_t node_byte_count = 2 * sizeof(float) * node_count;
  if (node_byte_count != unsigned(node_byte_count))
  {
    return kSeValueOverflow;
  }

  if (ok)
  {
    // Initial version used MapNode which contained two floats.
    // This interleaves occupancy/clearance, so we need to pull them out.
    std::vector<float> node_data(node_count * 2);
    ok = stream.read(node_data.data(), unsigned(node_byte_count)) == node_byte_count && ok;

    VoxelBuffer<VoxelBlock> occupancy_buffer(chunk.voxel_blocks[occupancy_layer->layerIndex()]);
    VoxelBuffer<VoxelBlock> clearance_buffer(chunk.voxel_blocks[clearance_layer->layerIndex()]);

    for (unsigned i = 0; i < unsigned(node_data.size() / 2); ++i)
    {
      // Unnecessary casting for clang-tidy
      occupancy_buffer.writeVoxel(i, node_data[unsigned((i << 1u) + 0u)]);
      clearance_buffer.writeVoxel(i, node_data[unsigned((i << 1u) + 1u)]);
    }
  }

  return (ok) ? 0 : kSeFileReadFailure;
}
}  // namespace v0
}  // namespace ohm
