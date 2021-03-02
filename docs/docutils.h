// Copyright (c) 2021
// Commonwealth Scientific and Industrial Research Organisation (CSIRO)
// ABN 41 687 119 230
//
// Author: Kazys Stepanas
#ifndef DOCUTILS_H
#define DOCUTILS_H

namespace ohm
{
/*!

@page docutils Ohm utilities

Ohm includes a number of command line utilities supporting generating and manipulating ohm map files. This page provides
high level information about the available utilities, their primary purpose and general usage.

## ohmpop[cpu,cuda,ocl]

The `ohmpop` - "ohm populate" - utilties are used to generate ohm map files from a point cloud and corresponding
trajectory file. There are three versions of this utility which mostly share the same command line options. The GPU
enabled versions support some additional options to control GPU related settings.

- `ohmpopcpu` the CPU implementation of ohm map generation
- `ohmpopcuda` a CUDA enabled version of the algorithm. Only present when building with CUDA libraries.
- `ohmpopocl` an OpenCL enabled version of the algorithm. Only present when building with OpenCL libraries.

The input point cloud is expected to be the results of a SLAM scan after global optimisation and loop closure and
requires each point to be correctly timestamped in order to correlate the point agains the trajectory file. The
trajectory file is used to identify the scanner position corresponding to each sample.

Support inputs are as follows:

- Point cloud:
  - Supports LAS/LAZ when ohm is built against `libLAS` and `lasZIP`.
  - Supports many point cloud formats when build against PDAL. Precise formats depend on the PDAL support.
- Trajectory file
  - Supports a text file format (see below).
  - Supports the same point cloud formats as the input cloud.

The text file trajectory format supports an optional "headings" line as the first line of the file after which each line
must be of the following form:

| Field       | Description                                                                 |
| ----------- | --------------------------------------------------------------------------- |
| Timestamp   | Floating point timestamp value in the same time base as the point cloud     |
| X           | X coordinate for the sensor position, in the same frame as the point cloud  |
| Y           | Y coordinate for the sensor position, in the same frame as the point cloud  |
| Z           | X coordinate for the sensor position, in the same frame as the point cloud  |
| q0          | Quaternion W component for the sensor rotation (unused)                     |
| q1          | Quaternion X component for the sensor rotation (unused)                     |
| q2          | Quaternion Y component for the sensor rotation (unused)                     |
| q3          | Quaternion Z component for the sensor rotation (unused)                     |
| user fields | Additional fields (optional, ignored)                                       |

For each sample point, `ohmpop` tries to match a corresponding trajectory interval in in the trajectory file and
interpolates the scanner pose between the nearest trajectory samples.

`ohmpop` generates a `.ohm` file for the map. By default it generates only the occupancy map layer, but command line
options may be used to generate the voxel mean layer and covariance (normal distribution transforms or ndt) layer. A ply
representation of the ohm map is also generated by default which contains a single point per occupied voxel.

## ohminfo

A basic utility for displaying the content of a `.ohm` map file. Most ohm algorithms add meta data about how the map was
generated which are included in the `ohminfo` display.

## ohm2ply

A conversion utility which extracts data from an ohm map into a PLY point cloud file. `ohm2ply` supports different
colourisation modes - e.g., colour by height - as well as different data extraction modes.

| ohm2ply mode  | Description |
| ------------- | ---------------------------------------------------------------------------------------------------- |
| occupancy | (default) Extract a single point per occupied voxel prefering voxel mean over voxel centre if available  |
| occupancy-centre | Same as `occupancy`, but always uses the voxel centre to position points                          |
| covariance  | Uses the covariance/ndt layer to export polygonal ellipsoids from an ndt based map                     |
| clearance   | Extract the `clearance` layer colouring points by proximity to an occupied voxel (experimental)        |
| heightmap   | Extract points from a heightmap map file (see `ohmheightmap`). Uses heights from the heightmap layer   |
| heightmap-mesh | Meshes a single layer heightmap into a polygonal PLY file (experimental)                            |

## ohmheightmap

Generates a heightmap from an ohm map file. The output is another ohm map file with a heightmap data layer. Supports a
number of heightmap generation methods including generating a multi-layer heightmap.

| ohmheightmap mode | Description |
| ----------------- | ------------------------------------------------------------------------------------------------ |
| planar    | Simple planar walk, searching for a ground voxel within a band across the map. Generates a single layer. |
| fill              | A flood fill based extension of the planar technique. Generates a single layer.                  |
| layered           | Generates a multi-layer heightmap. Columns are sorted in ascending height order.                 |
| layered-unordered | Generates a multi-layer heightmap where column. Columns are unsorted.                            |

## ohmfilter

Filters an input point cloud against an ohm map rejecting points which do not fall within occupied voxels in the
occupancy map.

## ohmprob

A simple utility which converts a floating point value between an occupancy probability and an occupancy value (log
space). This uses the same algorithm used in ohm map occupancy calculations.

## ohmhm2img (experimental)

Converts an ohm heightmap file into a png image. This is an experimental utility and does not support layered
heightmaps.

## ohmquery[cuda,ocl] (experimental)

Supports running experimental ohm query algorithms - @c OhmLineQuery, @c OhmNearestNeighbours.

*/
} // namespace ohm

#endif // DOCUTILS_H
