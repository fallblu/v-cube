#include "app/blind_drill.hpp"

#include "app/session_panel.hpp"
#include "io/algset_json.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace vcube {

bool BlindContext::away() const {
  return !page->isVisible() || !page->isActiveWindow();
}

BlindDrill::BlindDrill(BlindContext &context, QObject *parent)
    : QObject(parent), ctx_(context) {}

QString BlindDrill::letterOf(BldPiece kind, int sticker) const {
  return qs(ctx_.scheme.letters.letter(kind, sticker));
}

QString BlindDrill::stickerText(BldPiece kind, int sticker) const {
  return tr("%1 (%2)").arg(letterOf(kind, sticker),
                           qs(bldStickerName(kind, sticker)));
}

QString BlindDrill::stickerHtml(BldPiece kind, int sticker) const {
  return stickerText(kind, sticker).toHtmlEscaped();
}

void BlindDrill::look(Move m) {
  if (!isRotation(m.type)) {
    emit message(tr("Only rotations: turn the cube as a whole to look at it"));
    return;
  }
  ctx_.cube->animateMove(m);
  ctx_.cube->setMarks(movedMarks(ctx_.cube->marks(), m));
}

// --- shared pieces ---------------------------------------------------------

QString blindTimeText(StatValue ms, int decimals) {
  return QString::fromStdString(formatTime(ms, decimals));
}

QString foldedLetter(const QString &s) {
  return qs(foldLetter(s.trimmed().toStdString()));
}

QComboBox *blindComboBox(const QStringList &items, QWidget *parent) {
  auto *box = new QComboBox(parent);
  box->setFocusPolicy(Qt::NoFocus);
  box->addItems(items);
  return box;
}

AttemptColumn blindColumn(const QString &id, const QString &title) {
  AttemptColumn c;
  c.id = id;
  c.title = title;
  return c;
}

QWidget *labelledBox(const QString &label, QWidget *w, QWidget *parent) {
  auto *box = new QWidget(parent);
  auto *layout = new QHBoxLayout(box);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(new QLabel(label, box));
  w->setParent(box);
  layout->addWidget(w);
  return box;
}

QString colouredHtml(const QColor &colour, const QString &html) {
  return QString("<p style='color:%1'>%2</p>").arg(colour.name(), html);
}

QComboBox *blindPiecesBox(QWidget *parent) {
  QComboBox *box = blindComboBox(
      {QObject::tr("Edges"), QObject::tr("Corners"), QObject::tr("Both")},
      parent);
  box->setToolTip(QObject::tr("The pieces whose letters are asked"));
  return box;
}

BlindStatsPanel::BlindStatsPanel(bool resetButton, QWidget *parent)
    : QWidget(parent) {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  panel_ = new SessionPanel(this);
  panel_->sessionButton()->setVisible(false); // no sessions
  layout->addWidget(panel_, 1);
  if (resetButton) {
    reset_ = flatButton(tr("Reset statistics"), this);
    reset_->setToolTip(tr("Forget this drill's statistics for every letter"));
    layout->addWidget(reset_);
  }
}

void showLetterStats(SessionPanel *panel, const LetterStats &stats,
                     BlindPieces pieces, bool setups,
                     const LetterScheme &letters, int decimals,
                     const QColor &muted) {
  std::vector<LetterChoice> pool = letterPool(
      pieces != BlindPieces::Corners, pieces != BlindPieces::Edges, setups);
  std::vector<AttemptRow> rows;
  int attempts = 0, correct = 0;
  std::int64_t correctMs = 0;
  for (auto [kind, sticker] : pool) {
    const LetterStat &s = stats.of(kind, sticker);
    attempts += s.attempts;
    correct += s.correct;
    correctMs += s.correctMs;
    AttemptRow row;
    row.index = sticker;
    auto add = [&](const QString &text, QColor colour = {}) {
      int col = static_cast<int>(row.cells.size());
      if (col >= 2 && s.attempts == 0) {
        colour = muted;
      }
      row.cells.push_back({text, colour, {}, col > 0});
    };
    add(qs(letters.letter(kind, sticker)));
    add(qs(bldStickerName(kind, sticker)));
    add(QString::number(s.attempts));
    add(s.accuracy() ? QString("%1%").arg(std::lround(*s.accuracy() * 100))
                     : QString("-"),
        s.accuracy() && *s.accuracy() < 0.8 ? QColor(kBlindWrong) : QColor());
    add(s.meanMs()
            ? blindTimeText(static_cast<std::int64_t>(*s.meanMs()), decimals)
            : QString("-"));
    rows.push_back(std::move(row));
  }
  panel->setColumns({blindColumn("letter", QObject::tr("Letter")),
                     blindColumn("sticker", QObject::tr("Sticker")),
                     blindColumn("tries", QObject::tr("Tries")),
                     blindColumn("right", QObject::tr("Right")),
                     blindColumn("mean", QObject::tr("Mean"))});
  panel->setRows(std::move(rows));
  panel->setStatsHtml(
      (setups ? QObject::tr("<p><b>Setup moves</b>: %1 tries, %2 right%3, "
                            "mean %4.</p>")
              : QObject::tr("<p><b>Letter quiz</b>: %1 tries, %2 right%3, "
                            "mean %4.</p>"))
          .arg(attempts)
          .arg(correct)
          .arg(attempts ? QString(" (%1%)").arg(
                              std::lround(100.0 * correct / attempts))
                        : QString())
          .arg(correct ? blindTimeText(correctMs / correct, decimals)
                       : QString("-")));
}

} // namespace vcube
