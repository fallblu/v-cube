#pragma once

// Time limits for the tests that check how long something takes.

#include <QtGlobal>

#include <chrono>

namespace vcube::testing {

// `limit` times VCUBE_TEST_TIME_SCALE, a whole number (1 when it isn't
// set): slower builds, sanitized ones say, set it higher.
inline std::chrono::milliseconds timeLimit(std::chrono::milliseconds limit) {
  bool ok = false;
  int scale = qEnvironmentVariableIntValue("VCUBE_TEST_TIME_SCALE", &ok);
  return ok && scale > 1 ? limit * scale : limit;
}

} // namespace vcube::testing
