//
// author Kazys Stepanas
//
#include <glm/glm.hpp>

#include <ohm/DefaultLayer.h>
#include <ohm/MapInfo.h>
#include <ohm/MapLayer.h>
#include <ohm/MapLayout.h>
#include <ohm/MapSerialise.h>
#include <ohm/OccupancyMap.h>
#include <ohm/OccupancyUtil.h>
#include <ohm/VoxelData.h>
#include <ohm/VoxelLayout.h>

#include <ohmutil/OhmUtil.h>
#include <ohmutil/Options.h>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <sstream>
#include <unordered_set>

namespace
{
int g_quit = 0;

void onSignal(int arg)
{
  if (arg == SIGINT || arg == SIGTERM)
  {
    ++g_quit;
  }
}

struct Options
{
  std::string map_file;
  bool calculate_extents = false;
  bool detail = false;
};
}  // namespace


int parseOptions(Options *opt, int argc, char *argv[])  // NOLINT(modernize-avoid-c-arrays)
{
  cxxopts::Options opt_parse(argv[0], "\nProvide information about the contents of an occupancy map file.\n");
  opt_parse.positional_help("<map.ohm>");

  try
  {
    opt_parse.add_options()("help", "Show help.")("i,map", "The input map file (ohm) to load.",
                                                  cxxopts::value(opt->map_file))(
      "extents", "Report map extents? Requires region traversal",
      optVal(opt->calculate_extents)->implicit_value("true"))(
      "detail", "Traverse voxels for detailed information? min occupancy, max occupancy, max samples (if available)",
      optVal(opt->detail)->implicit_value("true"));

    opt_parse.parse_positional({ "map" });

    cxxopts::ParseResult parsed = opt_parse.parse(argc, argv);

    if (parsed.count("help") || parsed.arguments().empty())
    {
      // show usage.
      std::cout << opt_parse.help({ "", "Group" }) << std::endl;
      return 1;
    }

    if (opt->map_file.empty())
    {
      std::cerr << "Missing input map" << std::endl;
      return -1;
    }
  }
  catch (const cxxopts::OptionException &e)
  {
    std::cerr << "Argument error\n" << e.what() << std::endl;
    return -1;
  }

  return 0;
}

void showMapInfo(const ohm::MapInfo &info)
{
  unsigned item_count = info.extract(nullptr, 0);

  std::cout << "Meta data items: " << item_count << std::endl;
  if (item_count)
  {
    std::vector<ohm::MapValue> items;
    items.resize(item_count);
    item_count = info.extract(items.data(), item_count);
    std::sort(items.begin(), items.end(), [](const ohm::MapValue &a, const ohm::MapValue &b) {
      return std::less<std::string>{}(a.name(), b.name());
    });

    ohm::MapValue str_value;
    for (unsigned i = 0; i < item_count; ++i)
    {
      str_value = items[i].toStringValue();
      std::cout << "  " << str_value.name() << " : " << static_cast<const char *>(str_value) << std::endl;
    }
  }

  std::cout << std::endl;
}

int main(int argc, char *argv[])
{
  Options opt;

  std::cout.imbue(std::locale(""));

  int res = 0;
  res = parseOptions(&opt, argc, argv);

  if (res)
  {
    return res;
  }

  signal(SIGINT, onSignal);
  signal(SIGTERM, onSignal);

  ohm::OccupancyMap map(1.0f);
  ohm::MapVersion version;

  size_t region_count = 0;
  res = ohm::loadHeader(opt.map_file.c_str(), map, &version, &region_count);

  if (res != 0)
  {
    std::cerr << "Failed to load map. Error(" << res << "): " << ohm::serialiseErrorCodeString(res) << std::endl;
    return res;
  }

  std::cout << "File format version: " << version.major << '.' << version.minor << '.' << version.patch << std::endl;
  std::cout << std::endl;

  std::cout << "Estimated memory (CPU only): " << ohm::util::Bytes(map.calculateApproximateMemory()) << std::endl;

  std::cout << "Voxel resolution: " << map.resolution() << std::endl;
  std::cout << "Map origin: " << map.origin() << std::endl;
  std::cout << "Region spatial dimensions: " << map.regionSpatialResolution() << std::endl;
  std::cout << "Region voxel dimensions: " << map.regionVoxelDimensions() << " : " << map.regionVoxelVolume()
            << std::endl;
  std::cout << "Region count: " << region_count << std::endl;
  std::cout << std::endl;

  std::cout << "Occupancy threshold: " << map.occupancyThresholdProbability() << " (" << map.occupancyThresholdValue()
            << ")" << std::endl;
  std::cout << "Hit probability: " << map.hitProbability() << " (" << map.hitValue() << ")" << std::endl;
  std::cout << "Miss probability: " << map.missProbability() << " (" << map.missValue() << ")" << std::endl;
  std::cout << "Probability min/max: [" << map.minVoxelProbability() << "," << map.maxVoxelProbability() << "]"
            << std::endl;
  std::cout << "Value min/max: [" << map.minVoxelValue() << "," << map.maxVoxelValue() << "]" << std::endl;
  std::cout << "Saturation min/max: [" << (map.saturateAtMinValue() ? "on" : "off") << ","
            << (map.saturateAtMaxValue() ? "on" : "off") << "]" << std::endl;
  ;

  std::cout << "Touched stamp: " << map.stamp() << std::endl;
  std::cout << "Flags: " << std::endl;
  if (map.flags() != ohm::MapFlag::kNone)
  {
    unsigned bit = 1;
    for (unsigned i = 0; i < sizeof(ohm::MapFlag) * 8; ++i, bit <<= 1u)
    {
      if (unsigned(map.flags()) & bit)
      {
        std::cout << "  " << mapFlagToString(ohm::MapFlag(bit)) << std::endl;
      }
    }
  }
  else
  {
    std::cout << "  None" << std::endl;
  }

  std::cout << std::endl;

  // Meta info.
  showMapInfo(map.mapInfo());

  // Data needing chunks to be partly loaded.
  // - Extents
  // - Region count
  // - Memory footprint

  const ohm::MapLayout &layout = map.layout();
  std::string indent;
  std::string vox_size_str;
  std::string region_size_str;
  std::cout << "Layers: " << layout.layerCount() << std::endl;

  for (size_t i = 0; i < layout.layerCount(); ++i)
  {
    const ohm::MapLayer &layer = layout.layer(i);
    ohm::VoxelLayoutConst voxels = layer.voxelLayout();
    indent = "  ";
    std::cout << indent << layer.name() << std::endl;
    indent += "  ";
    std::cout << indent << "serialised? " << ((layer.flags() & ohm::MapLayer::kSkipSerialise) == 0 ? "true" : "false")
              << std::endl;
    std::cout << indent << "subsampling: " << layer.subsampling() << std::endl;
    std::cout << indent << "voxels: " << layer.dimensions(map.regionVoxelDimensions()) << " : "
              << layer.volume(layer.dimensions(map.regionVoxelDimensions())) << std::endl;

    std::cout << indent << "voxel byte size: " << ohm::util::Bytes(voxels.voxelByteSize()) << std::endl;
    std::cout << indent << "region byte size: "
              << ohm::util::Bytes(voxels.voxelByteSize() * layer.volume(layer.dimensions(map.regionVoxelDimensions())))
              << std::endl;

    indent += "  ";
    std::cout << std::setw(4) << std::setfill('0');
    for (size_t i = 0; i < voxels.memberCount(); ++i)
    {
      std::cout << indent << "0x" << std::hex << voxels.memberOffset(i) << " "
                << ohm::DataType::name(voxels.memberType(i)) << " " << voxels.memberName(i) << " (0x"
                << voxels.memberSize(i) << ")" << std::endl;
    }
    std::cout << std::setw(0) << std::dec;
  }

  // Load full map if required
  if (opt.calculate_extents || opt.detail)
  {
    // Reload the map for full extents.
    res = ohm::load(opt.map_file.c_str(), map);
    if (res)
    {
      std::cerr << "Failed to load map regions. Error code: " << res << std::endl;
      return res;
    }
  }

  if (opt.calculate_extents)
  {
    glm::dvec3 min_ext(0.0);
    glm::dvec3 max_ext(0.0);
    ohm::Key min_key(ohm::Key::kNull);
    ohm::Key max_key(ohm::Key::kNull);
    map.calculateExtents(&min_ext, &max_ext, &min_key, &max_key);

    std::cout << std::endl;
    std::cout << "Spatial Extents: " << min_ext << " - " << max_ext << std::endl;
    std::cout << "Key Extents: " << min_key << " - " << max_key << std::endl;
  }

  if (opt.detail)
  {
    float min_occupancy = std::numeric_limits<float>::max();
    float max_occupancy = -std::numeric_limits<float>::max();
    uint64_t free_voxels = 0;
    uint64_t occupied_voxels = 0;
    uint64_t total_point_count = 0;
    unsigned max_point_count = 0;

    const int mean_layer = map.layout().meanLayer();

    ohm::Voxel<const float> voxel(&map, map.layout().occupancyLayer());
    ohm::Voxel<const ohm::VoxelMean> mean(&map, map.layout().meanLayer());
    if (voxel.isLayerValid())
    {
      for (auto iter = map.begin(); iter != map.end() && !g_quit; ++iter)
      {
        ohm::setVoxelKey(iter, voxel, mean);
        float value;
        voxel.read(&value);
        if (value != ohm::unobservedOccupancyValue())
        {
          min_occupancy = std::min(value, min_occupancy);
          max_occupancy = std::max(value, max_occupancy);

          free_voxels += (value < map.occupancyThresholdValue());
          occupied_voxels += (value >= map.occupancyThresholdValue());

          if (mean.isLayerValid() && value >= map.occupancyThresholdValue())
          {
            ohm::VoxelMean mean_info;
            mean.read(&mean_info);
            max_point_count = std::max<unsigned>(mean_info.count, max_point_count);
            total_point_count += mean_info.count;
          }
        }
      }

      std::cout << "Probability max: " << ohm::valueToProbability(max_occupancy) << " (" << max_occupancy << ")"
                << std::endl;
      std::cout << "Probability min: " << ohm::valueToProbability(min_occupancy) << " (" << min_occupancy << ")"
                << std::endl;
      std::cout << "Free voxels: " << free_voxels << std::endl;
      std::cout << "Occupied voxels: " << occupied_voxels << std::endl;

      if (mean_layer >= 0)
      {
        std::cout << "Max voxel samples: " << max_point_count << std::endl;
        std::cout << "Average voxel samples: " << total_point_count / occupied_voxels << std::endl;
      }
    }
    else
    {
      std::cout << "No " << ohm::default_layer::occupancyLayerName() << " layer" << std::endl;
    }
  }

  return res;
}
