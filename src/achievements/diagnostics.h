#pragma once
#include "achievements/service.h"
namespace torchlight::achievements {
// Development observation only. None of these fields participates in rule evaluation.
struct Observation {
  std::string_view source;
  uint32_t caller=0, actor=0;
  int64_t event=-1, value=0;
  std::string_view id;
  std::optional<bool> eligible;
  Character inputs;
};
std::string Describe(const Observation&,const State& before,const State& after);
std::string DescribeState(std::string_view operation,const State&);
}
