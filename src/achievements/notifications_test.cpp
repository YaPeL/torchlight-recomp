// Notification queue order and the per-mode policy; no runtime, guest or Steam.
#include "achievements/notifications.h"
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>
namespace pc=torchlight::achievements;
namespace {
int failures=0;
void Check(bool value,const char* message) { if(!value) { ++failures; std::cerr<<message<<'\n'; } }
}
int main() {
  pc::NotificationQueue queue;
  Check(!queue.Pop(),"empty queue has nothing to announce");
  queue.Push("A"); queue.Push("B");
  Check(queue.Size()==2,"both pushes kept");
  Check(queue.Pop()==std::optional<std::string>("A") && queue.Pop()==std::optional<std::string>("B") && !queue.Pop(),
        "first in, first out, each once");
  queue.Push("C"); queue.Clear();
  Check(!queue.Pop(),"clear drops pending notifications");
  // Producers on guest threads, one consumer: nothing lost or duplicated.
  std::vector<std::thread> producers;
  for (int t=0;t<4;++t) producers.emplace_back([&queue]{ for (int i=0;i<1000;++i) queue.Push("X"); });
  size_t popped=0;
  for (auto& producer:producers) producer.join();
  while (queue.Pop()) ++popped;
  Check(popped==4000,"concurrent pushes all delivered once");

  Check(pc::NotificationsEnabled("local",false),"local always notifies");
  Check(pc::NotificationsEnabled("steam-dry-run",false),"dry-run always notifies (Steam shows nothing)");
  Check(!pc::NotificationsEnabled("steam",false),"real Steam backend leaves it to the overlay by default");
  Check(pc::NotificationsEnabled("steam",true),"option turns ours on with real Steam");
  Check(!pc::NotificationsEnabled("off",true) && !pc::NotificationsEnabled("bogus",true),"no native mode, no notification");
  return failures?EXIT_FAILURE:EXIT_SUCCESS;
}
