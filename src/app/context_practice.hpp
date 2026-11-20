#pragma once

#include "train/context_practice.hpp"

#include <QString>

namespace vcube {

struct AlgSet;

// What Learn's in-context practice (see ContextPractice) says.

// The keys, while solving and once done.
QString contextStatusText(const ContextPractice &practice, bool done);
// After the pairs (`done`): how each went beside the set's algs; and in
// either case the set's cheapest order. `muted` is the colour of the
// notes.
QString contextResultHtml(const ContextPractice &practice, const AlgSet &set,
                          bool done, const QString &muted);

} // namespace vcube
