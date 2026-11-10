#include "app/bld_scheme_dialog.hpp"

#include "app/cube_view.hpp"
#include "io/algset_json.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace vcube {

namespace {

constexpr std::array<BldPiece, 2> kKinds = {BldPiece::Edge, BldPiece::Corner};

// A list of 24 strings from the settings, or nullopt if it isn't one.
std::optional<std::array<std::string, kBldStickers>>
readList(const QSettings &q, const QString &key) {
  QStringList list = q.value(key).toStringList();
  if (list.size() != kBldStickers) {
    return std::nullopt;
  }
  std::array<std::string, kBldStickers> out{};
  for (int i = 0; i < kBldStickers; ++i) {
    out[static_cast<std::size_t>(i)] = ss(list[i].trimmed());
  }
  return out;
}

QStringList toList(const std::array<std::string, kBldStickers> &list) {
  QStringList out;
  for (const auto &s : list) {
    out << qs(s);
  }
  return out;
}

} // namespace

QString setupMovesText(int moves) {
  return moves == 1 ? QObject::tr("1 move")
                    : QObject::tr("%1 moves").arg(moves);
}

QString pieceName(BldPiece kind, bool plural) {
  if (kind == BldPiece::Edge) {
    return plural ? QObject::tr("Edges") : QObject::tr("Edge");
  }
  return plural ? QObject::tr("Corners") : QObject::tr("Corner");
}

BlindScheme BlindScheme::load() {
  QSettings q;
  BlindScheme s;
  if (auto letters = readList(q, "blind/edgeLetters")) {
    s.letters.edges = *letters;
  }
  if (auto letters = readList(q, "blind/cornerLetters")) {
    s.letters.corners = *letters;
  }
  if (auto setups = readList(q, "blind/edgeSetups")) {
    s.edgeSetups = *setups;
  }
  if (auto setups = readList(q, "blind/cornerSetups")) {
    s.cornerSetups = *setups;
  }
  Hold h{q.value("blind/top", kWcaHold.top).toInt(),
         q.value("blind/front", kWcaHold.front).toInt()};
  s.hold = isValidHold(h) ? h : kWcaHold;
  // Letters that can't be used (e.g. from a hand-edited file) fall back to
  // Speffz.
  for (BldPiece kind : kKinds) {
    if (!s.letters.problems(kind).empty()) {
      auto &list = kind == BldPiece::Edge ? s.letters.edges : s.letters.corners;
      const auto &speffz = kind == BldPiece::Edge
                               ? LetterScheme::speffz().edges
                               : LetterScheme::speffz().corners;
      list = speffz;
    }
  }
  return s;
}

void BlindScheme::save() const {
  QSettings q;
  q.setValue("blind/edgeLetters", toList(letters.edges));
  q.setValue("blind/cornerLetters", toList(letters.corners));
  q.setValue("blind/edgeSetups", toList(edgeSetups));
  q.setValue("blind/cornerSetups", toList(cornerSetups));
  q.setValue("blind/top", hold.top);
  q.setValue("blind/front", hold.front);
}

const std::string &BlindScheme::ownSetup(BldPiece kind, int sticker) const {
  const auto &list = kind == BldPiece::Edge ? edgeSetups : cornerSetups;
  return list[static_cast<std::size_t>(sticker)];
}

std::string &BlindScheme::ownSetup(BldPiece kind, int sticker) {
  auto &list = kind == BldPiece::Edge ? edgeSetups : cornerSetups;
  return list[static_cast<std::size_t>(sticker)];
}

std::optional<Alg> BlindScheme::setup(BldPiece kind, int sticker) const {
  return op::setupInUse(kind, sticker, ownSetup(kind, sticker));
}

std::array<Alg, kBldStickers> BlindScheme::setups(BldPiece kind) const {
  std::array<Alg, kBldStickers> out{};
  for (int i = 0; i < kBldStickers; ++i) {
    out[static_cast<std::size_t>(i)] = setup(kind, i).value_or(Alg{});
  }
  return out;
}

QString setupProblem(BldPiece kind, int sticker, const std::string &text) {
  ParseResult parsed = parseAlg(text);
  if (!parsed) {
    return QObject::tr("can't read it at character %1")
        .arg(parsed.error->position + 1);
  }
  if (op::isValidSetup(kind, sticker, parsed.alg)) {
    return {};
  }
  // Number every sticker and follow the target's.
  FaceletCube::Stickers ids{};
  for (int i = 0; i < kFaceletCount; ++i) {
    ids[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i);
  }
  FaceletCube where(ids);
  where.apply(parsed.alg);
  int target = bldFacelet(kind, sticker);
  int swap = bldFacelet(kind, op::swapSpot(kind));
  if (where[swap] == target) {
    return kind == BldPiece::Edge
               ? QObject::tr("moves the buffer (UR) or the UFR and UBR "
                             "corners, which the T-perm needs in place")
               : QObject::tr("moves the buffer (UBL) or the UL and UB "
                             "edges, which the Y-perm needs in place");
  }
  for (int f = 0; f < kFaceletCount; ++f) {
    if (where[f] == target) {
      if (auto s = bldSticker(kind, f)) {
        return QObject::tr("brings it to %1, not %2")
            .arg(qs(bldStickerName(kind, *s)),
                 qs(bldStickerName(kind, op::swapSpot(kind))));
      }
    }
  }
  return QObject::tr("doesn't bring it to %1")
      .arg(qs(bldStickerName(kind, op::swapSpot(kind))));
}

BldSchemeDialog::BldSchemeDialog(const BlindScheme &scheme,
                                 const ColourScheme &colours, QWidget *parent)
    : QDialog(parent), scheme_(scheme) {
  setWindowTitle(tr("Lettering and setups"));
  auto *layout = new QVBoxLayout(this);

  auto *holdRow = new QHBoxLayout;
  topBox_ = new QComboBox(this);
  frontBox_ = new QComboBox(this);
  for (int c = 0; c < 6; ++c) {
    topBox_->addItem(colourName(c));
    frontBox_->addItem(colourName(c));
  }
  topBox_->setCurrentIndex(scheme_.hold.top);
  frontBox_->setCurrentIndex(scheme_.hold.front);
  topBox_->setToolTip(tr("The colour on top while you solve blind; the "
                         "letters belong to the stickers as held this way"));
  frontBox_->setToolTip(tr("The colour in front while you solve blind"));
  // Only a click picks a colour: typed letters never change the hold.
  topBox_->setFocusPolicy(Qt::ClickFocus);
  frontBox_->setFocusPolicy(Qt::ClickFocus);
  holdError_ = new QLabel(this);
  holdError_->setStyleSheet("color: #e66");
  holdRow->addWidget(new QLabel(tr("Blind hold:"), this));
  holdRow->addWidget(topBox_);
  holdRow->addWidget(new QLabel(tr("top,"), this));
  holdRow->addWidget(frontBox_);
  holdRow->addWidget(new QLabel(tr("front"), this));
  holdRow->addWidget(holdError_);
  holdRow->addStretch();
  layout->addLayout(holdRow);

  auto *split = new QSplitter(this);
  net_ = new CubeView(split);
  net_->setMode(CubeView::Mode::Net);
  net_->setMinimumSize(320, 240);
  CubeLook look;
  look.scheme = colours;
  net_->setLook(look);
  net_->setToolTip(tr("Click a sticker to find it in the table"));
  tabs_ = new QTabWidget(split);
  for (BldPiece kind : kKinds) {
    auto *t = new QTableWidget(kBldStickers, 4, tabs_);
    t->setHorizontalHeaderLabels(
        {tr("Letter"), tr("Sticker"), tr("Setup"), tr("Check")});
    t->verticalHeader()->setVisible(false);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    t->horizontalHeader()->setSectionResizeMode(SetupCol, QHeaderView::Stretch);
    t->horizontalHeader()->setStretchLastSection(true);
    tables_[kind == BldPiece::Edge ? 0 : 1] = t;
    tabs_->addTab(t, pieceName(kind));
    connect(
        t, &QTableWidget::cellChanged, this,
        [this, kind](int row, int column) { cellChanged(kind, row, column); });
    connect(t, &QTableWidget::currentCellChanged, this, [this, kind](int row) {
      if (!loading_ && row >= 0) {
        selected_ = {kind, row};
        showNet();
      }
    });
  }
  split->addWidget(net_);
  split->addWidget(tabs_);
  split->setStretchFactor(1, 1);
  layout->addWidget(split, 1);

  auto *algs = new QLabel(this);
  algs->setTextFormat(Qt::RichText);
  algs->setWordWrap(true);
  algs->setTextInteractionFlags(Qt::TextSelectableByMouse);
  algs->setText(
      tr("<p>Old Pochmann. <b>Edges</b>: buffer UR, swapped with UL by the "
         "T-perm <code>%1</code>. <b>Corners</b>: buffer UBL, swapped with "
         "RDF (its R sticker) by the Y-perm <code>%2</code>. <b>Parity</b> "
         "(after an odd number of edge targets, before the corners): "
         "<code>%3</code>.</p><p>A setup brings the target sticker to the "
         "swap spot without moving the buffer or the other pieces the swap "
         "alg turns; it is undone after the swap. Leave a setup empty to "
         "use the computed one (grey).</p>")
          .arg(qs(op::swapAlgText(BldPiece::Edge)).toHtmlEscaped(),
               qs(op::swapAlgText(BldPiece::Corner)).toHtmlEscaped(),
               qs(op::parityAlgText()).toHtmlEscaped()));
  layout->addWidget(algs);

  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  auto *speffz =
      buttons->addButton(tr("Speffz letters"), QDialogButtonBox::ResetRole);
  auto *computed =
      buttons->addButton(tr("Computed setups"), QDialogButtonBox::ResetRole);
  speffz->setToolTip(tr("Letter every sticker A to X, as Speffz does"));
  computed->setToolTip(tr("Forget your own setups and use computed ones"));
  connect(speffz, &QPushButton::clicked, this, [this] {
    scheme_.letters = LetterScheme::speffz();
    fillTables();
    showNet();
  });
  connect(computed, &QPushButton::clicked, this, [this] {
    scheme_.edgeSetups = {};
    scheme_.cornerSetups = {};
    fillTables();
  });
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);

  connect(net_, &CubeView::stickerClicked, this, [this](int facelet) {
    for (BldPiece kind : kKinds) {
      if (auto s = bldSticker(kind, facelet)) {
        selectSticker(kind, *s);
        return;
      }
    }
  });
  connect(topBox_, &QComboBox::activated, this, &BldSchemeDialog::holdChanged);
  connect(frontBox_, &QComboBox::activated, this,
          &BldSchemeDialog::holdChanged);

  fillTables();
  holdChanged();
  // The keyboard starts on the letters, not on the blind hold.
  tables_[0]->setFocus();
  resize(980, 640);
}

QTableWidget *BldSchemeDialog::table(BldPiece kind) const {
  return tables_[kind == BldPiece::Edge ? 0 : 1];
}

void BldSchemeDialog::fillTables() {
  for (BldPiece kind : kKinds) {
    for (int i = 0; i < kBldStickers; ++i) {
      fillRow(kind, i);
    }
    table(kind)->resizeColumnToContents(LetterCol);
  }
}

void BldSchemeDialog::fillRow(BldPiece kind, int sticker) {
  loading_ = true;
  QTableWidget *t = table(kind);
  QColor muted = palette().color(QPalette::PlaceholderText);
  auto item = [&](int column, const QString &text, bool editable) {
    auto *it = new QTableWidgetItem(text);
    Qt::ItemFlags flags = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
    if (editable) {
      flags |= Qt::ItemIsEditable;
    }
    it->setFlags(flags);
    t->setItem(sticker, column, it);
    return it;
  };
  item(LetterCol, qs(scheme_.letters.letter(kind, sticker)), true);
  item(StickerCol, qs(bldStickerName(kind, sticker)), false);
  if (!op::isTarget(kind, sticker)) {
    item(SetupCol, QString(), false);
    item(CheckCol, tr("buffer"), false)->setForeground(muted);
    loading_ = false;
    return;
  }
  const std::string &own = scheme_.ownSetup(kind, sticker);
  std::optional<Alg> computed = op::defaultSetup(kind, sticker);
  QString computedText = computed ? qs(toString(*computed)) : QString();
  if (own.empty()) {
    auto *it = item(SetupCol, computedText, true);
    it->setForeground(muted);
    it->setToolTip(tr("Computed; type your own setup to replace it"));
    QString check =
        sticker == op::swapSpot(kind)
            ? tr("the swap spot: no setup")
            : tr("computed, %1")
                  .arg(setupMovesText(computed ? moveCount(*computed) : 0));
    item(CheckCol, check, false)->setForeground(muted);
  } else {
    item(SetupCol, qs(own), true);
    QString problem = setupProblem(kind, sticker, own);
    auto *check = item(CheckCol,
                       problem.isEmpty() ? tr("OK, %1").arg(setupMovesText(
                                               moveCount(parseAlgOrEmpty(own))))
                                         : problem,
                       false);
    check->setForeground(problem.isEmpty() ? statusColour(Status::Good)
                                           : statusColour(Status::Bad));
    check->setToolTip(problem.isEmpty()
                          ? QString()
                          : tr("Until it is fixed, the computed setup %1 is "
                               "used")
                                .arg(computedText));
  }
  loading_ = false;
}

void BldSchemeDialog::cellChanged(BldPiece kind, int row, int column) {
  if (loading_ || row < 0 || row >= kBldStickers) {
    return;
  }
  QString text = table(kind)->item(row, column)->text().trimmed();
  if (column == LetterCol) {
    scheme_.letters.letter(kind, row) = ss(text);
    showNet();
  } else if (column == SetupCol) {
    std::optional<Alg> computed = op::defaultSetup(kind, row);
    std::string clean = cleanAlgText(ss(text));
    bool same = computed && !clean.empty() && parseAlg(clean) &&
                parseAlgOrEmpty(clean) == *computed;
    scheme_.ownSetup(kind, row) = clean.empty() || same ? std::string() : clean;
  }
  fillRow(kind, row);
}

void BldSchemeDialog::showNet() {
  StickerMarks marks;
  for (BldPiece kind : kKinds) {
    for (int i = 0; i < kBldStickers; ++i) {
      marks.labels[static_cast<std::size_t>(bldFacelet(kind, i))] =
          qs(scheme_.letters.letter(kind, i));
    }
  }
  if (selected_) {
    marks.rings.push_back(bldFacelet(selected_->first, selected_->second));
  }
  net_->setMarks(marks);
}

void BldSchemeDialog::selectSticker(BldPiece kind, int sticker) {
  tabs_->setCurrentWidget(table(kind));
  table(kind)->setCurrentCell(sticker, LetterCol);
  table(kind)->scrollToItem(table(kind)->item(sticker, LetterCol));
  // Typing now edits the sticker's letter, rather than going to whatever
  // had the keyboard (such as the blind hold's colours).
  table(kind)->setFocus();
  selected_ = {kind, sticker};
  showNet();
}

void BldSchemeDialog::holdChanged() {
  Hold h{topBox_->currentIndex(), frontBox_->currentIndex()};
  bool valid = isValidHold(h);
  holdError_->setText(valid ? QString()
                            : tr("Top and front must be next to each other"));
  if (auto *ok = findChild<QDialogButtonBox *>()) {
    ok->button(QDialogButtonBox::Ok)->setEnabled(valid);
  }
  if (valid) {
    scheme_.hold = h;
    net_->setColourMap(colourMapOf(h));
  }
  showNet();
}

void BldSchemeDialog::accept() {
  QStringList problems;
  for (BldPiece kind : kKinds) {
    for (const std::string &p : scheme_.letters.problems(kind)) {
      problems << (p.empty()
                       ? tr("%1: a sticker has no letter").arg(pieceName(kind))
                       : tr("%1: \"%2\" is used more than once")
                             .arg(pieceName(kind), qs(p).toHtmlEscaped()));
    }
  }
  if (!problems.isEmpty()) {
    QMessageBox::warning(
        this, tr("Lettering"),
        tr("Every sticker needs its own letter (per piece type):") +
            "<br><br>" + problems.join("<br>"));
    return;
  }
  QDialog::accept();
}

} // namespace vcube
