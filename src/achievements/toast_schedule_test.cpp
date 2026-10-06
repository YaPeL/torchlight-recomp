// Toast timing: show, hold, hide, gap, queue order and loading-screen interruption.
#include "achievements/toast_schedule.h"
#include <cstdlib>
#include <deque>
#include <iostream>
namespace pc=torchlight::achievements;
using Action=pc::ToastSchedule::Action;
namespace {
int failures=0;
void Check(bool value,const char* message) { if(!value) { ++failures; std::cerr<<message<<'\n'; } }
}
int main() {
  std::deque<std::string> pending{"A","B"};
  int asked=0;
  auto next=[&]()->std::optional<std::string> {
    ++asked;
    if (pending.empty()) return std::nullopt;
    auto id=pending.front(); pending.pop_front(); return id;
  };
  pc::ToastSchedule schedule(5.0,0.5);
  auto step=schedule.Update(0,true,next);
  Check(step.action==Action::kNone && asked==0,"covered UI takes nothing from the queue");
  step=schedule.Update(1,false,next);
  Check(step.action==Action::kShow && step.id=="A" && schedule.Showing(),"first unlock shows");
  Check(schedule.Update(5.9,false,next).action==Action::kNone,"stays up for its time");
  step=schedule.Update(6,false,next);
  Check(step.action==Action::kHide && step.id=="A" && !schedule.Showing(),"hides after five seconds");
  Check(schedule.Update(6.2,false,next).action==Action::kNone && asked==1,"gap before the next one");
  step=schedule.Update(6.5,false,next);
  Check(step.action==Action::kShow && step.id=="B","queued unlock follows in order");
  step=schedule.Update(8,true,next);
  Check(step.action==Action::kHide && step.id=="B","loading screen hides the toast");
  Check(schedule.Update(20,true,next).action==Action::kNone,"nothing while covered");
  step=schedule.Update(21,false,next);
  Check(step.action==Action::kShow && step.id=="B" && asked==2,"interrupted toast shows again without a new pop");
  Check(schedule.Update(25.9,false,next).action==Action::kNone,"shown again for the full time");
  Check(schedule.Update(26,false,next).action==Action::kHide,"then hides");
  Check(schedule.Update(27,false,next).action==Action::kNone && asked==3,"empty queue: nothing to show");
  return failures?EXIT_FAILURE:EXIT_SUCCESS;
}
