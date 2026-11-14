#include "app/page_parts.hpp"

#include "app/cube_view.hpp"
#include "app/flow_layout.hpp"
#include "app/scramble_source.hpp"
#include "app/session_panel.hpp"
#include "app/widgets.hpp"

#include <QButtonGroup>
#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
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
  QFont f = label->font();
  f.setPointSizeF(f.pointSizeF() * 0.9);
  label->setFont(f);
  return label;
}

void showTipsAsHelp(QFormLayout *form) {
  for (int row = form->rowCount() - 1; row >= 0; --row) {
    QLayoutItem *item = form->itemAt(row, QFormLayout::FieldRole);
    if (!item) {
      item = form->itemAt(row, QFormLayout::SpanningRole);
    }
    QWidget *field = item ? item->widget() : nullptr;
    if (!field || field->toolTip().isEmpty()) {
      continue;
    }
    form->insertRow(row + 1, QString(),
                    helpLabel(field->toolTip(), form->parentWidget()));
  }
}

QComboBox *createCubeKindBox(QWidget *parent) {
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
  QFont sf = scramble->font();
  sf.setPointSize(17);
  scramble->setFont(sf);
  markScrambleLabel(scramble);
  scramble->setMinimumHeight(60);
  net = new NetPreview(this);
  scrambleRow->addWidget(scramble, 1);
  scrambleRow->addWidget(net);
  layout_->addLayout(scrambleRow);

  cube = new CubeView(this);
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
  centre = new QVBoxLayout;
  centre->addStretch(1);
  centre->addWidget(cube, 8);
  centre->addWidget(display);
  centre->addWidget(hint);
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

void SolveStage::addStandardOptions() {
  options->addSpacing();
  options->addWidget(cubeBox);
  cubeBox->show();
  viewPicker = options->addViewPicker(cube, mode_);
  options->addWidget(skipButton);
  skipButton->show();
}

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

void SegmentedPicker::addOption(const QString &text, const QVariant &data) {
  auto *b = new QPushButton(text, this);
  b->setCheckable(true);
  b->setFocusPolicy(Qt::NoFocus);
  group_->addButton(b, static_cast<int>(data_.size()));
  layout_->addWidget(b);
  if (data_.isEmpty()) {
    b->setChecked(true);
  }
  data_ << data;
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

int SegmentedPicker::findData(const QVariant &data) const {
  return static_cast<int>(data_.indexOf(data));
}

QString keyHintsHtml(const QList<KeyHint> &keys, const QPalette &palette) {
  // A chip: the key on a slightly raised background, in the text colour.
  QColor chip = palette.color(QPalette::Button);
  QColor key = palette.color(QPalette::ButtonText);
  QStringList parts;
  for (const KeyHint &k : keys) {
    parts << QStringLiteral("<span style=\"background-color:%1; color:%2;\">"
                            "&nbsp;%3&nbsp;</span>&nbsp;%4")
                 .arg(chip.name(), key.name(), k.key.toHtmlEscaped(),
                      k.action.toHtmlEscaped());
  }
  return parts.join(QStringLiteral(" &nbsp;&nbsp; "));
}

HintBar::HintBar(QWidget *parent) : QLabel(parent) {
  setAlignment(Qt::AlignCenter);
  setWordWrap(true);
  setTextFormat(Qt::RichText);
  setForegroundRole(QPalette::PlaceholderText);
  setProperty("vcubeHint", true);
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

} // namespace vcube
