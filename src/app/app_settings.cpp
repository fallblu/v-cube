#include "app/app_settings.hpp"

#include "app/colours.hpp"
#include "train/algset.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QPainter>
#include <QPalette>
#include <QProxyStyle>
#include <QRegularExpression>
#include <QSettings>
#include <QStyleFactory>
#include <QStyleHints>
#include <QStyleOption>

#include <algorithm>

namespace vcube {

namespace {

// Fusion draws the edges of text fields, spin boxes and check boxes a shade
// darker than the window, which can't be seen on the dark theme (and is
// faint on the light one). This draws them again in the field edge colour,
// 3:1 or more against the window and the field in either theme; a field
// with the focus keeps Fusion's highlighted edge.
class EdgeStyle : public QProxyStyle {
public:
  using QProxyStyle::QProxyStyle;

  void drawPrimitive(PrimitiveElement pe, const QStyleOption *opt, QPainter *p,
                     const QWidget *w) const override {
    QProxyStyle::drawPrimitive(pe, opt, p, w);
    switch (pe) {
    case PE_FrameLineEdit:
      if (!(opt->state & State_HasFocus)) {
        roundedEdge(p, opt->rect);
      }
      break;
    case PE_IndicatorCheckBox:
      p->save();
      p->setRenderHint(QPainter::Antialiasing, false);
      p->setPen(roleColour(Role::FieldEdge));
      p->setBrush(Qt::NoBrush);
      p->drawRect(opt->rect.adjusted(0, 0, -1, -1));
      p->restore();
      break;
    case PE_IndicatorRadioButton:
      p->save();
      p->setRenderHint(QPainter::Antialiasing, true);
      p->setPen(QPen(roleColour(Role::FieldEdge), 1.0));
      p->setBrush(Qt::NoBrush);
      p->drawEllipse(QRectF(opt->rect).adjusted(1.5, 1.5, -1.5, -1.5));
      p->restore();
      break;
    default:
      break;
    }
  }

  void drawComplexControl(ComplexControl cc, const QStyleOptionComplex *opt,
                          QPainter *p, const QWidget *w) const override {
    QProxyStyle::drawComplexControl(cc, opt, p, w);
    if (cc != CC_SpinBox || (opt->state & State_HasFocus)) {
      return;
    }
    const auto *spin = qstyleoption_cast<const QStyleOptionSpinBox *>(opt);
    if (spin && spin->frame) {
      roundedEdge(p, opt->rect);
    }
  }

private:
  // Fusion's field outline: a rounded rectangle on the half pixel.
  static void roundedEdge(QPainter *p, const QRect &r) {
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    p->translate(0.5, 0.5);
    p->setPen(roleColour(Role::FieldEdge));
    p->setBrush(Qt::NoBrush);
    p->drawRoundedRect(r.adjusted(0, 0, -1, -1), 2, 2);
    p->restore();
  }
};

} // namespace

void initSettingsStorage() {
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings ini;
  if (QFile::exists(ini.fileName())) {
    return;
  }
  // Earlier versions used the native format: v-cube.conf on Linux, the
  // registry on Windows.
  QSettings native(QSettings::NativeFormat, QSettings::UserScope,
                   QCoreApplication::organizationName(),
                   QCoreApplication::applicationName());
  const QStringList keys = native.allKeys();
  for (const QString &key : keys) {
    ini.setValue(key, native.value(key));
  }
}

namespace {

QString holdText(Hold h) { return QString("%1%2").arg(h.top).arg(h.front); }

std::optional<Hold> holdFromText(const QString &text) {
  if (text.size() != 2 || !text[0].isDigit() || !text[1].isDigit()) {
    return std::nullopt;
  }
  Hold h{text[0].digitValue(), text[1].digitValue()};
  return isValidHold(h) ? std::optional<Hold>(h) : std::nullopt;
}

ColourNeutrality loadNeutrality(const QSettings &q) {
  ColourNeutrality n;
  if (q.contains("neutrality/main")) {
    n.main =
        holdFromText(q.value("neutrality/main").toString()).value_or(kWcaHold);
    n.holds.clear();
    for (const QString &t : q.value("neutrality/holds").toStringList()) {
      if (auto h = holdFromText(t)) {
        n.holds.push_back(*h);
      }
    }
    n.normalize();
    return n;
  }
  // Earlier versions kept a top and front colour and a choice of fixed,
  // random front or random top and front per alg set in Learn and Drill;
  // start from those of the set last used there.
  QString set = q.value("learn/set", q.value("drill/set")).toString();
  if (set.isEmpty()) {
    return n;
  }
  QString key = "trainer/sets/" + set + "/";
  Hold main{q.value(key + "top", 0).toInt(), q.value(key + "front", 2).toInt()};
  static constexpr Neutrality kOld[] = {Neutrality::Fixed, Neutrality::Y,
                                        Neutrality::Full};
  int old = std::clamp(q.value(key + "neutrality", 0).toInt(), 0, 2);
  return ColourNeutrality::preset(main, kOld[old]);
}

} // namespace

AppSettings AppSettings::load() {
  AppSettings s;
  QSettings q;
  s.darkTheme = q.value("ui/darkTheme", s.darkTheme).toBool();
  auto clamped = [&q](const char *key, int fallback, int lo, int hi) {
    bool ok = false;
    int v = q.value(key, fallback).toInt(&ok);
    return std::clamp(ok ? v : fallback, lo, hi);
  };
  s.animationMs =
      clamped("ui/animationMs", s.animationMs, 0, AppSettings::kMaxAnimationMs);
  s.interfaceTextPercent =
      clamped("ui/interfaceTextPercent", s.interfaceTextPercent,
              kMinInterfacePercent, kMaxInterfacePercent);
  s.scrambleFontPt = clamped("ui/scrambleFontPt", s.scrambleFontPt,
                             kMinScrambleFontPt, kMaxScrambleFontPt);
  s.hintTextPercent = clamped("ui/hintTextPercent", s.hintTextPercent,
                              kMinHintPercent, kMaxHintPercent);
  s.thumbnailSize = clamped("ui/thumbnailSize", s.thumbnailSize, kMinThumbnail,
                            kMaxThumbnail);
  QStringList colours = q.value("ui/colours").toStringList();
  if (colours.size() == 6) {
    for (std::size_t i = 0; i < 6; ++i) {
      QColor c(colours[static_cast<qsizetype>(i)]);
      if (c.isValid()) {
        s.scheme.faces[i] = c;
      }
    }
  }
  if (q.contains("input/keymap")) {
    Keymap k = Keymap::fromStrings(q.value("input/keymap").toStringList());
    if (!k.bindings.empty()) {
      s.keymap = k;
    }
  }
  s.neutrality = loadNeutrality(q);
  s.holdMs = clamped("timer/holdMs", s.holdMs, 0, kMaxHoldMs);
  s.inspection = q.value("timer/inspection", s.inspection).toBool();
  s.inspectionSounds =
      q.value("timer/inspectionSounds", s.inspectionSounds).toBool();
  s.decimals = clamped("timer/decimals", s.decimals, 1, 3);
  s.timerFontPt =
      clamped("timer/fontPt", s.timerFontPt, kMinTimerFontPt, kMaxTimerFontPt);
  s.autoStop = q.value("timer/autoStop", s.autoStop).toBool();
  s.hideBeforeInspection =
      q.value("timer/hideBeforeInspection", s.hideBeforeInspection).toBool();
  s.simplifiedCounts =
      q.value("solves/simplifiedCounts", s.simplifiedCounts).toBool();
  s.optimalAnyHold =
      q.value("solves/optimalAnyHold", s.optimalAnyHold).toBool();
  s.srs.newPerDay = clamped("srs/newPerDay", s.srs.newPerDay, 0, 100);
  if (q.contains("srs/learningSteps")) {
    auto steps = stepsFromText(q.value("srs/learningSteps").toString());
    if (!steps.empty()) {
      s.srs.learningSteps = steps;
    }
  }
  if (q.contains("srs/relearningSteps")) {
    auto steps = stepsFromText(q.value("srs/relearningSteps").toString());
    if (!steps.empty()) {
      s.srs.relearningSteps = steps;
    }
  }
  SrsConfig &c = s.srs;
  c.graduatingDays = std::clamp(
      q.value("srs/graduatingDays", c.graduatingDays).toDouble(), 1.0, 365.0);
  c.easyDays =
      std::clamp(q.value("srs/easyDays", c.easyDays).toDouble(), 1.0, 365.0);
  c.lapseFactor = std::clamp(
      q.value("srs/lapseFactor", c.lapseFactor).toDouble(), 0.0, 1.0);
  c.easeRecovery = std::clamp(
      q.value("srs/easeRecovery", c.easeRecovery).toDouble(), 0.0, 1.0);
  c.creditLateReviews =
      q.value("srs/creditLateReviews", c.creditLateReviews).toBool();
  c.hardAdvancesSteps =
      q.value("srs/hardAdvancesSteps", c.hardAdvancesSteps).toBool();
  c.maxLearning =
      std::max(0, q.value("srs/maxLearning", c.maxLearning).toInt());
  c.learnAheadSeconds = std::max<qint64>(
      0, q.value("srs/learnAheadSeconds", qint64(c.learnAheadSeconds))
             .toLongLong());
  c.leechLapses =
      std::max(0, q.value("srs/leechLapses", c.leechLapses).toInt());
  s.slowRecognitionMs = std::max(
      100, q.value("srs/slowRecognitionMs", s.slowRecognitionMs).toInt());
  s.drill.load(q);
  return s;
}

SettingsParts changedParts(const AppSettings &a, const AppSettings &b) {
  SettingsParts parts;
  auto mark = [&parts](bool changed, SettingsPart part) {
    if (changed) {
      parts |= part;
    }
  };
  mark(a.darkTheme != b.darkTheme, SettingsPart::Theme);
  mark(!(a.scheme == b.scheme) || a.animationMs != b.animationMs,
       SettingsPart::Cube);
  mark(!(a.keymap == b.keymap), SettingsPart::Keys);
  mark(a.neutrality != b.neutrality, SettingsPart::Neutrality);
  mark(a.interfaceTextPercent != b.interfaceTextPercent ||
           a.scrambleFontPt != b.scrambleFontPt ||
           a.hintTextPercent != b.hintTextPercent ||
           a.timerFontPt != b.timerFontPt,
       SettingsPart::Text);
  mark(a.thumbnailSize != b.thumbnailSize, SettingsPart::Thumbnails);
  mark(a.holdMs != b.holdMs || a.inspection != b.inspection ||
           a.inspectionSounds != b.inspectionSounds ||
           a.decimals != b.decimals || a.autoStop != b.autoStop ||
           a.hideBeforeInspection != b.hideBeforeInspection,
       SettingsPart::Timer);
  mark(a.simplifiedCounts != b.simplifiedCounts ||
           a.optimalAnyHold != b.optimalAnyHold,
       SettingsPart::Counts);
  mark(!(a.srs == b.srs) || a.slowRecognitionMs != b.slowRecognitionMs,
       SettingsPart::Srs);
  mark(!(a.drill == b.drill), SettingsPart::Drill);
  return parts;
}

void AppSettings::save() const {
  QSettings q;
  q.setValue("ui/darkTheme", darkTheme);
  q.setValue("ui/animationMs", animationMs);
  q.setValue("ui/interfaceTextPercent", interfaceTextPercent);
  q.setValue("ui/scrambleFontPt", scrambleFontPt);
  q.setValue("ui/hintTextPercent", hintTextPercent);
  q.setValue("ui/thumbnailSize", thumbnailSize);
  QStringList colours;
  for (const auto &c : scheme.faces) {
    colours << c.name();
  }
  q.setValue("ui/colours", colours);
  q.setValue("input/keymap", keymap.toStrings());
  q.setValue("neutrality/main", holdText(neutrality.main));
  QStringList holds;
  for (Hold h : neutrality.holds) {
    holds << holdText(h);
  }
  q.setValue("neutrality/holds", holds);
  q.setValue("timer/holdMs", holdMs);
  q.setValue("timer/inspection", inspection);
  q.setValue("timer/inspectionSounds", inspectionSounds);
  q.setValue("timer/decimals", decimals);
  q.setValue("timer/fontPt", timerFontPt);
  q.setValue("timer/autoStop", autoStop);
  q.setValue("timer/hideBeforeInspection", hideBeforeInspection);
  q.setValue("solves/simplifiedCounts", simplifiedCounts);
  q.setValue("solves/optimalAnyHold", optimalAnyHold);
  q.setValue("srs/newPerDay", srs.newPerDay);
  q.setValue("srs/learningSteps", stepsToText(srs.learningSteps));
  q.setValue("srs/relearningSteps", stepsToText(srs.relearningSteps));
  q.setValue("srs/graduatingDays", srs.graduatingDays);
  q.setValue("srs/easyDays", srs.easyDays);
  q.setValue("srs/lapseFactor", srs.lapseFactor);
  q.setValue("srs/easeRecovery", srs.easeRecovery);
  q.setValue("srs/creditLateReviews", srs.creditLateReviews);
  q.setValue("srs/hardAdvancesSteps", srs.hardAdvancesSteps);
  q.setValue("srs/maxLearning", srs.maxLearning);
  q.setValue("srs/learnAheadSeconds", qint64(srs.learnAheadSeconds));
  q.setValue("srs/leechLapses", srs.leechLapses);
  q.setValue("srs/slowRecognitionMs", slowRecognitionMs);
  drill.save(q);
}

void applyTheme(QApplication &app, bool dark) {
  app.setStyle(new EdgeStyle(QStyleFactory::create("Fusion")));
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
  // Title bars and native dialogs follow the theme too.
  QGuiApplication::styleHints()->setColorScheme(dark ? Qt::ColorScheme::Dark
                                                     : Qt::ColorScheme::Light);
#endif
  QPalette p;
  if (!dark) {
    // Fusion's standard palette follows a dark desktop since Qt 6.5, so
    // the light one is spelled out.
    QColor window(0xef, 0xef, 0xf1);
    QColor text(0x1c, 0x1d, 0x21);
    QColor accent(0x1f, 0x6f, 0xd6);
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, Qt::white);
    p.setColor(QPalette::AlternateBase, QColor(0xf5, 0xf5, 0xf7));
    p.setColor(QPalette::ToolTipBase, QColor(0xff, 0xff, 0xe8));
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, QColor(0x62, 0x63, 0x6a));
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, QColor(0xe4, 0xe4, 0xe8));
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, QColor(0x1a, 0x62, 0xc0));
    p.setColor(QPalette::Light, Qt::white);
    p.setColor(QPalette::Midlight, QColor(0xe0, 0xe0, 0xe4));
    p.setColor(QPalette::Mid, QColor(0xb8, 0xb8, 0xbe));
    p.setColor(QPalette::Dark, QColor(0x9a, 0x9a, 0xa0));
    p.setColor(QPalette::Shadow, QColor(0x50, 0x50, 0x55));
    for (auto role :
         {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
      p.setColor(QPalette::Disabled, role, QColor(0x9a, 0x9b, 0xa0));
    }
    app.setPalette(p);
    return;
  }
  QColor window(0x26, 0x27, 0x2b);
  QColor base(0x1c, 0x1d, 0x21);
  QColor text(0xe6, 0xe6, 0xe8);
  // Dark enough for white text on a selection.
  QColor accent(0x2f, 0x72, 0xd0);
  p.setColor(QPalette::Window, window);
  p.setColor(QPalette::WindowText, text);
  p.setColor(QPalette::Base, base);
  p.setColor(QPalette::AlternateBase, QColor(0x23, 0x24, 0x29));
  p.setColor(QPalette::ToolTipBase, QColor(0x33, 0x34, 0x39));
  p.setColor(QPalette::ToolTipText, text);
  p.setColor(QPalette::PlaceholderText, QColor(0x9d, 0x9e, 0xa4));
  p.setColor(QPalette::Text, text);
  p.setColor(QPalette::Button, QColor(0x31, 0x32, 0x37));
  p.setColor(QPalette::ButtonText, text);
  p.setColor(QPalette::BrightText, Qt::white);
  p.setColor(QPalette::Highlight, accent);
  p.setColor(QPalette::HighlightedText, Qt::white);
  p.setColor(QPalette::Link, QColor(0x6a, 0xaa, 0xff));
  p.setColor(QPalette::Mid, QColor(0x3a, 0x3b, 0x40));
  p.setColor(QPalette::Dark, QColor(0x18, 0x18, 0x1b));
  p.setColor(QPalette::Light, QColor(0x45, 0x46, 0x4c));
  for (auto role :
       {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
    p.setColor(QPalette::Disabled, role, QColor(0x7a, 0x7b, 0x80));
  }
  app.setPalette(p);
}

QString setSettingsKey(const std::string &setId, const QString &name) {
  return QString("trainer/sets/%1/%2").arg(QString::fromStdString(setId), name);
}

QStringList untickedCases(const AlgSet &set) {
  QSettings q;
  QString key = setSettingsKey(set.id, "unticked");
  if (q.contains(key)) {
    return q.value(key).toStringList();
  }
  QString old = setSettingsKey(set.id, "selection");
  if (!q.contains(old)) {
    return {}; // never changed: every case is ticked
  }
  QStringList ticked = q.value(old).toStringList();
  QStringList unticked;
  for (const auto &g : set.groups) {
    for (const auto &c : g.cases) {
      QString id = QString::fromStdString(c.id);
      if (!ticked.contains(id)) {
        unticked << id;
      }
    }
  }
  q.setValue(key, unticked);
  q.remove(old);
  return unticked;
}

void setUntickedCases(const std::string &setId, const QStringList &caseIds) {
  QSettings().setValue(setSettingsKey(setId, "unticked"), caseIds);
}

QString stepsToText(const std::vector<std::int64_t> &steps) {
  QStringList parts;
  for (auto s : steps) {
    if (s % 86400 == 0) {
      parts << QString("%1d").arg(s / 86400);
    } else if (s % 3600 == 0) {
      parts << QString("%1h").arg(s / 3600);
    } else if (s % 60 == 0) {
      parts << QString("%1m").arg(s / 60);
    } else {
      parts << QString("%1s").arg(s);
    }
  }
  return parts.join(' ');
}

std::vector<std::int64_t> stepsFromText(const QString &text) {
  std::vector<std::int64_t> out;
  static const QRegularExpression re("^(\\d+)([smhd]?)$");
  for (const QString &part : text.split(' ', Qt::SkipEmptyParts)) {
    auto m = re.match(part.trimmed());
    if (!m.hasMatch()) {
      return {};
    }
    std::int64_t n = m.captured(1).toLongLong();
    QString unit = m.captured(2);
    std::int64_t mult = unit == "s"   ? 1
                        : unit == "h" ? 3600
                        : unit == "d" ? 86400
                                      : 60;
    out.push_back(n * mult);
  }
  return out;
}

} // namespace vcube
