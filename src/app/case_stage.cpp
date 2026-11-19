#include "app/case_stage.hpp"

#include "app/colours.hpp"
#include "app/cube_view.hpp"
#include "cube/move.hpp"
#include "io/algset_json.hpp"
#include "train/alg_guide.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QVBoxLayout>

#include <algorithm>

namespace vcube {

CaseStage::CaseStage(QWidget *parent) : QWidget(parent) {
  auto *top = new QWidget(this);
  auto *topLayout = new QVBoxLayout(top);
  topLayout->setContentsMargins(0, 0, 0, 0);
  header = new QLabel(top);
  header->setTextFormat(Qt::RichText);
  header->setAlignment(Qt::AlignCenter);
  QFont hf = header->font();
  hf.setPointSize(hf.pointSize() + 2);
  header->setFont(hf);
  cube = new CubeView(top);
  // The header and the cube sit at the bottom of the room above the
  // splitter, so that the status and the keys are right under the cube.
  // The cube takes two thirds of the room (as a view not filling its area
  // would draw it).
  cube->setFillsArea(true);
  cube->setMinimumSize(200, 160);
  options = new OptionBar(top);
  topLayout->addWidget(options);
  topLayout->addStretch(1);
  topLayout->addWidget(header);
  topLayout->addWidget(cube, 2);

  auto *bottom = new QWidget(this);
  auto *bottomLayout = new QVBoxLayout(bottom);
  // Clear of the splitter's grip above.
  bottomLayout->setContentsMargins(0, 4, 0, 0);
  status = new QLabel(bottom);
  status->setAlignment(Qt::AlignCenter);
  status->setWordWrap(true);
  guide_ = new QLabel(bottom);
  guide_->setAlignment(Qt::AlignCenter);
  guide_->setWordWrap(true);
  guide_->setTextFormat(Qt::RichText);
  QFont gf = guide_->font();
  gf.setPointSize(gf.pointSize() + 5);
  guide_->setFont(gf);
  guide_->setVisible(false);
  bottomLayout->addWidget(guide_);
  bottomLayout->addWidget(status);
  keys_ = new KeyStrip(bottom);
  bottomLayout->addWidget(keys_);
  actionRow_ = new QWidget(bottom);
  auto *actionLayout = new QHBoxLayout(actionRow_);
  actionLayout->setContentsMargins(0, 4, 0, 4);
  actionRow_->setVisible(false);
  bottomLayout->addWidget(actionRow_);

  // The outcome on one line, the grade buttons on the next.
  gradeRow = new QWidget(bottom);
  auto *gradeRows = new QVBoxLayout(gradeRow);
  gradeRows->setContentsMargins(0, 0, 0, 0);
  auto *outcome = new QHBoxLayout;
  gradeBadge_ = new QLabel(gradeRow);
  QFont bf = gradeBadge_->font();
  bf.setPointSize(bf.pointSize() + 3);
  bf.setBold(true);
  gradeBadge_->setFont(bf);
  gradeBadge_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  gradeLabel_ = new QLabel(gradeRow);
  gradeLabel_->setWordWrap(true);
  outcome->addStretch();
  outcome->addWidget(gradeBadge_);
  outcome->addWidget(gradeLabel_);
  outcome->addStretch();
  gradeRows->addLayout(outcome);
  auto *gradeLayout = new QHBoxLayout;
  gradeRows->addLayout(gradeLayout);
  gradeLayout->addStretch();
  for (QPushButton *&b : gradeButtons) {
    b = new QPushButton(gradeRow);
    b->setFocusPolicy(Qt::NoFocus);
    b->setCheckable(true);
    gradeLayout->addWidget(b);
  }
  gradeLayout->addStretch();
  // The row keeps its place while hidden, so that the cube doesn't change
  // size from one phase to the next.
  QSizePolicy keep = gradeRow->sizePolicy();
  keep.setRetainSizeWhenHidden(true);
  gradeRow->setSizePolicy(keep);
  bottomLayout->addWidget(gradeRow);

  quizRow_ = new QWidget(bottom);
  auto *quizLayout = new QHBoxLayout(quizRow_);
  quizLayout->setContentsMargins(0, 0, 0, 0);
  quizRow_->setVisible(false);
  bottomLayout->addWidget(quizRow_);

  reveal = new QLabel(bottom);
  reveal->setTextFormat(Qt::RichText);
  reveal->setWordWrap(true);
  reveal->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
  reveal->setTextInteractionFlags(Qt::TextSelectableByMouse);
  QFont rf = reveal->font();
  rf.setPointSize(rf.pointSize() + 3);
  reveal->setFont(rf);
  auto *revealScroll = new QScrollArea(bottom);
  revealScroll->setWidget(reveal);
  revealScroll->setWidgetResizable(true);
  revealScroll->setFrameShape(QFrame::NoFrame);
  revealScroll->setFocusPolicy(Qt::NoFocus);
  bottomLayout->addWidget(revealScroll, 1);
  familyRow_ = new QWidget(bottom);
  new QHBoxLayout(familyRow_);
  familyRow_->setVisible(false);
  bottomLayout->addWidget(familyRow_);

  splitter = new QSplitter(Qt::Vertical, this);
  splitter->setObjectName("learnRight");
  splitter->addWidget(top);
  splitter->addWidget(bottom);
  splitter->setStretchFactor(0, 1);
  splitter->setChildrenCollapsible(false);
  // A handle of its own height, so that its grip isn't drawn over the
  // text under it.
  splitter->setHandleWidth(10);
  splitter->setSizes({520, 260});
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(splitter);
}

void CaseStage::setKeys(const QList<KeyHint> &keys) { keys_->setKeys(keys); }

void CaseStage::setActions(const std::vector<Action> &actions) {
  auto *row = static_cast<QHBoxLayout *>(actionRow_->layout());
  while (QLayoutItem *item = row->takeAt(0)) {
    if (QWidget *w = item->widget()) {
      w->deleteLater();
    }
    delete item;
  }
  row->addStretch();
  for (const Action &a : actions) {
    auto *b = new QPushButton(a.text, actionRow_);
    b->setFocusPolicy(Qt::NoFocus);
    // Queued: the action may rebuild these buttons.
    connect(b, &QPushButton::clicked, this, a.run, Qt::QueuedConnection);
    row->addWidget(b);
  }
  row->addStretch();
  actionRow_->setVisible(!actions.empty());
}

void CaseStage::showGuide(const AlgGuide *g, bool nextOnly, bool solving,
                          bool deviated) {
  guide_->setVisible(g != nullptr);
  if (!g) {
    guide_->clear();
    return;
  }
  const QString nextStyle = "color:" + statusColour(Status::Warning).name() +
                            "; font-weight:bold; text-decoration:underline";
  QString muted = palette().color(QPalette::PlaceholderText).name();
  auto token = [g](std::size_t i) {
    return qs(toString(g->moves()[i])).toHtmlEscaped();
  };
  QString text;
  if (nextOnly) {
    // A learning-step hint: only the next move.
    if (g->done()) {
      text = tr("The alg is done.");
    } else {
      text = tr("Next move: %1")
                 .arg("<span style='" + nextStyle + "'>" + token(g->at()) +
                      "</span>");
    }
  } else {
    QStringList parts;
    for (std::size_t i = 0; i < g->moves().size(); ++i) {
      QString t = token(i);
      if (i < g->at()) {
        parts << "<span style='color:" + muted + "'>" + t + "</span>";
      } else if (i == g->at()) {
        parts << "<span style='" + nextStyle + "'>" + t + "</span>";
      } else {
        parts << t;
      }
    }
    text = parts.join(' ');
  }
  if (solving && !g->matched()) {
    text += "<br><span style='font-size:small; color:" + muted + "'>" +
            (deviated ? tr("That isn't the alg: Esc starts the case again.")
                      : tr("Keep going: the cube is between two moves of the "
                           "alg.")) +
            "</span>";
  }
  guide_->setText(text);
}

void CaseStage::showOutcome(const QString &badge, const QString &colour,
                            const QString &detail) {
  gradeBadge_->setText(badge);
  gradeBadge_->setStyleSheet(
      QString("QLabel { background: %1; color: white; border-radius: 4px; "
              "padding: 2px 10px; }")
          .arg(colour));
  // On one line beside the badge while it fits, wrapped after that.
  gradeLabel_->setText(detail);
  gradeLabel_->setMinimumWidth(
      std::min(gradeLabel_->fontMetrics().horizontalAdvance(detail) + 8, 420));
}

void CaseStage::setQuizChoices(const QStringList &names) {
  auto *layout = static_cast<QHBoxLayout *>(quizRow_->layout());
  while (QLayoutItem *item = layout->takeAt(0)) {
    delete item->widget();
    delete item;
  }
  layout->addStretch();
  for (qsizetype i = 0; i < names.size(); ++i) {
    auto *b =
        new QPushButton(QString("%1 %2").arg(i + 1).arg(names[i]), quizRow_);
    b->setFocusPolicy(Qt::NoFocus);
    connect(b, &QPushButton::clicked, this,
            [this, i] { emit quizAnswered(static_cast<std::size_t>(i)); });
    layout->addWidget(b);
  }
  layout->addStretch();
  quizRow_->setVisible(true);
}

void CaseStage::showQuizAnswer(std::size_t right, std::size_t chosen) {
  auto *layout = quizRow_->layout();
  std::size_t button = 0;
  for (int i = 0; i < layout->count(); ++i) {
    auto *b = qobject_cast<QPushButton *>(layout->itemAt(i)->widget());
    if (!b) {
      continue;
    }
    std::size_t k = button++;
    if (k == right) {
      b->setStyleSheet("background:#2f9e55; color:white; font-weight:bold");
    } else if (k == chosen) {
      b->setStyleSheet("background:#d9453b; color:white");
    }
    b->setEnabled(k == right || k == chosen);
  }
}

void CaseStage::hideQuiz() { quizRow_->setVisible(false); }

void CaseStage::showFamily(const std::vector<FamilyCase> &cases) {
  auto *layout = static_cast<QHBoxLayout *>(familyRow_->layout());
  while (QLayoutItem *item = layout->takeAt(0)) {
    delete item->widget();
    delete item;
  }
  familyRow_->setVisible(true);
  QColor muted = palette().color(QPalette::PlaceholderText);
  layout->addStretch();
  for (const FamilyCase &c : cases) {
    auto *cell = new QWidget(familyRow_);
    auto *cellLayout = new QVBoxLayout(cell);
    cellLayout->setContentsMargins(6, 0, 6, 0);
    auto *picture = new QLabel(cell);
    picture->setPixmap(c.picture);
    picture->setAlignment(Qt::AlignCenter);
    auto *text = new QLabel(cell);
    text->setAlignment(Qt::AlignCenter);
    text->setTextFormat(Qt::RichText);
    text->setText(
        QString("<b>%1</b><br><span style='color:%2'>%3</span>")
            .arg(c.name.toHtmlEscaped(),
                 c.done ? statusColour(Status::Warning).name() : muted.name(),
                 c.alg));
    text->setToolTip(c.done ? tr("The case you just did")
                            : tr("A mirror image of it"));
    cellLayout->addWidget(picture);
    cellLayout->addWidget(text);
    layout->addWidget(cell);
  }
  layout->addStretch();
}

void CaseStage::hideFamily() { familyRow_->setVisible(false); }

} // namespace vcube
