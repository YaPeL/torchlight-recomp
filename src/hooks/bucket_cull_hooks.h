// Bucket culling hooks (bucket_cull_hooks.cpp; bucket_cull.h has the design).

#pragma once

#include <cstdint>
#include <string_view>

namespace torchlight::hooks {

// After the guest image is loaded: on in native mode unless --native_bucket_cull=false.
void InstallBucketCull(bool native_only);
// Every program the guest's RTSS creates (resource_hooks.cpp): a vertex program that does not
// place vertices as the fixed pipeline does turns the culling off for the rest of the session.
void BucketCullNoteProgram(std::string_view target, std::string_view source);
// Whether `renderable`, offered to the render queue now, is a geometry bucket the main scene walk
// drops (entirely outside the camera's frustum).
bool BucketCulledInMainWalk(const uint8_t* base, uint32_t renderable);

struct BucketCullStats {
  uint64_t main_walks = 0, buckets_tested = 0, buckets_dropped = 0, buckets_unreadable = 0;
};

}  // namespace torchlight::hooks
