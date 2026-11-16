#include "app/help_text.hpp"

#include "app/mode_table.hpp"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QStringList>

#include <vector>

namespace vcube {

namespace {

QString tr(const char *text) {
  return QCoreApplication::translate("vcube::HelpText", text);
}

#define T(text) QT_TRANSLATE_NOOP("vcube::HelpText", text)

constexpr int kNoMode = -1;
constexpr int kTurningKeys = -2; // the section written from the keymap

// A section of the controls: paragraphs, a table and more paragraphs,
// worded as Markdown (`code`, **bold**, [links](...)).
struct Section {
  const char *title; // empty: the preamble
  int mode;          // an AppMode, kNoMode or kTurningKeys
  std::vector<const char *> intro;
  std::vector<const char *> columns;
  std::vector<std::vector<const char *>> rows;
  std::vector<const char *> outro;
};

const std::vector<Section> &sections() {
  static const std::vector<Section> all = {
      {"",
       kNoMode,
       {T("Every keyboard control, mode by mode. In the app, `F1` shows the "
          "same controls in a panel beside the page, the current mode's first, "
          "with your own turning keys. The [user guide](user-guide.md) says "
          "more about what each mode does."),
        T("A key only reaches the mode's page when no text field or dialog has "
          "the keyboard. A search box or count field gives the keys back with "
          "Enter (or Esc).")},
       {},
       {

       },
       {}},
      {T("Window"),
       kNoMode,
       {},
       {T("Key"), T("Action")},
       {{T("Ctrl+1 … Ctrl+9, Ctrl+-, Ctrl+="),
         T("Learn, Drill, Library, Alg Lab, Timed, Untimed, Move Count, F2L, "
           "TSLE, Inspection, Blind, in the sidebar's order (not while a "
           "solve or inspection is running)")},
        {T("Ctrl+0"), T("Today")},
        {T("Ctrl+K"), T("command list: type part of any menu command or of a "
                        "choice on the page (alg set, session, scramble type, "
                        "cube, view, ...), then Enter")},
        {T("F1"), T("show or hide the keyboard controls")},
        {T("F9"), T("show or hide the mode's side panel (the case list or the "
                    "session)")},
        {T("F11"), T("full screen")},
        {T("F10"),
         T("focus mode (right-click outside the scramble, timer, cube and algs "
           "to switch it, full screen or the sidebar with the mouse)")},
        {T("Ctrl+B"), T("show or hide the sidebar")},
        {T("Ctrl+,"), T("settings")},
        {T("Ctrl+Shift+N"), T("colour neutrality settings")},
        {T("Ctrl+Q"), T("quit (asks first during a solve)")}},
       {}},
      {T("Turning keys"), kTurningKeys, {}, {}, {}, {}},
      {T("Learn"),
       AppMode::Learn,
       {},
       {T("When"), T("Key"), T("Action")},
       {{T("solving"), T("turning keys"),
         T("turn the cube; solving the case finishes it")},
        {T("solving"), T("Tab"),
         T("show the algs (grades the case Again); in the learning steps of a "
           "guided set, the first Tab shows only the next move (Hard)")},
        {T("solving"), T("Shift+Space"),
         T("give up (grades the case Again). Space alone does nothing, so a "
           "double tap after a case can't give up the next one.")},
        {T("solving"), T("Esc"),
         T("reset the case (grades it Again once the alg is begun; in a set "
           "solved a step at a time, back to where the current case came up)")},
        {T("after a case"), T("Space / Enter"), T("next case")},
        {T("after a case"), T("1 / 2 / 3 / 4"),
         T("change the grade to Again / Hard / Good / Easy")},
        {T("after a case"), T("Esc"), T("reset the cube to practise the case")},
        {T("any time"), T("Ctrl+Z"),
         T("take back the last grade (up to 10) and do the case again")},
        {T("walkthrough"), T("→ / ←"), T("step through the main alg / back")},
        {T("walkthrough"), T("Home or Esc"), T("back to the start")},
        {T("walkthrough"), T("Space / Enter"),
         T("do the case, with each next move shown")},
        {T("nothing due"), T("Space / Enter"),
         T("look again; takes the next learning case early when that is all "
           "there is")},
        {T("Recognise"), T("Space"), T("recalled the alg")},
        {T("Recognise"), T("Shift+Space"),
         T("can't recall it (the algs are shown)")},
        {T("Recognise, afterwards"), T("1-4 / Space or Enter"),
         T("grade and go on / go on with the grade shown")},
        {T("Which case?"), T("1-6"), T("answer")},
        {T("Which case?, afterwards"), T("Space / Enter"), T("next case")},
        {T("Recognise, Which case?"), T("turning keys"),
         T("rotations and U and D turns, to look at the case")},
        {T("Recognise, Which case?"), T("Esc"), T("reset the cube")},
        {T("in context"), T("turning keys"), T("turn the cube")},
        {T("in context"), T("Tab"), T("show the set's cheapest order")},
        {T("in context"), T("Esc"), T("start again")},
        {T("in context"), T("Shift+Space"), T("give up")},
        {T("in context, afterwards"), T("Space / Enter"), T("another one")},
        {T("in context, afterwards"), T("Esc"), T("the same one again")},
        {T("in context"), T("Backspace"), T("back to learning")},
        {T("count field (next to Add new cases)"), T("digits, then Enter"),
         T("start learning that many cases")}},
       {}},
      {T("Drill"),
       AppMode::Drill,
       {},
       {T("When"), T("Key"), T("Action")},
       {{T("virtual, waiting"), T("tap Space, then turn"),
         T("arm the timer; the first face turn starts it (Settings > Timing "
           "and solves can "
           "have Space held and let go instead; a turn made while Space is "
           "held and green starts it too)")},
        {T("virtual, waiting"), T("turning keys"),
         T("rotations only until the timer starts")},
        {T("physical, waiting"), T("hold Space, release"),
         T("start, once the time turns green (Drill's own hold time, Settings "
           "> Drill)")},
        {T("waiting"), T("Esc"),
         T("disarm the timer and set the case up as it was")},
        {T("waiting, running"), T("Shift+Space"),
         T("give the case up (a DNF). Settings > Drill can make Backspace the "
           "give-up key instead.")},
        {T("waiting"), T("Backspace"),
         T("says which key gives up (with Shift+Space as the give-up key)")},
        {T("virtual, running"), T("turning keys"),
         T("any turn; the timer stops by itself when the step is done (unless "
           "turned off in Settings > Timing and solves)")},
        {T("virtual, running"), T("Space"),
         T("stop (a DNF unless the step is done; before any face turn, the "
           "attempt is cancelled)")},
        {T("physical, running"), T("any key but Shift, Ctrl or Alt"),
         T("stop")},
        {T("running"), T("Esc"), T("cancel (nothing is recorded)")},
        {T("after a case"), T("Space / Enter"),
         T("next case. With \"Space practises the case\" after a DNF (Settings "
           "> Drill), Space practises and Enter goes on.")},
        {T("after a case"), T("Tab"),
         T("practise the case, with the algs shown")},
        {T("after a case"), T("Backspace"),
         T("toggle DNF (virtual) or \"wrong alg\" (physical); not for a case "
           "given up")},
        {T("after a case"), T("Delete or Ctrl+Z"),
         T("delete the attempt; one given up before the timer started is taken "
           "back and the case set up again")},
        {T("after a case"), T("Esc"), T("try the case again")},
        {T("practice"), T("Space / Enter"), T("next case")},
        {T("practice"), T("Tab"),
         T("switch between with the algs and without them")},
        {T("practice"), T("turning keys"),
         T("virtual: solve the case; each stage goes on by itself once solved "
           "(Settings > Drill)")},
        {T("practice"), T("Esc"),
         T("virtual: set the case up again for this stage")},
        {T("practice"), T("Backspace"),
         T("toggle DNF or \"wrong alg\" on the attempt")},
        {T("practice"), T("Delete or Ctrl+Z"), T("delete the attempt")}},
       {T("For about 0.3 s after the timer stops, keys are ignored, so that a "
          "second press doesn't skip the result.")}},
      {T("Library"),
       AppMode::Library,
       {},
       {T("Key"), T("Action")},
       {{T("Ctrl+Z / Ctrl+Shift+Z or Ctrl+Y"),
         T("undo / redo any change (Edit menu)")},
        {T("Ctrl+N"), T("new set")},
        {T("Alt+Enter"), T("set properties")},
        {T("Ctrl+Shift+K"), T("check algs")},
        {T("Ctrl+Shift+V"), T("paste algs")},
        {T("Ctrl+Shift+A"), T("new case from alg")},
        {T("Ctrl+Shift+G"), T("new group")},
        {T("Delete"), T("delete the selected cases or group, or remove the "
                        "selected alg in the alg list")},
        {T("F2"), T("rename the selected case or group, or edit the selected "
                    "alg in the alg list")},
        {T("Alt+Up / Alt+Down"),
         T("move the selected cases, group or alg up / down")}},
       {T("Ctrl- or Shift-click selects several cases.")}},
      {T("Case lists (Learn, Drill, Library)"),
       kNoMode,
       {},
       {T("Where"), T("Key"), T("Action")},
       {{T("search box"), T("Enter"), T("give the keys back to the cube")},
        {T("search box"), T("Esc"), T("clear the search")}},
       {}},
      {T("Timed"),
       AppMode::Timed,
       {},
       {T("When"), T("Key"), T("Action")},
       {{T("before a solve"), T("hold Space, release"),
         T("start, once the time turns green (the hold time is in Settings > "
           "Solves). With inspection on, tap Space first to start "
           "inspecting.")},
        {T("before a solve, virtual"), T("turning keys"), T("rotations only")},
        {T("inspection, hold, solve"), T("Esc"),
         T("cancel (the scramble is put back on the virtual cube)")},
        {T("physical, running"), T("any key"), T("stop")},
        {T("virtual, running"), T("turning keys"),
         T("any turn; the timer stops when the cube is solved (unless turned "
           "off in Settings > Timing and solves)")},
        {T("virtual, running"), T("Space"),
         T("stop (a DNF if the cube isn't solved)")},
        {T("after a virtual DNF"), T("Space, Enter or Esc"),
         T("next scramble")},
        {T("after a solve"), T("Alt+1 / Alt+2 / Alt+3"),
         T("no penalty / toggle +2 / toggle DNF on the last solve")},
        {T("after a solve"), T("Backspace"),
         T("toggle DNF on the last solve, as in Drill")},
        {T("after a solve"), T("Alt+C / Alt+R"),
         T("comment on / review the last solve")},
        {T("after a solve"), T("Delete / Ctrl+Z"),
         T("delete the last solve / put the last deleted solve back")},
        {T("after a solve"), T("Ctrl+→"), T("skip the scramble")},
        {T("alg set scramble"), T("Tab"),
         T("show or hide the algs for the case the scramble sets up")}},
       {}},
      {T("Untimed"),
       AppMode::Untimed,
       {},
       {T("When"), T("Key"), T("Action")},
       {{T("virtual, before the solve"), T("turning keys"),
         T("rotations only")},
        {T("virtual, before the solve"), T("hold Space, release"),
         T("start the solve, once the hint turns green")},
        {T("virtual, solving"), T("Space"),
         T("end the solve (a DNF if the cube isn't solved); it also ends by "
           "itself when the cube is solved (Settings > Timing and solves)")},
        {T("virtual, solving"), T("Esc"), T("put the scramble back")},
        {T("virtual, after the solve"), T("turning keys"), T("any turn")},
        {T("virtual, after the solve"), T("Space / Enter"), T("next scramble")},
        {T("virtual, after the solve"), T("Esc"),
         T("reset the cube to solved")},
        {T("virtual, alg field"), T("Enter / Esc"),
         T("apply the alg / give the keys back to the cube")},
        {T("physical"), T("Space or →"),
         T("keep the scramble as done and show the next")},
        {T("physical"), T("Backspace or ←"),
         T("previous scramble (→ goes forward again)")},
        {T("any"), T("Delete / Ctrl+Z"),
         T("delete the last solve / put the last deleted solve back")},
        {T("any"), T("Alt+C / Alt+R"), T("comment on / review the last solve")},
        {T("any"), T("Ctrl+→"), T("skip the scramble")},
        {T("alg set scramble"), T("Tab"),
         T("show or hide the algs for the case the scramble sets up")}},
       {}},
      {T("Move Count"),
       AppMode::MoveCount,
       {},
       {T("When"), T("Key"), T("Action")},
       {{T("virtual, before the solve"), T("turning keys"),
         T("rotations only")},
        {T("virtual, before the solve"), T("hold Space, release"),
         T("start the solve, once the count turns green")},
        {T("virtual, solving"), T("Space"),
         T("end the solve: the move count if the step is done, else a DNF; the "
           "next scramble follows")},
        {T("virtual, solving"), T("Esc"), T("put the scramble back")},
        {T("physical"), T("0-9"), T("type the move count")},
        {T("physical"), T("Backspace / Esc"), T("correct / clear the count")},
        {T("physical"), T("Enter"),
         T("record the count and show the next scramble")},
        {T("physical"), T("Alt+3"), T("record the scramble as DNF")},
        {T("any"), T("Delete / Ctrl+Z"),
         T("delete the last solve / put the last deleted solve back")},
        {T("any"), T("Alt+C / Alt+R"), T("comment on / review the last solve")},
        {T("any"), T("Ctrl+→"), T("skip the scramble")}},
       {}},
      {T("Review"),
       kNoMode,
       {T("In the solve review (Alt+R, or double-click a solve):")},
       {T("Key"), T("Action")},
       {{T("← / →"), T("step the replay back / forward")},
        {T("Home / End"), T("to the scramble / to the end")}},
       {}},
      {T("F2L"),
       AppMode::F2L,
       {},
       {T("When"), T("Key"), T("Action")},
       {{T("solving"), T("turning keys"),
         T("any turn; the clock runs from the first look")},
        {T("Pair solutions, solving"), T("Tab"),
         T("show the best solution's next move, done from the start (the "
           "try counts as hinted)")},
        {T("Pair choice, solving"), T("Tab"),
         T("name the best pair to start with (the pick isn't counted)")},
        {T("solving"), T("Backspace"), T("give up and see the solutions")},
        {T("solving"), T("Esc"), T("put the cube back as it started")},
        {T("afterwards"), T("Space"), T("next")},
        {T("afterwards"), T("Esc"), T("the same state again, not recorded")},
        {T("afterwards, Case explorer"), T("Enter"),
         T("play the chosen solution from the start")},
        {T("afterwards, Case explorer"), T("← / →"),
         T("step back / forward through it")},
        {T("afterwards, Case explorer"), T("1-9"),
         T("choose a solution of the list (or click it to play it)")},
        {T("afterwards, Case explorer"), T("turning keys"),
         T("turn the cube freely")},
        {T("Case explorer"), T("↑ / ↓"), T("the case before / after")},
        {T("Case explorer"), T("Space"), T("another example of the case")},
        {T("Case explorer"), T("Esc"), T("put the cube back")}},
       {T("Full F2L has no hint and nothing to step through: its pairs are "
          "compared in a table.")}},
      {T("TSLE"),
       AppMode::Tsle,
       {},
       {T("When"), T("Key"), T("Action")},
       {{T("solving"), T("turning keys"),
         T("any turn; a step is judged when the trigger is finished")},
        {T("Guided intro, a trigger shown"), T("Enter"),
         T("show the trigger on the cube, then put the cube back to do it")},
        {T("Guided intro"), T("Space"),
         T("skip the case as known; after a case, the next one")},
        {T("Next trigger, Full TSLE, solving"), T("Tab"),
         T("show the best step from here (the try counts as hinted)")},
        {T("Next trigger, Full TSLE, solving"), T("Backspace"),
         T("give up and see the good triggers")},
        {T("solving"), T("Esc"), T("put the cube back as it started")},
        {T("afterwards"), T("Space"), T("next")},
        {T("afterwards"), T("Enter / 1-9"),
         T("play the best trigger / the trigger with that number (or click "
           "it)")},
        {T("afterwards"), T("Esc"), T("the same state again, not recorded")},
        {T("Case explorer"), T("Enter / 1-9"),
         T("do the best trigger / that trigger; the list follows the cube")},
        {T("Case explorer"), T("turning keys"),
         T("turn the cube freely; the list follows it")},
        {T("Case explorer"), T("↑ / ↓"), T("the case before / after")},
        {T("Case explorer"), T("Space"), T("the case at its next AUF")},
        {T("Case explorer"), T("Esc"), T("back to the case")}},
       {}},
      {T("Inspection"),
       AppMode::Inspection,
       {},
       {T("When"), T("Key"), T("Action")},
       {{T("Plan"), T("Space"),
         T("start inspection (the cube is shown), hide the cube, reveal it, "
           "then the next scramble")},
        {T("Plan, inspecting"), T("turning keys"), T("rotations only")},
        {T("Plan, cube hidden"), T("turning keys"), T("any turn, unseen")},
        {T("Plan, afterwards"), T("turning keys"),
         T("rotations, to look at the cube")},
        {T("any"), T("Esc"),
         T("put the scramble back (nothing is recorded), or end a retry")},
        {T("Bad edges"), T("Space"),
         T("show the cube; afterwards, the next scramble")},
        {T("Bad edges"), T("0-9 / Backspace / Enter"),
         T("type a count / correct it / give it (F/B first, then R/L if "
           "asked)")},
        {T("Bad edges, afterwards"), T("Tab"),
         T("ring the other axis's bad edges")},
        {T("between attempts"), T("Delete / Ctrl+Z"),
         T("delete the last attempt / put the last deleted attempt back")},
        {T("between attempts"), T("Alt+R"), T("review the last Plan attempt")},
        {T("between attempts"), T("Ctrl+→"), T("skip the scramble")}},
       {T("A Space in the moment the time limit hides the cube is ignored, and "
          "Space with no moves done doesn't finish.")}},
      {T("Blind"),
       AppMode::Blind,
       {},
       {T("Drill"), T("Key"), T("Action")},
       {{T("Letter quiz"), T("letter keys"), T("answer")},
        {T("Letter quiz"), T("Enter / Backspace"),
         T("answer a longer label / correct")},
        {T("Letter quiz"), T("click a sticker on the net"),
         T("answer a letter → sticker question")},
        {T("Letter quiz, Setup moves"), T("Space"),
         T("next letter; before an answer, give the letter up or skip it (not "
           "for half a second after the quiz moved on by itself)")},
        {T("Setup moves"), T("turning keys"), T("do the setup")},
        {T("Setup moves"), T("Esc"),
         T("start the setup again (after it: try the letter again)")},
        {T("Setup moves, Type it"), T("Esc outside the field"),
         T("give the letter up")},
        {T("Memo tracing"), T("Space"), T("new scramble")},
        {T("Memo tracing"), T("Enter (or Tab)"),
         T("type the letters: Enter after the edges, Enter again to check")},
        {T("Memo tracing"), T("turning keys"),
         T("rotations, to look at the cube (also after the trace)")},
        {T("Memo tracing"), T("Esc"), T("give the scramble up")},
        {T("Blind solves, virtual"), T("Space"),
         T("start the memo and the timer, start the execution (hides the "
           "cube), stop")},
        {T("Blind solves, virtual"), T("turning keys"),
         T("rotations during the memo, any turn during the execution, "
           "rotations afterwards")},
        {T("Blind solves, physical"), T("Space / any key"),
         T("start, end the memo / stop")},
        {T("Blind solves, physical, after a solve"), T("Backspace"),
         T("toggle DNF")},
        {T("Blind solves, between solves"), T("Delete / Ctrl+Z"),
         T("delete the last solve / put the last deleted solve back")},
        {T("Blind solves"), T("Esc"), T("cancel")}},
       {}},
      {T("Alg Lab"),
       AppMode::AlgLab,
       {T("The Search tab is used with the mouse: Enter in the field under the "
          "list adds the alg typed. These keys are for the Test tab.")},
       {T("When"), T("Key"), T("Action")},
       {{T("Before the session or after its last rep"), T("Space"),
         T("start: the next alg is set up")},
        {T("Physical, case set up"), T("Space held, then let go"),
         T("start the timer (hold until the time turns green)")},
        {T("Physical, timer running"), T("any key but Shift, Ctrl, Alt"),
         T("stop")},
        {T("Virtual, case set up"), T("turning keys"),
         T("rotations look at the case; the first layer turn starts the timer, "
           "which stops when the case is solved")},
        {T("Virtual, timer running"), T("Space"),
         T("stop early (a DNF unless solved)")},
        {T("Case set up or timer running"), T("Esc"),
         T("set the case up again (nothing is recorded)")},
        {T("After an attempt"), T("Space"), T("next alg")},
        {T("After an attempt"), T("Backspace"), T("toggle DNF")},
        {T("After an attempt"), T("Delete"),
         T("delete the time and set the case up again")},
        {T("After an attempt"), T("Esc"), T("the same alg again")}},
       {T("Keys are ignored for a moment after the timer stops. Double-click "
          "an alg in the session's list to time it next.")}},
      {T("Today"),
       AppMode::Today,
       {T("Each set has a button to review or learn it; Review all sets "
          "reviews every set's due cases together.")},
       {},
       {

       },
       {}},
  };
  return all;
}

const char *const kTurningIntro = T(
    "The virtual cube (Learn, Drill on the virtual cube, the Solves modes' "
    "virtual sessions, F2L, TSLE, Inspection, Blind, Alg Lab's virtual test "
    "sessions) "
    "is turned with cstimer's face keys. Shift and a face key turns the "
    "wide layer, e.g. Shift+I is `r` and Shift+G is `f'`. Remap them in "
    "Settings > Cube and keys: select a row and **Change** its key or move, or "
    "**Add** one. A turning key may be held with Shift but not Ctrl or "
    "Alt; the digits, arrows, function keys and the modes' own keys can't "
    "be bound.");
const char *const kTurningOutro =
    T("Before a solve starts, the cube only turns as a whole (rotations), so "
      "that a slip can't turn a face early.");

// Markdown's inline marks as rich text.
QString inlineHtml(const QString &markdown) {
  QString s = markdown.toHtmlEscaped();
  static const QRegularExpression code("`([^`]+)`");
  static const QRegularExpression bold("\\*\\*([^*]+)\\*\\*");
  static const QRegularExpression link("\\[([^\\]]+)\\]\\([^)]*\\)");
  s.replace(code, "<code>\\1</code>");
  s.replace(bold, "<b>\\1</b>");
  s.replace(link, "\\1");
  return s;
}

// "key move" pairs of the keymap, e.g. "I R".
QStringList turningPairs(const Keymap &keymap) {
  QStringList out;
  for (const auto &b : keymap.bindings) {
    out << Keymap::keyText(b.key, b.shift) + " " +
               QString::fromStdString(toString(b.move));
  }
  return out;
}

QString sectionHtml(const Section &s, const Keymap &keymap) {
  QString html = "<h3>" + tr(s.title).toHtmlEscaped() + "</h3>";
  if (s.mode == kTurningKeys) {
    html += "<p>" + inlineHtml(tr(kTurningIntro)) + "</p><p>";
    QStringList cells;
    for (const QString &pair : turningPairs(keymap)) {
      cells << "<code>" + pair.section(' ', 0, 0).toHtmlEscaped() +
                   "</code>&nbsp;" + pair.section(' ', 1).toHtmlEscaped();
    }
    html += cells.join(" &nbsp; ") + "</p><p>" + inlineHtml(tr(kTurningOutro)) +
            "</p>";
    return html;
  }
  for (const char *p : s.intro) {
    html += "<p>" + inlineHtml(tr(p)) + "</p>";
  }
  if (!s.rows.empty()) {
    html += "<table cellspacing=\"0\" cellpadding=\"3\">";
    for (const auto &row : s.rows) {
      html += "<tr>";
      for (std::size_t i = 0; i < row.size(); ++i) {
        // The key column: the last but one.
        bool key = i + 2 == row.size();
        QString cell = inlineHtml(tr(row[i]));
        html += key ? "<td><b>" + cell + "</b></td>" : "<td>" + cell + "</td>";
      }
      html += "</tr>";
    }
    html += "</table>";
  }
  for (const char *p : s.outro) {
    html += "<p>" + inlineHtml(tr(p)) + "</p>";
  }
  return html;
}

} // namespace

QString controlsHtml(int mode, const Keymap &keymap) {
  QString html;
  const auto &all = sections();
  for (const Section &s : all) {
    if (s.mode == mode && mode >= 0) {
      html += sectionHtml(s, keymap);
    }
  }
  for (const Section &s : all) {
    if (s.title[0] != 0 && !(s.mode == mode && mode >= 0)) {
      html += sectionHtml(s, keymap);
    }
  }
  return html;
}

QString controlsMarkdown() {
  QString md = "# Keys\n\n<!-- Written from src/app/help_text.cpp by the test "
               "\"keys.md matches the key table\": edit the table, not this "
               "file. -->\n\n";
  auto paragraphs = [&](const std::vector<const char *> &list) {
    for (const char *p : list) {
      md += QString::fromUtf8(p) + "\n\n";
    }
  };
  auto tableRow = [&](const QStringList &cells) {
    md += "| " + cells.join(" | ") + " |\n";
  };
  for (const Section &s : sections()) {
    if (s.title[0] != 0) {
      md += QString("## ") + s.title + "\n\n";
    }
    if (s.mode == kTurningKeys) {
      md += QString::fromUtf8(kTurningIntro) + "\n\n";
      tableRow({"Key", "Move", "Key", "Move", "Key", "Move", "Key", "Move"});
      tableRow({"---", "---", "---", "---", "---", "---", "---", "---"});
      QStringList pairs = turningPairs(Keymap::defaultLayout());
      for (qsizetype i = 0; i < pairs.size(); i += 4) {
        QStringList cells;
        for (qsizetype j = i; j < i + 4; ++j) {
          cells << (j < pairs.size() ? pairs[j].section(' ', 0, 0) : QString())
                << (j < pairs.size() ? pairs[j].section(' ', 1) : QString());
        }
        tableRow(cells);
      }
      md += "\n" + QString::fromUtf8(kTurningOutro) + "\n\n";
      continue;
    }
    paragraphs(s.intro);
    if (!s.rows.empty()) {
      QStringList head, rule;
      for (const char *c : s.columns) {
        head << c;
        rule << "---";
      }
      tableRow(head);
      tableRow(rule);
      for (const auto &row : s.rows) {
        QStringList cells;
        for (const char *c : row) {
          cells << c;
        }
        tableRow(cells);
      }
      md += "\n";
    }
    paragraphs(s.outro);
  }
  return md.trimmed() + "\n";
}

} // namespace vcube
