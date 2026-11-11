#include "app/help_text.hpp"

#include <QCoreApplication>
#include <QStringList>

#include <array>

namespace vcube {

namespace {

QString tr(const char *text) {
  return QCoreApplication::translate("vcube::HelpText", text);
}

struct Section {
  const char *title;
  const char *body;
};

// By mode, in AppMode order.
constexpr std::array<Section, 10> kModeSections = {{
    {QT_TRANSLATE_NOOP("vcube::HelpText", "Learn (virtual cube, untimed)"),
     QT_TRANSLATE_NOOP(
         "vcube::HelpText",
         "<p>Solve the case on the virtual cube from memory. <b>Tab</b> "
         "shows the algs (grades the case Again), <b>Shift+Space</b> gives "
         "up, <b>Esc</b> resets the case. After the case: <b>Space/Enter</b> "
         "next case, <b>1–4</b> change the grade (Again/Hard/Good/Easy), "
         "<b>Esc</b> resets the cube to practise.</p><p>Guided learning: a "
         "new case's walkthrough steps with <b>→</b>/<b>←</b> (<b>Home</b> "
         "back to the start) and <b>Space</b> tries it with each next move "
         "shown; in the learning steps <b>Tab</b> first shows only the next "
         "move. In context: <b>Tab</b> shows the set's cheapest order, "
         "<b>Esc</b> starts again, <b>Shift+Space</b> gives up, then "
         "<b>Space</b> for another; <b>Backspace</b> goes back to "
         "learning.</p><p><b>Ctrl+Z</b> takes back the last grade and does "
         "the case again, <b>F9</b> hides the case list. Recognise: "
         "<b>Space</b> once you recall the alg (<b>Shift+Space</b> if you "
         "can't), then <b>1–4</b> grade it. Which case?: <b>1–6</b> answer, "
         "<b>Space</b> next.</p>")},
    {QT_TRANSLATE_NOOP("vcube::HelpText", "Drill (timed)"),
     QT_TRANSLATE_NOOP(
         "vcube::HelpText",
         "<p>Virtual cube: tap <b>Space</b> to arm the timer and the first "
         "face turn starts it (or hold Space and let go, in Settings); until "
         "then only rotations turn the cube. The timer stops when the step "
         "is done (solved for PLL, oriented for OLL, ...); <b>Space</b> "
         "stops it before, which is a DNF unless the step is done. Physical "
         "cube: hold <b>Space</b> until the time turns green and let go; any "
         "key but a modifier stops. <b>Esc</b> cancels. <b>Shift+Space</b> "
         "before or during the solve gives the case up (a DNF; "
         "<b>Delete</b> takes it back if the timer hadn't started); "
         "Settings > Drill can make <b>Backspace</b> the give-up key "
         "instead.</p>"
         "<p>After the case: <b>Space</b> next case, <b>Tab</b> practise "
         "it, <b>Backspace</b> toggles DNF (virtual) or \"wrong alg\" "
         "(physical), <b>Delete</b> deletes the attempt, <b>Esc</b> tries "
         "the case again. Practice is untimed and unrecorded: first with the "
         "algs shown, then without them (<b>Tab</b> switches; on the virtual "
         "cube each stage goes on by itself once solved). <b>Space</b> goes "
         "on to the next case and <b>Esc</b> resets the cube.</p>"
         "<p>Click a column of the case list to sort by it; right-click a "
         "case for its drill history or to clear its times.</p>")},
    {QT_TRANSLATE_NOOP("vcube::HelpText", "Library"),
     QT_TRANSLATE_NOOP(
         "vcube::HelpText",
         "<p>The <b>Set</b> and <b>Cases</b> menus hold every action, with "
         "keys: <b>Ctrl+Z</b> / <b>Ctrl+Shift+Z</b> (or <b>Ctrl+Y</b>) undo "
         "and redo any change, <b>Ctrl+Shift+V</b> pastes algs, "
         "<b>Ctrl+Shift+A</b> makes a case from an alg, <b>Ctrl+Shift+G</b> a "
         "new group, <b>Ctrl+N</b> a new set, <b>Alt+Enter</b> opens the set "
         "properties and <b>Ctrl+Shift+K</b> checks the algs. <b>Delete</b>, "
         "<b>F2</b> and <b>Alt+Up</b> / <b>Alt+Down</b> remove, rename and "
         "move the selected cases or group, or the selected alg when the alg "
         "list has the keys. Ctrl- or Shift-click selects several cases. In "
         "the search box, <b>Enter</b> gives the keys back and <b>Esc</b> "
         "clears it.</p>")},
    {QT_TRANSLATE_NOOP("vcube::HelpText", "Timed"),
     QT_TRANSLATE_NOOP(
         "vcube::HelpText",
         "<p>Hold <b>Space</b> until the time turns green, release to start. "
         "With inspection on, tap Space to start inspecting first. "
         "<b>Esc</b> cancels. Physical cube: any key stops. Virtual cube: "
         "until the timer starts only rotations turn the cube; <b>Space</b> "
         "stops it, and a cube that isn't solved then is a DNF; after a DNF "
         "<b>Space</b> gives the next scramble.</p><p>After a solve: "
         "<b>Alt+1</b> / <b>Alt+2</b> / <b>Alt+3</b> no penalty / toggle +2 "
         "/ toggle DNF, <b>Delete</b> deletes the last solve and "
         "<b>Ctrl+Z</b> puts it back, <b>Alt+C</b> comments on it and "
         "<b>Alt+R</b> reviews it. <b>Ctrl+→</b> skips the scramble.</p>"
         "<p>Alg set scrambles: <b>Tab</b> shows or hides the algs for the "
         "case the scramble sets up. Nothing is graded.</p>")},
    {QT_TRANSLATE_NOOP("vcube::HelpText", "Untimed"),
     QT_TRANSLATE_NOOP(
         "vcube::HelpText",
         "<p>Virtual cube: rotations only until the solve starts (hold "
         "<b>Space</b> until it turns green, then release); <b>Space</b> ends "
         "it (DNF if the cube isn't solved) and the cube then turns freely; "
         "<b>Space</b> again gives the next scramble. <b>Esc</b> starts the "
         "scramble again during a solve, or resets the cube to solved after "
         "it. In the alg field, <b>Enter</b> applies the alg and <b>Esc</b> "
         "returns the keyboard to the cube. Physical cube: <b>Space</b> or "
         "<b>→</b> keeps the scramble as done and shows the next, "
         "<b>Backspace</b> or <b>←</b> goes back.</p><p><b>Delete</b> "
         "deletes the last solve and <b>Ctrl+Z</b> puts it back, "
         "<b>Alt+C</b> / <b>Alt+R</b> comment on or review it, "
         "<b>Ctrl+→</b> skips the scramble. Alg set scrambles: <b>Tab</b> "
         "shows or hides the algs for the case the scramble sets up.</p>")},
    {QT_TRANSLATE_NOOP("vcube::HelpText", "Move Count"),
     QT_TRANSLATE_NOOP(
         "vcube::HelpText",
         "<p>Virtual cube: rotations only until the solve starts (hold "
         "<b>Space</b> until it turns green, then release); <b>Space</b> ends "
         "it, counting the moves (HTM) if the session's step is done and DNF "
         "if not. <b>Esc</b> starts the scramble again. Physical cube: solve "
         "the step, then type the number of moves your solution took: "
         "<b>0–9</b> type the count, <b>Backspace</b> corrects it, <b>Esc</b> "
         "clears it and <b>Enter</b> records it and shows the next scramble; "
         "<b>Alt+3</b> records it as DNF.</p><p><b>Delete</b> deletes the "
         "last solve and <b>Ctrl+Z</b> puts it back, <b>Alt+C</b> / "
         "<b>Alt+R</b> comment on or review it, <b>Ctrl+→</b> skips the "
         "scramble. Double-click a solve to review it (<b>←</b>/<b>→</b>, "
         "<b>Home</b>/<b>End</b> step the replay).</p>")},
    {QT_TRANSLATE_NOOP("vcube::HelpText", "Inspection"),
     QT_TRANSLATE_NOOP(
         "vcube::HelpText",
         "<p><b>Space</b> starts inspection (the cube only turns as a "
         "whole), <b>Space</b> again hides the cube to execute your plan with "
         "the turning keys, and <b>Space</b> reveals it; <b>Space</b> then "
         "gives the next scramble. <b>Esc</b> puts the scramble back (or ends "
         "a retry). After an attempt, rotations turn the cube to look at it. "
         "Bad edges: type the count with <b>0–9</b> (<b>Backspace</b> "
         "corrects) and press <b>Enter</b>; afterwards <b>Tab</b> rings the "
         "other "
         "axis's bad edges.</p>")},
    {QT_TRANSLATE_NOOP("vcube::HelpText", "Blind"),
     QT_TRANSLATE_NOOP(
         "vcube::HelpText",
         "<p>Letter quiz: type the ringed sticker's letter (<b>Enter</b> for "
         "a longer label) or click the sticker named; <b>Space</b> next. Memo "
         "tracing: <b>Space</b> new scramble, <b>Enter</b> to type the edge "
         "letters, <b>Enter</b> for the corners and <b>Enter</b> to check. "
         "Setup moves: turn the cube until the ringed sticker reaches ◎; "
         "<b>Esc</b> starts again, <b>Space</b> skips (typed setups: "
         "<b>Esc</b> outside the field gives the letter up). Blind solves: "
         "<b>Space</b> starts the memo, <b>Space</b> hides the cube for the "
         "execution and <b>Space</b> stops (physical cube: any key stops, "
         "<b>Backspace</b> toggles DNF); <b>Esc</b> cancels.</p>")},
    {QT_TRANSLATE_NOOP("vcube::HelpText", "Alg Lab"),
     QT_TRANSLATE_NOOP(
         "vcube::HelpText",
         "<p>Search tab: pick a case on the left (an alg set's or a lab "
         "case), type an alg and press <b>Enter</b> to add it, or Find algs "
         "to generate them; right-click candidates to add them to the set, "
         "make one the main alg or add them to the test session.</p>"
         "<p>Test tab: <b>Space</b> starts the session. Physical cube: "
         "scramble, hold <b>Space</b> until the time turns green and let go, "
         "do the alg shown, any key stops. Virtual cube: your first turn "
         "starts the timer and it stops when the case is solved "
         "(<b>Space</b> stops early, a DNF). <b>Esc</b> sets the case up "
         "again. After an attempt: <b>Space</b> next alg, "
         "<b>Backspace</b> toggles DNF, <b>Delete</b> deletes the time, "
         "<b>Esc</b> the same alg again. Double-click an alg in the list to "
         "time it next.</p>")},
    {QT_TRANSLATE_NOOP("vcube::HelpText", "Today"),
     QT_TRANSLATE_NOOP(
         "vcube::HelpText",
         "<p>Reviews due and new cases left in each alg set, and the solves "
         "and time spent in each mode today and over the last 7 days. "
         "Double-click a set to learn it.</p>")},
}};

const char *const kWindow = QT_TRANSLATE_NOOP(
    "vcube::HelpText",
    "<p><b>Ctrl+1</b> to <b>Ctrl+9</b> switch between Learn, Drill, "
    "Library, Timed, Untimed, Move Count, Inspection, Blind and Alg Lab, and "
    "<b>Ctrl+0</b> shows Today. Switching is blocked while a solve or "
    "inspection is running.</p><p><b>Ctrl+K</b> opens the command list: "
    "type to find any menu command or a choice on the page (set, session, "
    "scramble type, cube, view, ...) and press <b>Enter</b>. <b>F1</b> "
    "shows or hides these controls. <b>F11</b> toggles full screen, "
    "<b>F10</b> focus mode, which hides everything but the scramble, timer, "
    "cube and algs (right-click shows both options), and <b>Ctrl+B</b> the "
    "sidebar. <b>Ctrl+,</b> opens the settings and <b>Ctrl+Shift+N</b> the "
    "colour neutrality settings.</p>");

QString section(const QString &title, const QString &body) {
  return "<h3>" + title.toHtmlEscaped() + "</h3>" + body;
}

} // namespace

QString controlsHtml(int mode, const Keymap &keymap) {
  QString html;
  int count = static_cast<int>(kModeSections.size());
  auto modeSection = [&](int m) {
    const Section &s = kModeSections[static_cast<std::size_t>(m)];
    return section(tr(s.title), tr(s.body));
  };
  if (mode >= 0 && mode < count) {
    html += modeSection(mode);
  }
  html += section(tr("Modes and window"), tr(kWindow));
  for (int m = 0; m < count; ++m) {
    if (m != mode) {
      html += modeSection(m);
    }
  }
  QStringList rows;
  for (const auto &b : keymap.bindings) {
    rows << QString("<code>%1</code>&nbsp;%2")
                .arg(Keymap::keyText(b.key, b.shift).toHtmlEscaped(),
                     QString::fromStdString(toString(b.move)).toHtmlEscaped());
  }
  html += section(tr("Turning keys (Learn and the virtual cube)"),
                  "<p>" + rows.join(" &nbsp; ") + "</p>");
  return html;
}

} // namespace vcube
