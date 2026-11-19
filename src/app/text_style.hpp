#pragma once

#include <QFont>
#include <QString>

class QWidget;

namespace vcube {

// The type scale: every size of text in the app is one of these roles,
// worked out from the application font (which the interface text size
// setting scales), so that the pages keep their proportions at any size.
// The sizes set in Settings (scramble, timer, hints) are separate.
enum class TextRole {
  Caption, // help lines under controls, sidebar headings
  Body,    // the application font
  Heading, // a stage's header, the algs revealed, a grade
  Title,   // the line a stage is about: the prompt, the alg being done
  Display, // a page's title
};

// Gives `w` the size of `role` (and bold), now and whenever the interface
// text size changes.
void setTextRole(QWidget *w, TextRole role, bool bold = false);

// Scales the application font to `percent` of the system's and brings
// every widget given a role up to date. The first call takes the system
// font as it is then.
void setInterfaceTextPercent(int percent);

// "13 px": the size of body text at `percent`, for the setting's choices.
int interfaceTextPixels(int percent);

// `f` with tabular (equal width) figures, for numbers that change in
// place; as it is where the font has no such feature or Qt is older than
// 6.7.
QFont withTabularFigures(QFont f);
// Gives `w` tabular figures, keeping the rest of its font inherited (so it
// still follows the interface text size).
void useTabularFigures(QWidget *w);

// The two sides of a case diagram, in pixels at the system's text size:
// small beside a list or a table, large where the case is the subject.
inline constexpr int kSmallDiagram = 120;
inline constexpr int kLargeDiagram = 160;

// `pixels` scaled by the interface text size the app started with (a
// change takes effect on the next start, as sizes fixed at construction
// would otherwise disagree with pictures drawn later).
int scaledToText(int pixels);

} // namespace vcube
