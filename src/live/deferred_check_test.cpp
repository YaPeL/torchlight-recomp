// Tests for DeferredCheck: the check runs once, on the first Result, and not before.

#include <cstdio>
#include <cstdlib>

#include "live/deferred_check.h"

namespace {

using torchlight::live::DeferredCheck;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
  }
}

void TestOnce(bool answer) {
  int runs = 0;
  DeferredCheck check([&] {
    ++runs;
    return answer;
  });
  Check(!check.Done() && runs == 0, "nothing runs until asked");
  Check(check.Result() == answer && runs == 1, "the first Result runs the check");
  Check(check.Done(), "done after it ran");
  Check(check.Result() == answer && check.Result() == answer && runs == 1,
        "later Results reuse the answer");
}

}  // namespace

int main() {
  TestOnce(true);
  TestOnce(false);
  std::printf("deferred_check_test: ok\n");
  return 0;
}
