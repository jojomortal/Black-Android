#pragma once
#include <cstdint>

#include "gpu/texture.h"

// HD texture pack: the game's textures are matched by content to the files of "BLACK HD Reimagined" through a map
// (textures/black-hd-reimagined.map, see textures/README.md)
namespace hd {

// loads the map named by BLACK_HD_MAP, once; false when there is none
bool Enabled();

// the vulkan thread saw a guest texture about to be uploaded: key it, count hits and misses (guest = its level 0)
void Note(const gpu::TextureDesc& d, const gpu::FormatInfo& fi, const uint8_t* guest);

}  // namespace hd
