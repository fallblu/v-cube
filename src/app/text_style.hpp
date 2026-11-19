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

} // namespace vcube
