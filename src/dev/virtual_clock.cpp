#include "dev/virtual_clock.h"

namespace torchlight::dev {

uint64_t VirtualClock::Read(Reader& reader) const {
  uint64_t value = FrameTicks();
  if (reader.any && value <= reader.last) value = reader.last + 1;
  reader.last = value;
  reader.any = true;
  return value;
}

}  // namespace torchlight::dev
