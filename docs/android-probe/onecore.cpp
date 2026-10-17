// One-core CPU comparison (M2 vs the phones): three small kernels close to what the recompiled
// game spends its time on (integer/branchy code, dependent memory loads, scalar floating point).
// Each kernel runs a fixed amount of work; the best of 5 runs is reported in ms. Not a standard
// benchmark; only the ratios between machines matter.
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using Clock = std::chrono::steady_clock;

static uint64_t sink;

// Integer and branches: a byte-wise hash and a small state machine over a 4 MiB buffer.
static void IntegerKernel() {
  std::vector<uint8_t> buf(4u << 20);
  uint32_t x = 12345;
  for (auto& b : buf) { x = x * 1664525u + 1013904223u; b = uint8_t(x >> 24); }
  uint64_t h = 1469598103934665603ull;
  int state = 0;
  for (int pass = 0; pass < 40; ++pass) {
    for (uint8_t b : buf) {
      h = (h ^ b) * 1099511628211ull;
      switch ((b + state) & 3) {
        case 0: state += 1; break;
        case 1: state ^= b; break;
        case 2: state -= int(h & 7); break;
        default: state = int(h >> 61); break;
      }
    }
  }
  sink += h + uint64_t(state);
}

// Dependent loads: a random cycle through 16 MiB (cache and memory latency, like walking trees).
static void PointerChaseKernel() {
  const size_t n = (16u << 20) / sizeof(uint32_t);
  std::vector<uint32_t> next(n);
  for (size_t i = 0; i < n; ++i) next[i] = uint32_t(i);
  uint64_t r = 88172645463325252ull;
  for (size_t i = n - 1; i > 0; --i) {  // Sattolo: one cycle through every element
    r ^= r << 13; r ^= r >> 7; r ^= r << 17;
    size_t j = size_t(r % i);
    std::swap(next[i], next[j]);
  }
  uint32_t p = 0;
  for (size_t i = 0; i < 20'000'000; ++i) p = next[p];
  sink += p;
}

// Scalar floating point: a small n-body step, single precision like the guest's math.
static void FloatKernel() {
  const int n = 64;
  float px[n], py[n], pz[n], vx[n] = {}, vy[n] = {}, vz[n] = {};
  for (int i = 0; i < n; ++i) { px[i] = float(i); py[i] = float(i * 7 % 13); pz[i] = float(i % 5); }
  for (int step = 0; step < 40000; ++step) {
    for (int i = 0; i < n; ++i) {
      float ax = 0, ay = 0, az = 0;
      for (int j = 0; j < n; ++j) {
        float dx = px[j] - px[i], dy = py[j] - py[i], dz = pz[j] - pz[i];
        float inv = 1.0f / std::sqrt(dx * dx + dy * dy + dz * dz + 0.01f);
        float s = inv * inv * inv;
        ax += dx * s; ay += dy * s; az += dz * s;
      }
      vx[i] += ax * 0.001f; vy[i] += ay * 0.001f; vz[i] += az * 0.001f;
    }
    for (int i = 0; i < n; ++i) { px[i] += vx[i]; py[i] += vy[i]; pz[i] += vz[i]; }
  }
  float t = 0;
  for (int i = 0; i < n; ++i) t += px[i] + py[i] + pz[i];
  uint32_t bits;
  std::memcpy(&bits, &t, 4);
  sink += bits;
}

static double BestMs(void (*kernel)()) {
  double best = 1e30;
  for (int run = 0; run < 5; ++run) {
    auto t0 = Clock::now();
    kernel();
    double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    if (ms < best) best = ms;
  }
  return best;
}

int main() {
  double a = BestMs(IntegerKernel), b = BestMs(PointerChaseKernel), c = BestMs(FloatKernel);
  std::printf("integer %.1f ms\npointer_chase %.1f ms\nfloat %.1f ms\n(sink %llu)\n", a, b, c,
              (unsigned long long)(sink & 0xff));
  return 0;
}
