#include "app/page_parts.hpp"

#include "app/cube_view.hpp"
#include "app/flow_layout.hpp"
#include "app/scramble_source.hpp"
#include "app/text_style.hpp"
#include "app/widgets.hpp"

#include <QButtonGroup>
#include <QComboBox>
#include <QDialog>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QVBoxLayout>

namespace vcube {

OptionBar::OptionBar(QWidget *parent) : QWidget(parent) {
  layout_ = new FlowLayout(this);
  layout_->setContentsMargins(0, 0, 0, 0);
}

QWidget *OptionBar::addPicker(const QString &label, QWidget *control,
                              int stretch) {
  auto *item = new QWidget(this);
  auto *row = new QHBoxLayout(item);
  row->setContentsMargins(0, 0, 0, 0);
  auto *text = new QLabel(label, item);
  control->setParent(item);
  text->setBuddy(control);
  row->addWidget(text);
  row->addWidget(control, 1);
  layout_->addWidget(item, stretch);
  return item;
}

QWidget *OptionBar::addViewPicker(CubeView *view, const QString &mode) {
  return addPicker(tr("View:"), createViewSelector(view, mode, this));
}

void OptionBar::addWidget(QWidget *w, int stretch) {
  w->setParent(this);
  layout_->addWidget(w, stretch);
}

void OptionBar::addSpacing(int size) { layout_->addSpacing(size); }

void OptionBar::addStretch(int stretch) { layout_->addStretch(stretch); }

QLabel *helpLabel(const QString &text, QWidget *parent) {
  auto *label = new QLabel(text, parent);
  label->setWordWrap(true);
  label->setForegroundRole(QPalette::PlaceholderText);
  setTextRole(label, TextRole::Caption);
  return label;
}

void showTipsAsHelp(QFormLayout *form) {
  for (int row = form->rowCount() - 1; row >= 0; --row) {
    QLayoutItem *item = form->itemAt(row, QFormLayout::FieldRole);
    const bool spanning = !item;
    if (spanning) {
      item = form->itemAt(row, QFormLayout::SpanningRole);
    }
    QWidget *field = item ? item->widget() : nullptr;
    if (!field || field->toolTip().isEmpty()) {
      continue;
    }
    // The field and its help as one cell, so that each help line sits
    // close under its own field and every row is spaced alike.
    QFormLayout::TakeRowResult taken = form->takeRow(row);
    QWidget *label = taken.labelItem ? taken.labelItem->widget() : nullptr;
    if (!label && !spanning) {
      // An empty label, so that the row is laid out as the others are.
      label = new QLabel(form->parentWidget());
    }
    delete taken.labelItem;
    delete taken.fieldItem;
    auto *cell = new QWidget(form->parentWidget());
    auto *column = new QVBoxLayout(cell);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(2);
    const bool grows =
        field->sizePolicy().horizontalPolicy() & QSizePolicy::ExpandFlag;
    column->addWidget(field, 0, grows ? Qt::Alignment() : Qt::AlignLeft);
    column->addWidget(helpLabel(field->toolTip(), cell));
    if (spanning) {
      form->insertRow(row, cell);
    } else {
      form->insertRow(row, label, cell);
      if (auto *text = qobject_cast<QLabel *>(label)) {
        text->setBuddy(field);
      }
    }
  }
}

// The two-item drop-down "Virtual cube" / "Physical cube".
static QComboBox *createCubeKindBox(QWidget *parent) {
  auto *box = new QComboBox(parent);
  box->setFocusPolicy(Qt::NoFocus);
  box->addItem(QObject::tr("Virtual cube"));
  box->addItem(QObject::tr("Physical cube"));
  return box;
}

SolveStage::SolveStage(const QString &mode, QWidget *parent) : QWidget(parent) {
  layout_ = new QVBoxLayout(this);
  layout_->setContentsMargins(0, 0, 0, 0);
  options = new OptionBar(this);
  layout_->addWidget(options);

  auto *scrambleRow = new QHBoxLayout;
  scramble = new QLabel(this);
  scramble->setWordWrap(true);
  scramble->setAlignment(Qt::AlignCenter);
  scramble->setTextInteractionFlags(Qt::TextSelectableByMouse);
  markScrambleLabel(scramble);
  scramble->setMinimumHeight(60);
  net = new NetPreview(this);
  scrambleRow->addWidget(scramble, 1);
  scrambleRow->addWidget(net);

  cube = new CubeView(this);
  cube->setFillsArea(true);
  display = new TimeDisplay(this);
  hint = new QLabel(this);
  hint->setAlignment(Qt::AlignCenter);
  hint->setWordWrap(true);
  hint->setForegroundRole(QPalette::PlaceholderText);
  focusStats = new QLabel(this);
  focusStats->setAlignment(Qt::AlignCenter);
  focusStats->setForegroundRole(QPalette::PlaceholderText);
  focusStats->setVisible(false);
  buttons_ = new QHBoxLayout;
  buttons_->addStretch();
  buttons_->addStretch();
  keys_ = new KeyStrip(this);
  // One block centred in the height: the scramble, the cube, and right
  // under it what goes with it (the time, the hint, the keys, buttons).
  centre = new QVBoxLayout;
  centre->addStretch(1);
  centre->addLayout(scrambleRow);
  centre->addWidget(cube, 3);
  centre->addWidget(display);
  centre->addWidget(hint);
  centre->addWidget(keys_);
  centre->addWidget(focusStats);
  centre->addLayout(buttons_);
  centre->addStretch(1);
  layout_->addLayout(centre, 3);

  cubeBox = createCubeKindBox(options);
  cubeBox->setToolTip(tr("The cube this session's solves are done on (fixed "
                         "once the session has solves)"));
  cubeBox->hide();
  viewPicker = nullptr;
  skipButton = flatButton(tr("Skip scramble"), options);
  skipButton->setToolTip(
      tr("Go on to the next scramble without solving this one (Ctrl+→)"));
  skipButton->hide();
  mode_ = mode;
}

void SolveStage::setCubeStretch(int stretch) {
  centre->setStretchFactor(cube, stretch);
}

void SolveStage::addStandardOptions() {
  options->addSpacing();
  options->addWidget(cubeBox);
  cubeBox->show();
  viewPicker = options->addViewPicker(cube, mode_);
  options->addWidget(skipButton);
  skipButton->show();
}

void SolveStage::setKeys(const QList<KeyHint> &keys) { keys_->setKeys(keys); }

void SolveStage::addButton(QWidget *button, bool first) {
  buttons_->insertWidget(first ? 1 : buttons_->count() - 1, button);
}

void SolveStage::addAboveCube(QWidget *w) {
  centre->insertWidget(centre->indexOf(cube), w);
}

void SolveStage::addUnderButtons(QWidget *w) {
  centre->insertWidget(centre->count() - 1, w);
}

void SolveStage::addAboveChart(QWidget *w) {
  layout_->insertWidget(chart_ ? layout_->indexOf(chart_) : layout_->count(),
                        w);
}

void SolveStage::setChart(TrendChart *chart) {
  chart_ = chart;
  chart->setParent(this);
  layout_->addWidget(chart, 1);
}

SegmentedPicker::SegmentedPicker(QWidget *parent) : QWidget(parent) {
  layout_ = new QHBoxLayout(this);
  layout_->setContentsMargins(0, 0, 0, 0);
  layout_->setSpacing(0);
  group_ = new QButtonGroup(this);
  group_->setExclusive(true);
  connect(group_, &QButtonGroup::idClicked, this, &SegmentedPicker::activated);
}

void SegmentedPicker::addOption(const QString &text, const QVariant &value) {
  auto *b = new QPushButton(text, this);
  b->setCheckable(true);
  b->setFocusPolicy(Qt::NoFocus);
  group_->addButton(b, static_cast<int>(data_.size()));
  layout_->addWidget(b);
  if (data_.isEmpty()) {
    b->setChecked(true);
  }
  data_ << value;
}

int SegmentedPicker::currentIndex() const { return group_->checkedId(); }

void SegmentedPicker::setCurrentIndex(int index) {
  if (QAbstractButton *b = group_->button(index)) {
    b->setChecked(true);
  }
}

QVariant SegmentedPicker::currentData() const {
  return data_.value(currentIndex());
}

int SegmentedPicker::findData(const QVariant &value) const {
  return static_cast<int>(data_.indexOf(value));
}

QString keyHintsHtml(const QList<KeyHint> &keys, const QPalette &palette) {
  // A chip: the key on a raised background, in the text colour. The
  // button colour is a step further from the window, which it is close to.
  QColor chip = palette.color(QPalette::Button);
  chip = palette.color(QPalette::Window).lightness() >= 128 ? chip.darker(108)
                                                            : chip.lighter(120);
  QColor key = palette.color(QPalette::ButtonText);
  QStringList parts;
  for (qsizetype i = 0; i < keys.size(); ++i) {
    const KeyHint &k = keys[i];
    QString part =
        QStringLiteral("<span style=\"background-color:%1; color:%2;\">"
                       "&nbsp;%3&nbsp;</span>&nbsp;%4")
            .arg(chip.name(), key.name(),
                 // A key and its action stay on one line.
                 k.key.toHtmlEscaped().replace(' ', "&nbsp;"),
                 k.action.toHtmlEscaped().replace(' ', "&nbsp;"));
    if (k.click) {
      part = QStringLiteral("<a href=\"chip:%1\" style=\"color:%2; "
                            "text-decoration:none;\">%3</a>")
                 .arg(i)
                 .arg(palette.color(QPalette::Link).name(), part);
    }
    parts << part;
  }
  return parts.join(QStringLiteral(" &nbsp;&nbsp; "));
}

HintBar::HintBar(QWidget *parent) : QLabel(parent) {
  setAlignment(Qt::AlignCenter);
  setWordWrap(true);
  setTextFormat(Qt::RichText);
  setForegroundRole(QPalette::PlaceholderText);
  setProperty("vcubeHint", true);
  // The chips that keyHintsHtml made links can be clicked: the click runs
  // the action of that key.
  setTextInteractionFlags(Qt::LinksAccessibleByMouse);
  setFocusPolicy(Qt::NoFocus); // the keys stay with the page
  setOpenExternalLinks(false);
  connect(this, &QLabel::linkActivated, this, [this](const QString &link) {
    if (!link.startsWith(QStringLiteral("chip:"))) {
      return;
    }
    qsizetype i = link.mid(5).toLongLong();
    if (i < 0 || i >= keys_.size() || !keys_[i].click) {
      return;
    }
    // Later: the action may change the chips.
    QMetaObject::invokeMethod(this, keys_[i].click, Qt::QueuedConnection);
  });
}

void HintBar::setHint(const QString &message, const QList<KeyHint> &keys) {
  message_ = message;
  keys_ = keys;
  render();
}

void HintBar::changeEvent(QEvent *e) {
  QLabel::changeEvent(e);
  if (e->type() == QEvent::PaletteChange) {
    render();
  }
}

void HintBar::render() {
  QString html = message_.toHtmlEscaped();
  if (!keys_.isEmpty()) {
    if (!html.isEmpty()) {
      html += QStringLiteral("<br>");
    }
    html += keyHintsHtml(keys_, palette());
  }
  setText(html);
  setVisible(!html.isEmpty());
}

QSplitter *mainSplitter(QWidget *page, const QList<QWidget *> &widgets,
                        const QList<int> &sizes) {
  auto *split = new QSplitter(page);
  split->setObjectName("main");
  for (QWidget *w : widgets) {
    split->addWidget(w);
  }
  for (int i = 1; i < widgets.size(); ++i) {
    split->setStretchFactor(i, 1);
  }
  split->setSizes(sizes);
  return split;
}

QDialogButtonBox *addButtonBox(QDialog *dialog, QBoxLayout *layout,
                               QDialogButtonBox::StandardButtons buttons) {
  auto *box = new QDialogButtonBox(buttons, dialog);
  QObject::connect(box, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
  QObject::connect(box, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  layout->addWidget(box);
  return box;
}

QScrollArea *scrollArea(QWidget *content, QWidget *parent) {
  auto *scroll = new QScrollArea(parent);
  scroll->setWidget(content);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setFocusPolicy(Qt::NoFocus);
  return scroll;
}

QPushButton *flatButton(const QString &text, QWidget *parent) {
  auto *b = new QPushButton(text, parent);
  b->setFocusPolicy(Qt::NoFocus);
  return b;
}

} // namespace vcube
