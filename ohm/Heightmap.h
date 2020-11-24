// Copyright (c) 2018
// Commonwealth Scientific and Industrial Research Organisation (CSIRO)
// ABN 41 687 119 230
//
// Author: Kazys Stepanas
#ifndef HEIGHTMAP_H
#define HEIGHTMAP_H

#include "OhmConfig.h"

#include "Aabb.h"
#include "HeightmapVoxelType.h"
#include "UpAxis.h"

#include <memory>

#include <glm/fwd.hpp>

#include <vector>

namespace ohm
{
class Key;
class MapInfo;
class OccupancyMap;
class VoxelConst;
struct HeightmapDetail;
struct HeightmapVoxel;

/// A 2D voxel map variant which calculates a heightmap surface from another @c OccupancyMap .
///
/// The heightmap is built from an @c OccupancyMap and forms an axis aligned collapse of that map. The up axis may be
/// specified on construction of the heightmap, but must be aligned to a primary axis. The heightmap is built in
/// its own @c OccupancyMap , which consists of a single layer of voxels. The @c MapLayout for the heightmap is
/// two layers:
/// - **occupancy** layer
///   - float occupancy
/// - *heightmap* layer (named from @c HeightmapVoxel::kHeightmapLayer )
///   - @c HeightmapVoxel
///
/// The height specifies the absolute height of the surface, while clearance denotes how much room there is above
/// the surface voxel before the next obstruction. Note that the height values always increase going up, so the
/// height value will be inverted when using any @c UpAxis::kNegN @c UpAxis value. Similarly, the clearance is always
/// positive unless there are no further voxels above the surface, in which case the clearance is zero
/// (no information).
///
/// Each voxel in the heightmap represents a collapse of the source @c OccupancyMap based on a seed reference
/// position - see @c buildHeightmap() . The heightmap is generated by considering each column in the source map
/// relative to a reference height based on the seed position and neighbouring cells. When a valid supporting surface
/// is found, a heightmap voxel is marked as occupied and given a height associated with the supporting surface. This
/// supporting surface is the closest occupied voxel to the current reference position also having sufficient
/// clearance above it, @c minClearance() .
///
/// The heightmap may also generate a 'virtual surface' from the interface between uncertain and free voxels when
/// @c generateVirtualSurface() is set. A 'virtual surface' voxel is simply a free voxel with an uncertain voxel below
/// it, but only in a column which does not have an occupied voxel within the search range. Virtual surface voxels
/// are marked as free in the heightmap.
///
/// The heightmap is generated either using a planar seach or a flood fill from the reference position. The planar
/// search operates at a fixed reference height at each column, while the flood fill search height is dependent on
/// the height of neighbour voxels. The flood fill is better at following surfaces, however it is significantly
/// slower.
///
/// Some variables limit the search for a supporting voxel in each column. To be considered as a support candidate, a
/// voxel must;
///
/// - Lie within the extents given to @c buildHeightmap()
/// - Must not be higher than the @c ceiling() height above its starting search position.
///
/// The generated heightmap may be accessed via @c heightmap() and voxel positions should be retrieved using
/// @c getHeightmapVoxelPosition() .
///
/// The @c OccupancyMap used to represent the heightmap has additional meta data stored in its @c MapInfo :
/// - <b>heightmap</b> - Present and true if this is a heightmap.
/// - <b>heightmap-axis</b> - The up axis ID for a heightmap.
/// - <b>heightmap-axis-x</b> - The up axis X value for a heightmap.
/// - <b>heightmap-axis-y</b> - The up axis Y value for a heightmap.
/// - <b>heightmap-axis-z</b> - The up axis Z value for a heightmap.
/// - <b>heightmap-blur</b> - The blur value used to generate the heightamp.
/// - <b>heightmap-clearance</b> - The clearance value used to generate the heightamp.
class Heightmap
{
public:
  /// Size of regions in the heightmap. This is a 2D voxel extent. The region height is always one voxel.
  static const unsigned kDefaultRegionSize = 128;
  /// Voxel value assigned to heightmap cells which represent a real surface extracted from the source map.
  static constexpr float kHeightmapSurfaceValue = 1.0f;
  /// Voxel value assigned to heightmap cells which represent a virtual surface extracted from the source map.
  /// Virtual surfaces may be formed by the interface between a free voxel supported by an uncertain/null voxel.
  static constexpr float kHeightmapVirtualSurfaceValue = -1.0f;
  /// Voxel value assigned to heightmap cells which have no valid voxel in the entire column from the source map.
  static constexpr float kHeightmapVacantValue = 0.0f;

  /// Construct a default initialised heightmap.
  Heightmap();

  /// Construct a new heightmap optionally tied to a specific @p map .
  /// @param grid_resolution The grid resolution for the heightmap. Should match the source map for best results.
  /// @param min_clearance The minimum clearance value expected above each surface voxel.
  /// @param up_axis Identifies the up axis for the map.
  /// @param region_size Grid size of each region in the heightmap.
  Heightmap(double grid_resolution, double min_clearance, UpAxis up_axis = UpAxis::kZ, unsigned region_size = 0);

  /// Destructor.
  ~Heightmap();

  /// Set number of threads to use in heightmap generation, enabling multi-threaded code path as required.
  ///
  /// Setting the @p thread_count to zero enables multi-threading using the maximum number of threads. Setting the
  /// @p thread_count to 1 disables threads (default).
  ///
  /// Using multiple threads may not yield significant gains.
  ///
  /// @param thread_count The number of threads to set.
  /// @return True if mult-threading is available. False when no mult-threading is available and @p thread_count is
  /// ignored.
  bool setThreadCount(unsigned thread_count);

  /// Get the number of threads to use.
  ///
  /// - 0: use all available
  /// - 1: force single threaded, or no multi-threading is available.
  /// - n: Use n threads.
  /// @return The number of threads to use.
  unsigned threadCount() const;

  /// Set the occupancy map on which to base the heightmap. The heightmap does not take ownership of the pointer so
  /// the @p map must persist until @c buildHeightmap() is called.
  void setOccupancyMap(OccupancyMap *map);

  /// Access the current source occupancy map.
  OccupancyMap *occupancyMap() const;

  /// Access the currently generated heightmap.
  OccupancyMap &heightmap() const;

  /// Set the ceiling level. Points above this distance above the base height in the source map are ignored.
  /// @param ceiling The new ceiling value. Positive to enable.
  void setCeiling(double ceiling);

  /// Get the ceiling level. Points above this distance above the base height in the source map are ignored.
  /// @return The ceiling value.
  double ceiling() const;

  /// Set the minimum clearance required above a voxel in order to consider it a heightmap voxel.
  /// @param clearance The new clearance value.
  void setMinClearance(double clearance);

  /// Get the minimum clearance required above a voxel in order to consider it a heightmap voxel.
  /// @return The height clearance value.
  double minClearance() const;

  /// Sets whether voxel mean positions are ignored (true) forcing the use of voxel centres.
  /// @param ignore True to force voxel centres even when voxel mean positions are present.
  void setIgnoreVoxelMean(bool ignore);

  /// Force voxel centres even when voxel mean positions are present?
  /// @return True to ignore voxel mean positioning.
  /// @see @ref voxelmean
  bool ignoreVoxelMean() const;

  /// Set the generation of a heightmap floor around the transition from unknown to free voxels?
  ///
  /// This option allows a heightmap floor to be generated in columns where there is no clear occupied floor voxel.
  /// When enabled, the heightmap generates a floor level at the lowest transition point from unknown to free voxel.
  ///
  /// @param enable Enable this option?
  void setGenerateVirtualSurface(bool enable);

  /// Allow the generation of a heightmap floor around the transition from unknown to free voxels?
  ///
  /// @see @c setGenerateVirtualSurface()
  ///
  /// @return True if this option is enabled.
  bool generateVirtualSurface() const;

  /// Set whether virtual surface candidates below the reference position are preferred to real above.
  ///
  /// When building a heightmap column, the default behaviour is for virtual surfaces to be reported only if the
  /// search expanse does not include a real occupied voxel from which a real surface can be derived. This option
  /// changes the behaviour to make a virtual surface candidate which lies below the reference position a preferred
  /// seed candidate to an occupied voxel which lies above the reference position. This can generate better ground
  /// results where the ground cannot be properly observed.
  ///
  /// @param enable True to promote virtual candidate below the reference position.
  void setPromoteVirtualBelow(bool enable);

  /// Query whether virtual surface voxels below the reference position are preferred to real voxels above.
  ///
  /// @see @c setPromoteVirtualBelow()
  ///
  /// @return True to prefer virtual voxels below the reference position.
  bool promoteVirtualBelow() const;

  /// Set the heightmap generation to flood fill ( @c true ) or planar ( @c false ).
  /// @param flood_fill True to enable the flood fill technique.
  void setUseFloodFill(bool flood_fill);

  /// Is the flood fill generation technique in use ( @c true ) or planar technique ( @c false ).
  /// @return True when using flood fill.
  bool useFloodFill() const;

  /// The layer number which contains @c HeightmapVoxel structures.
  /// @return The heightmap layer index or -1 on error (not present).
  /// @see @ref voxelmean
  int heightmapVoxelLayer() const;

  /// The layer number which contains @c HeightmapVoxel structures during heightmap construction.
  /// @return The heightmap build layer index or -1 on error (not present).
  int heightmapVoxelBuildLayer() const;

  /// Get the up axis identifier used to generate the heightmap.
  UpAxis upAxis() const;

  /// Get the up axis index [0, 2] marking XYZ respectively. Ignores direction.
  int upAxisIndex() const;

  /// Get the normal vector for the up axis used with last @c buildHeightmap() .
  const glm::dvec3 &upAxisNormal() const;

  /// Component index of the first surface axis normal [0, 2].
  int surfaceAxisIndexA() const;

  /// Get a unit vector which lies along the surface of the heightmap, perpendicular to @c surfaceAxisB() and
  /// upAxisNormal().
  const glm::dvec3 &surfaceAxisA() const;

  /// Component of the second surface axis normal [0, 2].
  int surfaceAxisIndexB() const;

  /// Get a unit vector which lies along the surface of the heightmap, perpendicular to @c surfaceAxisA() and
  /// upAxisNormal().
  const glm::dvec3 &surfaceAxisB() const;

  /// Static resolution of @c Axis to a normal.
  /// @param axis_id The @c Axis ID.
  static const glm::dvec3 &upAxisNormal(UpAxis axis_id);

  /// Get a unit vector which lies along the surface of the heightmap, perpendicular to @c surfaceAxisB() and
  /// upAxisNormal().
  static const glm::dvec3 &surfaceAxisA(UpAxis axis_id);

  /// Get a unit vector which lies along the surface of the heightmap, perpendicular to @c surfaceAxisA() and
  /// upAxisNormal().
  static const glm::dvec3 &surfaceAxisB(UpAxis axis_id);

  /// Seed and enable the local cache (see class documentation).
  /// @param reference_pos The position around which to seed the local cache.
  void seedLocalCache(const glm::dvec3 &reference_pos);

  /// Generate the heightmap around a reference position. This sets the @c base_height as in the overload, but also
  /// changes the behaviour to flood fill out from the reference position.
  ///
  /// @param reference_pos The staring position to build a heightmap around. Nominally a vehicle or sensor position.
  /// @param cull_to Build the heightmap only from within these extents in the source map.
  /// @return true on success.
  bool buildHeightmap(const glm::dvec3 &reference_pos, const ohm::Aabb &cull_to = ohm::Aabb(0.0));

  /// Query the information about a voxel in the @c heightmap() occupancy map.
  ///
  /// Heightmap voxel values, positions and semantics are specialised from the general @c OccupancyMap usage. This
  /// method may be used to ensure the correct position values are retrieved and consistent voxel interpretations
  /// are made. All position queries should be made via this function. The return value is used indicate whether
  /// the voxel is relevant/occupied within the occupancy map.
  ///
  /// @param key The key of the voxel to test for validity and to retrieve the position and details of. If this key
  ///            does not map to a valid voxel in the @p heightmap() of this object a @c HeightmapVoxel::Unknown
  ///            value will be returned.
  /// @param[out] pos The retrieved position of @p heightmap_voxel . Only valid when this function returns something
  ///             other than @c HeightmapVoxel::Unknown .
  /// @param[out] voxel_info Clearance and height details of the voxel associated with @p key. Only valid when this
  ///             function returns something other than @c HeightmapVoxel::Unknown .
  /// @return The type of the voxel in question. May return @c HeightmapVoxel::Unknown if @p key is invalid.
  HeightmapVoxelType getHeightmapVoxelInfo(const Key &key, glm::dvec3 *pos, HeightmapVoxel *voxel_info = nullptr) const;

  //-------------------------------------------------------
  // Internal
  //-------------------------------------------------------

  /// Internal heightmap detail access.
  /// @return Internal heightmap details.
  inline HeightmapDetail *detail() { return imp_.get(); }
  /// Internal heightmap detail access.
  /// @return Internal heightmap details.
  inline const HeightmapDetail *detail() const { return imp_.get(); }

  /// Update @c info to reflect the details of how the heightmap is generated. See class comments.
  /// @param info The info object to update.
  void updateMapInfo(MapInfo &info) const;

  /// Ensure that @p key is referencing a voxel within the heightmap plane.
  /// @param[in,out] key The key to project. May be modified by this call. Must not be null.
  /// @return A reference to @p key.
  Key &project(Key *key) const;

private:
  /// Internal implementation of heightmap construction. Supports the different key walking techniques available.
  /// @param walker The key walker used to iterate the source map and heightmap overlap.
  /// @param reference_pos Reference position around which to generate the heightmap
  /// @param on_visit Optional callback invoked for each key visited. Parameters are: @p walker, this object's
  ///   internal details, the candidate key first evaluated for the column search start, the ground key to be migrated
  ///   to the heightmap. Both keys reference the source map.
  template <typename KeyWalker>
  bool buildHeightmapT(KeyWalker &walker, const glm::dvec3 &reference_pos,
                       void (*on_visit)(KeyWalker &, const HeightmapDetail &, const Key &, const Key &) = nullptr);

  std::unique_ptr<HeightmapDetail> imp_;
};
}  // namespace ohm

#endif  // HEIGHTMAP_H
