#include "achievements/diagnostics.h"
#include <cmath>
#include <iomanip>
#include <sstream>
namespace torchlight::achievements {
namespace {
template<class T> void Optional(std::ostream& out,const std::optional<T>& value) {
  if (value) out<<*value; else out<<"null";
}
void Unlocks(std::ostream& out,const std::set<std::string>& values) {
  out<<'['; bool first=true;
  for (const auto& id:values) { if (!first) out<<','; first=false; out<<std::quoted(id); }
  out<<']';
}
}
std::string Describe(const Observation& o,const State& before,const State& after) {
  std::ostringstream out;
  out<<"{\"source\":"<<std::quoted(std::string(o.source))<<",\"caller\":\"0x"<<std::hex<<o.caller
     <<"\",\"actor\":\"0x"<<o.actor<<"\""<<std::dec<<",\"event\":"<<o.event<<",\"value\":"<<o.value
     <<",\"id\":"<<std::quoted(std::string(o.id))<<",\"eligible\":";
  Optional(out,o.eligible);
  out<<",\"character\":"<<std::quoted(o.inputs.identity)<<",\"difficulty\":"; Optional(out,o.inputs.difficulty);
  out<<",\"hardcore\":"; Optional(out,o.inputs.hardcore);
  out<<",\"played_seconds\":";
  if (o.inputs.played_seconds && std::isfinite(*o.inputs.played_seconds)) out<<std::setprecision(12)<<*o.inputs.played_seconds;
  else out<<"null";
  out<<",\"deaths\":"; Optional(out,o.inputs.deaths);
  out<<",\"gold\":"; Optional(out,o.inputs.gold);
  out<<",\"stats\":["; bool first=true;
  for (size_t i=0;i<before.stats.size();++i) if (before.stats[i]!=after.stats[i]) {
    if (!first) out<<','; first=false;
    out<<"{\"key\":"<<std::quoted(std::string(kStatKeys[i]))<<",\"before\":"<<before.stats[i]
       <<",\"after\":"<<after.stats[i]<<",\"pending\":"<<after.pending_deltas[i]<<",\"evaluation\":[";
    bool first_eval=true;
    for (const auto& d:kCatalog) if (d.stat==i) {
      if (!first_eval) out<<','; first_eval=false;
      out<<"{\"id\":"<<std::quoted(std::string(d.id))<<",\"threshold\":"<<d.threshold
         <<",\"met\":"<<(after.stats[i]>=d.threshold?1:0)<<'}';
    }
    out<<"]}";
  }
  out<<"],\"new_unlocks\":["; first=true;
  for (const auto& id:after.unlocked) if (!before.unlocked.contains(id)) {
    if (!first) out<<','; first=false; out<<std::quoted(id);
  }
  out<<"],\"duplicate\":"<<(!o.id.empty() && before.unlocked.contains(std::string(o.id)) && before==after?1:0)
     <<",\"changed\":"<<(before==after?0:1)<<'}';
  return out.str();
}
std::string DescribeState(std::string_view operation,const State& s) {
  std::ostringstream out;
  out<<"{\"operation\":"<<std::quoted(std::string(operation))<<",\"profile\":"<<std::quoted(s.profile)<<",\"stats\":[";
  for (size_t i=0;i<s.stats.size();++i) { if(i) out<<','; out<<s.stats[i]; }
  out<<"],\"pending\":[";
  for (size_t i=0;i<s.pending_deltas.size();++i) { if(i) out<<','; out<<s.pending_deltas[i]; }
  out<<"],\"unlocked\":"; Unlocks(out,s.unlocked); out<<'}';
  return out.str();
}
}
