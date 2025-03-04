// Copyright (c) 2020
// Commonwealth Scientific and Industrial Research Organisation (CSIRO)
// ABN 41 687 119 230
//
// Author: Kazys Stepanas
#include "RayMapperTrace.h"

#include "CalculateSegmentKeys.h"
#include "Key.h"
#include "KeyList.h"
#include "MapLayout.h"
#include "MapRegionCache.h"
#include "OccupancyMap.h"
#include "Trace.h"
#include "VoxelData.h"

#include "private/OccupancyMapDetail.h"

#ifdef TES_ENABLE
#include <3esmeshmessages.h>
#include <3esservermacros.h>
#include <shapes/3esmeshresource.h>

#include <glm/gtc/type_ptr.hpp>

#include <unordered_set>

namespace ohm
{
using KeyToIndexMap = std::unordered_map<Key, uint32_t, Key::Hash>;
using KeySet = std::unordered_set<Key, Key::Hash>;

struct OccupancyMeshDetail
{
  OccupancyMap *map;
  uint32_t id;

  std::vector<tes::Vector3d> vertices;
  // Define the render extents for the voxels.
  std::vector<tes::Vector3d> normals;
  std::vector<uint32_t> colours;
  /// Tracks indices of unused vertices in the vertex array.
  std::vector<uint32_t> unused_vertex_list;
  /// Maps voxel keys to their vertex indices.
  KeyToIndexMap voxel_index_map;
};

/// Defines and maintains a 3rd Eye Scene mesh resource based on an octomap.
///
/// Renders as a point cloud of occupied voxels.
class OccupancyMesh : public tes::MeshResource
{
public:
  explicit OccupancyMesh(OccupancyMap *map);
  ~OccupancyMesh() override;

  uint32_t id() const override;
  tes::Transform transform() const override;
  uint32_t tint() const override;
  uint8_t drawType(int stream) const override;

  unsigned vertexCount(int stream) const override;
  unsigned indexCount(int stream) const override;

  tes::DataBuffer vertices(int stream) const override;
  tes::DataBuffer indices(int stream) const override;
  tes::DataBuffer normals(int stream) const override;
  tes::DataBuffer uvs(int stream) const override;
  tes::DataBuffer colours(int stream) const override;

  tes::Resource *clone() const override;

  int transfer(tes::PacketWriter &packet, unsigned byte_limit, tes::TransferProgress &progress) const override;

  /// Updates noted changes to the debug view.
  /// @param newly_occupied Keys of voxels which have become occupied from free or uncertain since the last update.
  /// @param newly_free Keys of voxels which have become free from occupied since the last update.
  /// @param touched_occupied Keys of voxels which have changed occupied probability.
  void update(const KeySet &newly_occupied, const KeySet &newly_free, const KeySet &touched_occupied);

private:
  std::unique_ptr<OccupancyMeshDetail> imp_;
};

/// Assigns a colour to a voxel based on it's occupancy value.
/// @param occupancy_voxel The voxel of interest. Must be valid.
uint32_t voxelColour(const Voxel<const float> &occupancy_voxel)
{
  float occupancy;
  if (occupancy_voxel.isValid())
  {
    occupancy_voxel.read(&occupancy);
    occupancy = valueToProbability(occupancy);
    const auto intensity = float((occupancy - occupancy_voxel.map()->occupancyThresholdProbability()) /
                                 (1.0 - occupancy_voxel.map()->occupancyThresholdProbability()));
    const int c = int(255 * intensity);
    return tes::Colour(c, c, c).c;
  }
  return tes::Colour(0, 0, 0).c;
}

OccupancyMesh::OccupancyMesh(OccupancyMap *map)
  : imp_(std::make_unique<OccupancyMeshDetail>())
{
  imp_->map = map;
  imp_->id = tes::Id(this).id();
}

OccupancyMesh::~OccupancyMesh() = default;

uint32_t OccupancyMesh::id() const
{
  return imp_->id;
}

tes::Transform OccupancyMesh::transform() const
{
  return tes::Transform::identity(true);
}

uint32_t OccupancyMesh::tint() const
{
  return tes::Colour::Colours[tes::Colour::White].c;
}

uint8_t OccupancyMesh::drawType(int stream) const
{
  TES_UNUSED(stream);
  return tes::DtVoxels;
}

unsigned OccupancyMesh::vertexCount(int stream) const
{
  TES_UNUSED(stream);
  return unsigned(imp_->vertices.size());
}

unsigned OccupancyMesh::indexCount(int stream) const
{
  TES_UNUSED(stream);
  return 0;
}

tes::DataBuffer OccupancyMesh::vertices(int stream) const
{
  TES_UNUSED(stream);
  return tes::DataBuffer(imp_->vertices);
}

tes::DataBuffer OccupancyMesh::indices(int stream) const
{
  TES_UNUSED(stream);
  return tes::DataBuffer();
}

tes::DataBuffer OccupancyMesh::normals(int stream) const
{
  TES_UNUSED(stream);
  return tes::DataBuffer(imp_->normals);
}

tes::DataBuffer OccupancyMesh::uvs(int stream) const
{
  TES_UNUSED(stream);
  return tes::DataBuffer();
}

tes::DataBuffer OccupancyMesh::colours(int stream) const
{
  TES_UNUSED(stream);
  return tes::DataBuffer(imp_->colours);
}

tes::Resource *OccupancyMesh::clone() const
{
  auto *copy = new OccupancyMesh(imp_->map);
  *copy->imp_ = *imp_;
  return copy;
}

int OccupancyMesh::transfer(tes::PacketWriter &packet, unsigned byte_limit, tes::TransferProgress &progress) const
{
  // Build the voxel set if required.
  if (imp_->voxel_index_map.empty())
  {
    imp_->vertices.clear();
    imp_->colours.clear();
    Voxel<const float> occupancy_voxel(imp_->map, imp_->map->layout().occupancyLayer());
    const OccupancyMap &map = *imp_->map;
    for (auto iter = map.begin(); iter != map.end(); ++iter)
    {
      occupancy_voxel.setKey(iter);
      if (isOccupied(occupancy_voxel))
      {
        // Add voxel.
        imp_->voxel_index_map.insert(std::make_pair(*iter, uint32_t(imp_->vertices.size())));
        imp_->vertices.emplace_back(glm::value_ptr(map.voxelCentreGlobal(*iter)));
        // Normals represent voxel half extents.
        imp_->normals.emplace_back(0.5 * map.resolution());
        imp_->colours.push_back(voxelColour(occupancy_voxel));
      }
    }
  }

  return tes::MeshResource::transfer(packet, byte_limit, progress);
}

void OccupancyMesh::update(const KeySet &newly_occupied, const KeySet &newly_free, const KeySet &touched_occupied)
{
  if (newly_occupied.empty() && newly_free.empty() && touched_occupied.empty())
  {
    // Nothing to do.
    return;
  }

  if (g_tes->connectionCount() == 0)
  {
    // No-one to send to.
    imp_->vertices.clear();
    imp_->normals.clear();
    imp_->colours.clear();
    // indices.clear();
    imp_->unused_vertex_list.clear();
    imp_->voxel_index_map.clear();
    return;
  }

  // Start by removing freed nodes.
  size_t initial_unused_vertex_count = imp_->unused_vertex_list.size();
  std::vector<uint32_t> modified_vertices;
  for (const auto &key : newly_free)
  {
    // Resolve the index for this voxel.
    auto voxel_lookup = imp_->voxel_index_map.find(key);
    if (voxel_lookup != imp_->voxel_index_map.end())
    {
      // Invalidate the voxel.
      imp_->colours[voxel_lookup->second] = 0u;
      imp_->unused_vertex_list.push_back(voxel_lookup->second);
      modified_vertices.push_back(voxel_lookup->second);
      imp_->voxel_index_map.erase(voxel_lookup);
    }
  }

  // Now added occupied nodes, initially from the free list.
  Voxel<const float> occupancy_voxel(imp_->map, imp_->map->layout().occupancyLayer());
  size_t processed_occupied_count = 0;
  auto occupied_iter = newly_occupied.begin();
  while (!imp_->unused_vertex_list.empty() && occupied_iter != newly_occupied.end())
  {
    const uint32_t vertex_index = imp_->unused_vertex_list.back();
    const Key key = *occupied_iter;
    const bool mark_as_modified = imp_->unused_vertex_list.size() <= initial_unused_vertex_count;
    imp_->unused_vertex_list.pop_back();
    ++occupied_iter;
    ++processed_occupied_count;
    occupancy_voxel.setKey(key);
    imp_->vertices[vertex_index] = tes::Vector3d(glm::value_ptr(imp_->map->voxelCentreGlobal(key)));
    imp_->colours[vertex_index] = voxelColour(occupancy_voxel);
    imp_->voxel_index_map.insert(std::make_pair(key, vertex_index));
    // Only mark as modified if this vertex wasn't just invalidate by removal.
    // It will already be on the list otherwise.
    if (mark_as_modified)
    {
      modified_vertices.push_back(vertex_index);
    }
  }

  // Send messages for individually changed voxels.
  // Start a mesh redefinition message.
  std::vector<uint8_t> buffer(0xffffu);  // NOLINT(readability-magic-numbers)
  tes::PacketWriter packet(buffer.data(), static_cast<uint16_t>(buffer.size()));
  tes::MeshRedefineMessage msg{};
  tes::MeshComponentMessage cmpmsg{};
  tes::MeshFinaliseMessage finalmsg{};
  tes::ObjectAttributesd attributes{};
  tes::DataBuffer data_buffer;

  // Work out how many vertices we'll have after all modifications are done.
  size_t old_vertex_count = imp_->vertices.size();
  size_t new_vertex_count = imp_->vertices.size();
  if (newly_occupied.size() - processed_occupied_count > imp_->unused_vertex_list.size())
  {
    // We have more occupied vertices than available in the free list.
    // This means we will add new vertices.
    new_vertex_count += newly_occupied.size() - processed_occupied_count - imp_->unused_vertex_list.size();
  }

  msg.meshId = imp_->id;
  msg.vertexCount = uint32_t(new_vertex_count);
  msg.indexCount = 0;
  msg.drawType = drawType(0);
  attributes.identity();

  packet.reset(tes::MtMesh, tes::MeshRedefineMessage::MessageId);
  msg.write(packet, attributes);

  packet.finalise();
  g_tes->send(packet);

  // Next update changed triangles.
  cmpmsg.meshId = id();

  // Update modified vertices, one at a time.
  for (uint32_t vertex_index : modified_vertices)
  {
    packet.reset(tes::MtMesh, tes::MmtVertex);
    cmpmsg.write(packet);
    data_buffer = tes::DataBuffer(&imp_->vertices[vertex_index], 1);
    data_buffer.write(packet, 0, packet.bytesRemaining(), vertex_index);
    packet.finalise();
    g_tes->send(packet);

    // Send colour and position update.
    packet.reset(tes::MtMesh, tes::MmtVertexColour);
    cmpmsg.write(packet);
    data_buffer = tes::DataBuffer(&imp_->colours[vertex_index], 1);
    data_buffer.write(packet, 0, packet.bytesRemaining(), vertex_index);
    packet.finalise();
    g_tes->send(packet);
  }

  // Add remaining vertices and send a bulk modification message.
  // Continue iteration from where we left off.
  for (; occupied_iter != newly_occupied.end(); ++occupied_iter, ++processed_occupied_count)
  {
    const auto vertex_index = uint32_t(imp_->vertices.size());
    const Key key = *occupied_iter;
    imp_->voxel_index_map.insert(std::make_pair(key, vertex_index));
    imp_->vertices.emplace_back(glm::value_ptr(imp_->map->voxelCentreGlobal(key)));
    // Normals represent voxel half extents.
    imp_->normals.emplace_back(0.5 * imp_->map->resolution());
    imp_->colours.emplace_back(tes::Colour::Colours[tes::Colour::White].c);
  }

  // Send bulk messages for new vertices.
  if (old_vertex_count != new_vertex_count)
  {
    // Send colour and position update.
    auto offset = uint32_t(old_vertex_count);
    data_buffer = tes::DataBuffer(imp_->vertices);

    while (offset < new_vertex_count)
    {
      packet.reset(tes::MtMesh, tes::MmtVertex);
      cmpmsg.write(packet);
      offset += data_buffer.writePacked(packet, offset, 0.5 * imp_->map->resolution(), packet.bytesRemaining());
      packet.finalise();
      g_tes->send(packet);
    }

    offset = uint32_t(old_vertex_count);
    data_buffer = tes::DataBuffer(imp_->normals);
    while (offset < new_vertex_count)
    {
      packet.reset(tes::MtMesh, tes::MmtNormal);
      cmpmsg.write(packet);
      offset += data_buffer.write(packet, offset, packet.bytesRemaining());
      packet.finalise();
      g_tes->send(packet);
    }

    offset = uint32_t(old_vertex_count);
    data_buffer = tes::DataBuffer(imp_->colours);
    while (offset < new_vertex_count)
    {
      packet.reset(tes::MtMesh, tes::MmtVertexColour);
      cmpmsg.write(packet);
      offset += data_buffer.write(packet, offset, packet.bytesRemaining());
      packet.finalise();
      g_tes->send(packet);
    }
  }

  // Update colours for touched occupied
  if (!touched_occupied.empty())
  {
    for (const auto &key : touched_occupied)
    {
      occupancy_voxel.setKey(key);
      auto index_search = imp_->voxel_index_map.find(key);
      if (index_search != imp_->voxel_index_map.end())
      {
        const unsigned voxel_index = index_search->second;
        imp_->colours[voxel_index] = voxelColour(occupancy_voxel);

        packet.reset(tes::MtMesh, tes::MmtVertexColour);
        cmpmsg.write(packet);
        data_buffer = tes::DataBuffer(&imp_->colours[voxel_index], 1);
        data_buffer.write(packet, 0, packet.bytesRemaining(), voxel_index);
        packet.finalise();
        g_tes->send(packet);
      }
    }
  }

  // Finalise the modifications.
  finalmsg.meshId = imp_->id;
  finalmsg.flags = 0;
  packet.reset(tes::MtMesh, finalmsg.MessageId);
  finalmsg.write(packet);
  packet.finalise();
  g_tes->send(packet);
}


/// Draw an NDT visualisation for the given "sector key".
///
/// This sends ellipsoids for the occupied voxels in the sector. See @p RayMapperTrace::SectorSet .
void drawNdt(const glm::i16vec4 &sector_key, const OccupancyMap &map)
{
  std::vector<tes::Sphere> ellipsoids;
  std::vector<tes::Shape *> ellipsoid_ptrs;

  const glm::i16vec3 region_key(sector_key);
  const MapChunk *chunk = map.region(region_key);

  if (chunk)
  {
    // Extract occupied voxels in the region
    Voxel<const float> occ_voxel(&map, map.layout().occupancyLayer());
    Voxel<const VoxelMean> mean_voxel(&map, map.layout().meanLayer());
    Voxel<const CovarianceVoxel> cov_voxel(&map, map.layout().covarianceLayer());

    const auto dim = map.regionVoxelDimensions();
    const tes::Id shape_id = tes::Id(chunk, kTcNdt) + sector_key.w;  // Each sector has a unique key

    // Work out the sector indexing range.
    glm::ivec3 start_index(0);
    glm::ivec3 end_index(0);

    for (int i = 0; i < 3; ++i)
    {
      start_index[i] = !!(sector_key.w & (1 << i));  // NOLINT(hicpp-signed-bitwise)
      end_index[i] = (start_index[i]) ? dim[i] : dim[i] / 2;
      start_index[i] = (start_index[i]) ? dim[i] / 2 : 0;
    }

    for (int z = start_index.z; z < end_index.z; ++z)
    {
      for (int y = start_index.y; y < end_index.y; ++y)
      {
        for (int x = start_index.x; x < end_index.x; ++x)
        {
          const Key key(region_key, x, y, z);
          setVoxelKey(key, occ_voxel, mean_voxel, cov_voxel);
          if (isOccupied(occ_voxel))
          {
            const CovarianceVoxel cov_info = cov_voxel.data();
            glm::dquat rotation;
            glm::dvec3 scale;
            const glm::dvec3 pos = positionUnsafe(mean_voxel);
            if (covarianceUnitSphereTransformation(&cov_info, &rotation, &scale))
            {
              // Use single precision for smaller data size
              ellipsoids.emplace_back(
                tes::Sphere(shape_id, tes::Transform(tes::Vector3f(glm::value_ptr(pos)),
                                                     tes::Quaternionf(float(rotation.x), float(rotation.y),
                                                                      float(rotation.z), float(rotation.w)),
                                                     tes::Vector3f(glm::value_ptr(scale)))));
              ellipsoids.back().setColour(tes::Colour::Colours[tes::Colour::MediumSeaGreen]);
            }
          }
        }
      }
    }

    if (!ellipsoids.empty())
    {
      for (tes::Sphere &shape : ellipsoids)
      {
        ellipsoid_ptrs.emplace_back(&shape);
      }

      // Create the multi-shape to replace the existing one for the region.
      g_tes->create(tes::MultiShape(ellipsoid_ptrs.data(), ellipsoid_ptrs.size()).setReplace(true));
    }
    else
    {
      // Destroy any existing representation for this region.
      g_tes->destroy(tes::Sphere(shape_id));
    }
  }
}

/// Draw an NDT-TM visualisation for the given "sector key".
///
/// This sends ellipsoids for the occupied voxels in the sector. See @p RayMapperTrace::SectorSet .
void drawNdtTm(const glm::i16vec4 &sector_key, const OccupancyMap &map)
{
  std::vector<tes::Sphere> ellipsoids;
  std::vector<tes::Shape *> ellipsoid_ptrs;
  static float min_intensity = std::numeric_limits<float>::infinity(),
               max_intensity = -std::numeric_limits<float>::infinity();

  const glm::i16vec3 region_key(sector_key);
  const MapChunk *chunk = map.region(region_key);

  if (chunk)
  {
    // Extract occupied voxels in the region
    Voxel<const float> occ_voxel(&map, map.layout().occupancyLayer());
    Voxel<const VoxelMean> mean_voxel(&map, map.layout().meanLayer());
    Voxel<const CovarianceVoxel> cov_voxel(&map, map.layout().covarianceLayer());
    Voxel<const IntensityMeanCov> intensity_voxel(&map, map.layout().intensityLayer());
    Voxel<const HitMissCount> hit_miss_voxel(&map, map.layout().hitMissCountLayer());

    const tes::Id shape_id = tes::Id(chunk, kTcNdt) + sector_key.w;  // Each sector has a unique key

    // Work out the sector indexing range.
    glm::ivec3 start_index(0);
    glm::ivec3 end_index(0);

    for (int i = 0; i < 3; ++i)
    {
      start_index[i] = !!(sector_key.w & (1 << i));
      end_index[i] = (start_index[i]) ? map.regionVoxelDimensions()[i] : map.regionVoxelDimensions()[i] / 2;
      start_index[i] = (start_index[i]) ? map.regionVoxelDimensions()[i] / 2 : 0;
    }

    for (int z = start_index.z; z < end_index.z; ++z)
    {
      for (int y = start_index.y; y < end_index.y; ++y)
      {
        for (int x = start_index.x; x < end_index.x; ++x)
        {
          const Key key(region_key, x, y, z);
          setVoxelKey(key, occ_voxel, mean_voxel, cov_voxel, intensity_voxel, hit_miss_voxel);
          if (isOccupied(occ_voxel))
          {
            const CovarianceVoxel cov_info = cov_voxel.data();
            glm::dquat rotation;
            glm::dvec3 scale;
            const glm::dvec3 pos = positionUnsafe(mean_voxel);
            const IntensityMeanCov intensity_mean_cov = intensity_voxel.data();
            min_intensity = std::fmin(min_intensity, intensity_mean_cov.intensity_mean);
            max_intensity = std::fmax(max_intensity, intensity_mean_cov.intensity_mean);
            const float scaled_intensity =
              float(M_PI) * (-1.0f + 1.5f * (intensity_mean_cov.intensity_mean - min_intensity) /
                                       std::fmax(1.0f, max_intensity - min_intensity));
            const float sin_sc = std::sin(scaled_intensity), cos_sc = std::cos(scaled_intensity);
            const HitMissCount hit_miss_count = hit_miss_voxel.data();

            if (covarianceUnitSphereTransformation(&cov_info, &rotation, &scale))
            {
              // Use single precision for smaller data size
              ellipsoids.emplace_back(
                tes::Sphere(shape_id, tes::Transform(tes::Vector3f(glm::value_ptr(pos)),
                                                     tes::Quaternionf((float)rotation.x, (float)rotation.y,
                                                                      (float)rotation.z, (float)rotation.w),
                                                     tes::Vector3f(glm::value_ptr(scale)))));
              const float alpha = 0.9f * float(hit_miss_count.hit_count) /
                                  std::fmax(1.0f, float(hit_miss_count.hit_count + hit_miss_count.miss_count));
              ellipsoids.back().setColour(tes::Colour(0.1f + alpha * 0.5f * (1.0f + sin_sc),
                                                      0.1f + alpha * 0.5f * (1.0f + cos_sc),
                                                      0.1f + alpha * 0.5f * (1.0f - sin_sc), 1.0f));
            }
          }
        }
      }
    }

    if (!ellipsoids.empty())
    {
      for (tes::Sphere &shape : ellipsoids)
      {
        ellipsoid_ptrs.emplace_back(&shape);
      }

      // Create the multi-shape to replace the existing one for the region.
      g_tes->create(tes::MultiShape(ellipsoid_ptrs.data(), ellipsoid_ptrs.size()).setReplace(true));
    }
    else
    {
      // Destroy any existing representation for this region.
      g_tes->destroy(tes::Sphere(shape_id));
    }
  }
}
}  // namespace ohm

#else   // TES_ENABLE
namespace ohm
{
class OccupancyMesh
{
public:
  OccupancyMesh(OccupancyMap *) {}
};
}  // namespace ohm
#endif  // TES_ENABLE

namespace ohm
{
RayMapperTrace::RayMapperTrace(OccupancyMap *map, RayMapper *true_mapper)
  : map_(map)
  , true_mapper_(true_mapper)
  , imp_(std::make_unique<OccupancyMesh>(map))
{
#ifdef TES_ENABLE
  if (g_tes)
  {
    // Instantiate the mesh resource.
    g_tes->referenceResource(imp_.get());
    // Create the mesh object representation.
    g_tes->create(tes::MeshSet(imp_.get(), tes::Id(this, kTcVoxels)));

    // We need to run an update to ensure the mesh is created before we start trying to update it.
    TES_SERVER_UPDATE(g_tes, 0.0f);
  }
#endif  // TES_ENABLE
}


RayMapperTrace::~RayMapperTrace()
{
#ifdef TES_ENABLE
  if (g_tes)
  {
    // Release the mesh object representation.
    g_tes->destroy(tes::MeshSet(imp_.get(), tes::Id(this, kTcVoxels)));
    // Release the mesh resource.
    g_tes->releaseResource(imp_.get());
  }
#endif  // TES_ENABLE
}


bool RayMapperTrace::valid() const
{
  return true_mapper_ != nullptr && true_mapper_->valid();
}

size_t RayMapperTrace::integrateRays(const glm::dvec3 *rays, size_t element_count, const float *intensities,
                                     const double *timestamps, unsigned ray_update_flags)
{
#ifdef TES_ENABLE
  // Walk all the rays and cache the state of the (predicted) touched voxels.
  VoxelMap initial_state;
  VoxelMap updated_state;
  SectorSet sector_set;

  if (g_tes && element_count)
  {
    cacheState(rays, element_count, &initial_state, &sector_set);
  }
#endif  // TES_ENABLE

  const size_t result = true_mapper_->integrateRays(rays, element_count, intensities, timestamps, ray_update_flags);

#ifdef TES_ENABLE
  if (g_tes && element_count)
  {
    // Sync gpu cache to CPU
    if (map_->detail()->gpu_cache)
    {
      map_->detail()->gpu_cache->flush();
    }

    // Draw the rays
    g_tes->create(tes::MeshShape(tes::DtLines, tes::Id(0u, kTcRays),
                                 tes::DataBuffer(&rays->x, element_count, 3, sizeof(*rays) / sizeof(rays->x)))
                    .setColour(tes::Colour::Colours[tes::Colour::Yellow]));

    cacheState(rays, element_count, &updated_state);

    // Determine changes.
    KeySet newly_occupied;
    KeySet newly_freed;
    KeySet touched_occupied;

    for (const auto &voxel_info : updated_state)
    {
      const auto initial_info = initial_state.find(voxel_info.first);
      if (initial_info == initial_state.end())
      {
        // Something weird has happened. Let's ignore it!
        continue;
      }

      const OccupancyType initial_type = initial_info->second.type;
      if (voxel_info.second.type != initial_type)
      {
        // State has changed.
        switch (voxel_info.second.type)
        {
        case kOccupied:
          newly_occupied.insert(voxel_info.first);
          break;
        case kFree:
          if (initial_type == kOccupied)
          {
            newly_freed.insert(voxel_info.first);
          }
          break;
        default:
          // nothing to do here
          break;
        }
      }
      else if (voxel_info.second.type == kOccupied)
      {
        // Touched
        touched_occupied.insert(voxel_info.first);
      }
    }

    // Update the mesh changes
    imp_->update(newly_occupied, newly_freed, touched_occupied);

    // Update NDT representation.
    if (!sector_set.empty() && map_->layout().covarianceLayer())
    {
      for (const auto &sector_key : sector_set)
      {
        if (map_->layout().intensityLayer() >= 0 && map_->layout().hitMissCountLayer() >= 0)
        {
          drawNdtTm(sector_key, *map_);
        }
        else
        {
          drawNdt(sector_key, *map_);
        }
      }
    }

    // Do full update (with connection management)
    TES_SERVER_UPDATE(g_tes, 0.0f);
  }
#endif  // TES_ENABLE

  return result;
}


glm::i16vec4 RayMapperTrace::sectorKey(const Key &key) const
{
  // We divide the MapChunk into 8 sectors, like a voxel. We need to convert the local key into a sector index
  glm::ivec3 local = key.localKey();
  // Each axis is converted to 0 or 1
  local[0] = !!(local[0] >= (map_->regionVoxelDimensions()[0] / 2));
  local[1] = !!(local[1] >= (map_->regionVoxelDimensions()[1] / 2));
  local[2] = !!(local[2] >= (map_->regionVoxelDimensions()[2] / 2));

  // Linearise the new key as the w value for the return key.
  glm::i16vec4 sector_key(key.regionKey(), local.x + local.y * 2 + local.z * 4);

  return sector_key;
}


void RayMapperTrace::cacheState(const glm::dvec3 *rays, size_t element_count, VoxelMap *voxels, SectorSet *sectors)
{
  KeyList keys;

  // Setup voxel references
  Voxel<const float> occupancy_voxel(map_, map_->layout().occupancyLayer());
  Voxel<const VoxelMean> mean_voxel(map_, map_->layout().meanLayer());
  Voxel<const CovarianceVoxel> covariance_voxel(map_, map_->layout().covarianceLayer());

  for (size_t ri = 0; ri < element_count / 2; ++ri)
  {
    keys.clear();
    calculateSegmentKeys(keys, *map_, rays[ri * 2 + 0], rays[ri * 2 + 1], true);

    // Walk the ray keys
    for (const auto &key : keys)
    {
      if (sectors)
      {
        sectors->insert(sectorKey(key));
      }

      if (voxels->find(key) == voxels->end())
      {
        // not already in the set.
        setVoxelKey(key, occupancy_voxel, mean_voxel, covariance_voxel);

        VoxelState voxel_info;
        voxel_info.type = occupancyType(occupancy_voxel);

        if (voxel_info.type == kOccupied && covariance_voxel.isValid() && mean_voxel.isValid())
        {
          CovarianceVoxel cov;
          covariance_voxel.read(&cov);

          voxel_info.ellipse_pos = positionUnsafe(mean_voxel);
          covarianceUnitSphereTransformation(&cov, &voxel_info.ellipse_rotation, &voxel_info.ellipse_scale);
        }

        voxels->insert(std::make_pair(key, voxel_info));
      }
    }
  }
}
}  // namespace ohm
