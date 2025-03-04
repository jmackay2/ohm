// Copyright (c) 2017
// Commonwealth Scientific and Industrial Research Organisation (CSIRO)
// ABN 41 687 119 230
//
// Author: Kazys Stepanas

//------------------------------------------------------------------------------
// Note on building this code.
// This code may be used by multiple OpenCL kernels, but has to support the same
// for CUDA compilation. This is slightly tricky because OpenCL has entiredly
// separate compilation units, while CUDA does not. To work around this, we
// allow the code to be included/compiled multiple times with a different set
// of controlling defines. These are documented below:
// - WALK_LINE_VOXELS : the main function name
// - VISIT_LINE_VOXEL : the function to call when visiting a voxel. Must have
// the signature given below.
//
/// This function is called for each voxel traversed by the line walking function.
///
/// This function is not implemented in this source file, and must be implemented by the source including this file.
///
/// @param voxelKey The key for the voxel currently being traversed. This voxel is on the line.
/// @param isEndVoxel True if @p voxelKey is the last voxel on the line.
/// @param voxelResolution The edge length of each voxel cube.
/// @param userData User data pointer.
/// @return True to continue traversing the line, false to abort traversal.
// __device__ bool VISIT_LINE_VOXEL(const GpuKey *voxelKey, bool isEndVoxel,
//                                  const GpuKey *startKey, const GpuKey *endKey,
//                                  float voxelResolution, float entryRange, float exitRange, void *userData);
//------------------------------------------------------------------------------
#include "GpuKey.h"

#ifndef WALK_LINE_VOXELS
#error WALK_LINE_VOXELS must be used to define the voxel walking function.
#endif  // WALK_LINE_VOXELS

#ifndef VISIT_LINE_VOXEL
#error VISIT_LINE_VOXEL must be used to define the voxel visiting function
#endif  // VISIT_LINE_VOXEL

/// Calculate the @p GpuKey for @p point local to the region's minimum extents corner.
/// @param[out] key The output key.
/// @param point The coordinate to calculate the @p key for in region local coordinates.
/// @paramregionDim Defines the size of a region in voxels. Used to update the @p GpuKey.
/// @param voxelResolution Size of a voxel from one face to another.
/// @return True if @c point lies in the region, false otherwise.
__device__ bool coordToKey(GpuKey *key, const float3 *point, const int3 *regionDim, float voxelResolution);

/// Calculates the centre of the voxel defined by @p key (global space).
/// @param key The key marking the voxel of interest.
/// @paramregionDim Defines the size of a region in voxels. Used to update the @p GpuKey.
/// @param voxelResolution Size of a voxel from one face to another.
/// @return The centre of the voxel defined by @p key.
inline __device__ float3 voxelCentre(const GpuKey *key, const int3 *regionDim, float voxelResolution);

inline __device__ float getf3(const float3 *v, int index);
inline __device__ int geti3(const int3 *v, int index);

// Psuedo header guard to prevent symbol duplication.
#ifndef LINE_WALK_CL
#define LINE_WALK_CL
/// Line walking function for use by kernels.
/// The algorithm walks the voxels from @p startKey to @p endKey. The line segment is defined relative to the centre of
/// the @p startkey voxel with line points @p startPoint and @p endPoint respectively.
///
/// @c WALK_LINE_VOXELS() is invoked for each voxel traversed.
///
/// Based on J. Amanatides and A. Woo, "A fast voxel traversal algorithm for raytracing," 1987.
///
/// @param startKey The key for the voxel containing @p startPoint.
/// @param endKey The key for the voxel containing @p endPoint.
/// @param startVoxelCentre Coordinate of the centre of the starting voxel, in the same frame as @c startPoint and @c
///   endPoint.
/// @param startPoint The start point of the line segment to traverse, relative to the centre of the
///   start voxel (identified by startKey). That is the origin is the centre of the startKey voxel.
/// @param endPoint The end point of the line segment to traverse, relative to the centre of the
///   start voxel (identified by startKey). That is the origin is the centre of the startKey voxel.
/// @paramregionDim Defines the size of a region in voxels. Used to update the @p GpuKey.
/// @param voxelResolution Size of a voxel from one face to another.
/// @param userData User pointer passed to @c walkLineVoxel().
__device__ void WALK_LINE_VOXELS(const GpuKey *startKey, const GpuKey *endKey, const float3 *startVoxelCentre,
                                 const float3 *startPoint, const float3 *endPoint, const int3 *regionDim,
                                 float voxelResolution, void *userData);


inline __device__ bool coordToKey(GpuKey *key, const float3 *point, const int3 *regionDim, float voxelResolution)
{
  // Quantise.
  key->region[0] = pointToRegionCoord(point->x, regionDim->x * voxelResolution);
  key->region[1] = pointToRegionCoord(point->y, regionDim->y * voxelResolution);
  key->region[2] = pointToRegionCoord(point->z, regionDim->z * voxelResolution);

  // Localise.
  // Trying to minimise local variables for GPU.
  // The value we pass to pointToRegionVoxel is logically:
  //    point - regionCentre - regionHalfExtents
  // or
  //    point - regionMin
  // which equates to the point in region local coordinates.
  // printf("p.x(%f) = %f - (%f - %f)   : regionMin: %f\n",
  //        point->x - (regionCentreCoord(key->region[0], regionDim->x * voxelResolution) - 0.5f * regionDim->x *
  //        voxelResolution), point->x, regionCentreCoord(key->region[0], regionDim->x * voxelResolution), 0.5f *
  //        regionDim->x * voxelResolution, regionCentreCoord(key->region[0], regionDim->x * voxelResolution) - 0.5f *
  //        regionDim->x * voxelResolution);
  key->voxel[0] = pointToRegionVoxel(point->x - (regionCentreCoord(key->region[0], regionDim->x * voxelResolution) -
                                                 0.5f * regionDim->x * voxelResolution),
                                     voxelResolution, regionDim->x * voxelResolution);
  key->voxel[1] = pointToRegionVoxel(point->y - (regionCentreCoord(key->region[1], regionDim->y * voxelResolution) -
                                                 0.5f * regionDim->y * voxelResolution),
                                     voxelResolution, regionDim->y * voxelResolution);
  key->voxel[2] = pointToRegionVoxel(point->z - (regionCentreCoord(key->region[2], regionDim->z * voxelResolution) -
                                                 0.5f * regionDim->z * voxelResolution),
                                     voxelResolution, regionDim->z * voxelResolution);

  if (key->voxel[0] < regionDim->x && key->voxel[1] < regionDim->y && key->voxel[2] < regionDim->z)
  {
    return true;
  }

// Out of range.
#if 1
  printf("%u Bad key: " KEY_F "\nfrom (%.16f,%.16f,%.16f)\n"
         "  quantisation: (%.16f,%.16f,%.16f)\n"
         "  region: (%.16f,%.16f,%.16f)\n",
         (uint)get_global_id(0), KEY_A(*key), point->x, point->y, point->z,
         point->x -
           (regionCentreCoord(key->region[0], regionDim->x * voxelResolution) - 0.5f * regionDim->x * voxelResolution),
         point->y -
           (regionCentreCoord(key->region[1], regionDim->y * voxelResolution) - 0.5f * regionDim->y * voxelResolution),
         point->z -
           (regionCentreCoord(key->region[2], regionDim->z * voxelResolution) - 0.5f * regionDim->z * voxelResolution),
         regionDim->x * voxelResolution, regionDim->y * voxelResolution, regionDim->z * voxelResolution);
  printf("pointToRegionCoord(%.16f, %d * %.16f = %.16f)\n", point->y, regionDim->y, voxelResolution,
         regionDim->y * voxelResolution);
  printf("pointToRegionVoxel(%.16f - %.16f, ...)\n", point->y,
         regionCentreCoord(key->region[1], regionDim->y * voxelResolution) - 0.5f * (regionDim->y * voxelResolution));
#endif  // #
  return false;
}


inline __device__ float3 voxelCentre(const GpuKey *key, const int3 *regionDim, float voxelResolution)
{
  float3 voxel;

  // printf("voxelCentre(" KEY_F ", [%d %d %d], %f)\n", KEY_A(*key), regionDim->x, regionDim->y, regionDim->z,
  // voxelResolution);

  // Calculation is:
  //  - region centre - region half extents => region min extents.
  //  - add voxel region local coordiate.
  // Using terse code to reduce local variable load.
  voxel.x = regionCentreCoord(key->region[0], regionDim->x * voxelResolution) - 0.5f * regionDim->x * voxelResolution +
            key->voxel[0] * voxelResolution + 0.5f * voxelResolution;
  voxel.y = regionCentreCoord(key->region[1], regionDim->y * voxelResolution) - 0.5f * regionDim->y * voxelResolution +
            key->voxel[1] * voxelResolution + 0.5f * voxelResolution;
  voxel.z = regionCentreCoord(key->region[2], regionDim->z * voxelResolution) - 0.5f * regionDim->z * voxelResolution +
            key->voxel[2] * voxelResolution + 0.5f * voxelResolution;

  return voxel;
}


inline __device__ float getf3(const float3 *v, int index)
{
  return (index == 0) ? v->x : ((index == 1) ? v->y : v->z);
}


inline __device__ int geti3(const int3 *v, int index)
{
  return (index == 0) ? v->x : ((index == 1) ? v->y : v->z);
}
#endif  // LINE_WALK_CL


__device__ void WALK_LINE_VOXELS(const GpuKey *startKey, const GpuKey *endKey, const float3 *startVoxelCentre,
                                 const float3 *startPoint, const float3 *endPoint, const int3 *regionDim,
                                 float voxelResolution, void *userData)
{
  // see "A Faster Voxel Traversal Algorithm for Ray Tracing" by Amanatides & Woo
  float timeMax[3];
  float timeDelta[3];
  float timeLimit[3];
  int step[3] = { 0 };
  float timeCurrent = 0.0f;
  bool continueTraversal = true;
  float length = 0;

  // BUG: Intel OpenCL 2.0 compiler does not effect the commented assignment below. I've had to unrolled it in copyKey()
  // GpuKey currentKey = *startKey;
  GpuKey currentKey;
  copyKey(&currentKey, startKey);

  // printf("Start point : %f %f %f, " KEY_F "\n", startPoint->x, startPoint->y,
  // startPoint->z, KEY_A(*startKey)); printf("End point : %f %f %f\n", endPoint->x,
  // endPoint->y, endPoint->z); printf("currentKey: " KEY_F "\n", KEY_A(currentKey));

  // Compute step direction, increments and maximums along each axis.
  // Things to remember:
  // - start and end keys come precalcualted in the map space.
  // - float3 start/end points are single precision. To deal with this, they are likely in to be in some non-global
  //   frame. This frame is irrelevant here so long as startPoint, endPoint and startVoxelCentre are in the same frame.
  {
    // Scoped to try reduce local variable load on local memory.
    float3 direction = *endPoint - *startPoint;
    // Check for degenerate rays: start/end in the same voxel.
    if (fabs(dot(direction, direction)) > 1e-3f)
    {
      length = sqrt(dot(direction, direction));
      direction *= 1.0f / length;
    }
    else
    {
      // Denegerate ray. Set the direction to be endKey - startKey.
      direction = keyDirection(startKey, endKey);
      if (direction.x || direction.y || direction.z)
      {
        direction = normalize(direction);
        length = voxelResolution;  // Ensure a non-zero length.
      }
    }

    // const float3 voxel = voxelCentre(&currentKey, regionDim, voxelResolution);
    // printf("V: %f %f %f\n", voxel.x, voxel.y, voxel.z);
    // printf("C: " KEY_F "\n", KEY_A(currentKey));
    for (unsigned i = 0; i < 3; ++i)
    {
      if (getf3(&direction, i) != 0)
      {
        const float directionAxisInv = 1.0f / getf3(&direction, i);
        step[i] = (getf3(&direction, i) > 0) ? 1 : -1;
        // Time delta is the ray time between voxel boundaries calculated for each axis.
        timeDelta[i] = voxelResolution * fabs(directionAxisInv);
        // Calculate the distance from the origin to the nearest voxel edge for this axis.
        // const float nextVoxelBorder = getf3(&voxel, i) + step[i] * 0.5f * voxelResolution;
        const float nextVoxelBorder = getf3(startVoxelCentre, i) + step[i] * 0.5f * voxelResolution;
        timeMax[i] = (nextVoxelBorder - getf3(startPoint, i)) * directionAxisInv;
        // Set the distance limit
        // original...
        // timeLimit[i] = fabs((getf3(endPoint, i) - getf3(startPoint, i)) * directionAxisInv);
        // which is equivalent to...
        timeLimit[i] = length;
      }
      else
      {
        timeMax[i] = timeDelta[i] = FLT_MAX;
        timeLimit[i] = 0;
      }
    }
  }

  // printf("\n");
  // for (int i = 0; i < 3; ++i)
  // {
  //   printf("timeMax[%d]: %f timeLimit[%d]: %f timeDelta[%d]: %f\n",
  //          i, timeMax[i], i, timeLimit[i], i, timeDelta[i]);
  // }

  // printf("S: " KEY_F " C: " KEY_F " E: " KEY_F "\n", KEY_A(*startKey), KEY_A(currentKey), KEY_A(*endKey));

  int axis = 0;
  bool limitReached = false;
#ifdef LIMIT_LINE_WALK_ITERATIONS
  int iterations = 0;
  const int iterLimit = 2 * 32768;
#endif  // LIMIT_LINE_WALK_ITERATIONS
  while (!limitReached && !equalKeys(&currentKey, endKey) && continueTraversal)
  {
#ifdef LIMIT_LINE_WALK_ITERATIONS
    if (iterations++ > iterLimit)
    {
      printf("%u excessive line walk iterations.\n"
             "S: " KEY_F " E: " KEY_F "\n",
             "C: " KEY_F "\n" get_global_id(0), KEY_A(*startKey), KEY_A(*endKey), KEY_A(currentKey));
      break;
    }
#endif  // LIMIT_LINE_WALK_ITERATIONS
    // Select the minimum timeMax as the next axis.
    axis = (timeMax[0] < timeMax[2]) ? ((timeMax[0] < timeMax[1]) ? 0 : 1) : ((timeMax[1] < timeMax[2]) ? 1 : 2);
    const float nextTime = limitReached ? timeLimit[axis] : timeMax[axis];
    continueTraversal =
      VISIT_LINE_VOXEL(&currentKey, false, startKey, endKey, voxelResolution, timeCurrent, nextTime, userData);
    limitReached = fabs(timeMax[axis]) > timeLimit[axis];
    stepKeyAlongAxis(&currentKey, axis, step[axis], regionDim);
    timeMax[axis] += timeDelta[axis];
    timeCurrent = nextTime;
  }

  // if (limitReached)
  // {
  //   printf("%u limitReached\n", get_global_id(0));
  //   printf("timeMax[%d]: %f timeLimit[%d]: %f timeDelta[%d]: %f\n",
  //          axis, timeMax[axis], axis, timeLimit[axis], axis, timeDelta[axis]);
  // }
  // if (equalKeys(&currentKey, endKey))
  // {
  //   printf("%u currentKey == endKey\n", get_global_id(0));
  // }
  // if (!continueTraversal)
  // {
  //   printf("%u continueTraversal = false\n", get_global_id(0));
  // }

  // Walk end point.
  if (continueTraversal)
  {
    VISIT_LINE_VOXEL(endKey, endKey->voxel[3] == 0, startKey, endKey, voxelResolution, timeCurrent, length, userData);
  }
}
