// The render system and GPU a backend test draws with, from its command line: `--render_system=`
// `gl3plus` (the default) or `d3d11`, and `--gpu=<platform GPU id>` (empty: automatic; Windows'
// software adapter, WARP, is one of platform::AllGpus). CMakeLists.txt registers each test once
// per render system the platform has.

#pragma once

#include <cstdio>
#include <cstring>
#include <string>

#include "backend/backend_api.h"

namespace torchlight::backend_test {

struct RenderSystemArgs {
  tl_render_system render_system = TL_RENDER_SYSTEM_GL3PLUS;
  std::string gpu;
  const char* name = "gl3plus";
};

// False (after printing why) on an argument it does not know.
inline bool ParseRenderSystemArgs(int argc, char** argv, RenderSystemArgs& out) {
  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    if (std::strcmp(arg, "--render_system=gl3plus") == 0) {
      out.render_system = TL_RENDER_SYSTEM_GL3PLUS;
      out.name = "gl3plus";
    } else if (std::strcmp(arg, "--render_system=d3d11") == 0) {
      out.render_system = TL_RENDER_SYSTEM_D3D11;
      out.name = "d3d11";
    } else if (std::strncmp(arg, "--gpu=", 6) == 0) {
      out.gpu = arg + 6;
    } else {
      std::fprintf(stderr, "unknown argument %s (--render_system=gl3plus|d3d11, --gpu=ID)\n", arg);
      return false;
    }
  }
  return true;
}

}  // namespace torchlight::backend_test
