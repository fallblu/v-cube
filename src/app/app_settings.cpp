#include "app/app_settings.hpp"
#include "app/app_style.hpp"

#include "app/colours.hpp"
#include "app/platform.hpp"
#include "app/ui_state.hpp"
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
#include <cmath>
#include <limits>
#include <type_traits>

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
        roundedEdge(p, opt);
      }
      break;
    case PE_IndicatorCheckBox:
      p->save();
      p->setRenderHint(QPainter::Antialiasing, false);
      p->setPen(edgeColour(opt));
      p->setBrush(Qt::NoBrush);
      p->drawRect(opt->rect.adjusted(0, 0, -1, -1));
      p->restore();
      break;
    case PE_IndicatorRadioButton:
      p->save();
      p->setRenderHint(QPainter::Antialiasing, true);
      p->setPen(QPen(edgeColour(opt), 1.0));
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
      roundedEdge(p, opt);
    }
  }

private:
  // The field edge colour, half way to the window on a disabled control so
  // that it reads as disabled.
  static QColor edgeColour(const QStyleOption *opt) {
    const QColor edge = roleColour(Role::FieldEdge);
    if (opt->state & State_Enabled) {
      return edge;
    }
    const QColor window = opt->palette.color(QPalette::Window);
    return QColor((edge.red() + window.red()) / 2,
                  (edge.green() + window.green()) / 2,
                  (edge.blue() + window.blue()) / 2);
  }

  // Fusion's field outline: a rounded rectangle on the half pixel.
  static void roundedEdge(QPainter *p, const QStyleOption *opt) {
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    p->translate(0.5, 0.5);
    p->setPen(edgeColour(opt));
    p->setBrush(Qt::NoBrush);
    p->drawRoundedRect(opt->rect.adjusted(0, 0, -1, -1), 2, 2);
    p->restore();
  }
};

} // namespace

void initSettingsStorage(bool migrate) {
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings ini;
  if (!migrate || QFile::exists(ini.fileName())) {
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

// The settings kept as they are, by key: f(key, value) for a bool, and
// f(key, value, lo, hi) for a number brought within lo and hi on load,
// where one that isn't a number reads as the default.
template <typename S, typename F> void forEachValue(S &s, F f) {
  using A = AppSettings;
  f("ui/darkTheme", s.darkTheme);
  f("ui/animationMs", s.animationMs, 0, A::kMaxAnimationMs);
  f("ui/interfaceTextPercent", s.interfaceTextPercent, A::kMinInterfacePercent,
    A::kMaxInterfacePercent);
  f("ui/scrambleFontPt", s.scrambleFontPt, A::kMinScrambleFontPt,
    A::kMaxScrambleFontPt);
  f("ui/hintTextPercent", s.hintTextPercent, A::kMinHintPercent,
    A::kMaxHintPercent);
  f("ui/thumbnailSize", s.thumbnailSize, A::kMinThumbnail, A::kMaxThumbnail);
  f("timer/holdMs", s.holdMs, 0, A::kMaxHoldMs);
  f("timer/inspection", s.inspection);
  f("timer/inspectionSounds", s.inspectionSounds);
  f("timer/decimals", s.decimals, 1, 3);
  f("timer/fontPt", s.timerFontPt, A::kMinTimerFontPt, A::kMaxTimerFontPt);
  f("timer/autoStop", s.autoStop);
  f("timer/hideBeforeInspection", s.hideBeforeInspection);
  f("solves/simplifiedCounts", s.simplifiedCounts);
  f("solves/optimalAnyHold", s.optimalAnyHold);
  f("srs/newPerDay", s.srs.newPerDay, 0, 100);
  f("srs/graduatingDays", s.srs.graduatingDays, 1.0, 365.0);
  f("srs/easyDays", s.srs.easyDays, 1.0, 365.0);
  f("srs/lapseFactor", s.srs.lapseFactor, 0.0, 1.0);
  f("srs/easeRecovery", s.srs.easeRecovery, 0.0, 1.0);
  f("srs/creditLateReviews", s.srs.creditLateReviews);
  f("srs/hardAdvancesSteps", s.srs.hardAdvancesSteps);
  f("drill/autoStop", s.drill.autoStop);
  f("drill/tapToArm", s.drill.tapToArm);
  f("drill/shiftSpaceGivesUp", s.drill.shiftSpaceGivesUp);
  f("drill/practiseAfterDnf", s.drill.practiseAfterDnf);
  f("drill/autoAdvance", s.drill.autoAdvance);
}

// The numbers that read as 0 when they aren't numbers, as they always
// have, before they are brought within lo and hi: f(key, value, lo, hi).
template <typename S, typename F> void forEachUnchecked(S &s, F f) {
  constexpr int kNoMax = std::numeric_limits<int>::max();
  f("srs/maxLearning", s.srs.maxLearning, 0, kNoMax);
  f("srs/leechLapses", s.srs.leechLapses, 0, kNoMax);
  f("srs/slowRecognitionMs", s.slowRecognitionMs, 100, kNoMax);
  f("drill/holdMs", s.drill.holdMs, 0, DrillSettings::kMaxHoldMs);
  f("drill/requeueAfter", s.drill.requeueAfter, 0, 50);
  f("drill/weakestCount", s.drill.weakestCount, 1, 500);
}

} // namespace

AppSettings AppSettings::load() {
  AppSettings s;
  QSettings q;
  // Until it is set, turns aren't animated if the desktop asks for less
  // motion.
  if (!q.contains("ui/animationMs") && platformPrefersReducedMotion()) {
    s.animationMs = 0;
  }
  forEachValue(s, [&q](const char *key, auto &v, auto... range) {
    using T = std::remove_reference_t<decltype(v)>;
    if constexpr (std::is_same_v<T, bool>) {
      v = q.value(key, v).toBool();
    } else if constexpr (std::is_same_v<T, int>) {
      bool ok = false;
      int read = q.value(key, v).toInt(&ok);
      v = std::clamp(ok ? read : v, range...);
    } else {
      // "nan" reads as a number, and std::clamp would pass it on.
      bool ok = false;
      double read = q.value(key, v).toDouble(&ok);
      v = std::clamp(ok && !std::isnan(read) ? read : v, range...);
    }
  });
  forEachUnchecked(s, [&q](const char *key, int &v, int lo, int hi) {
    v = std::clamp(q.value(key, v).toInt(), lo, hi);
  });
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
    Keymap k = Keymap::fromStrings(q.value("input/keymap").toStringList(),
                                   &s.droppedKeyBindings);
    if (!k.bindings.empty()) {
      s.keymap = k;
    }
  }
  s.neutrality = loadNeutrality(q);
  for (auto [key, steps] :
       {std::pair{"srs/learningSteps", &s.srs.learningSteps},
        std::pair{"srs/relearningSteps", &s.srs.relearningSteps}}) {
    if (auto read = stepsFromText(q.value(key).toString()); !read.empty()) {
      *steps = read;
    }
  }
  s.srs.learnAheadSeconds = std::max<qint64>(
      0, q.value("srs/learnAheadSeconds", qint64(s.srs.learnAheadSeconds))
             .toLongLong());
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
  auto write = [&q](const char *key, const auto &v, auto...) {
    q.setValue(key, v);
  };
  forEachValue(*this, write);
  forEachUnchecked(*this, write);
  q.setValue("srs/learnAheadSeconds", qint64(srs.learnAheadSeconds));
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
  q.setValue("srs/learningSteps", stepsToText(srs.learningSteps));
  q.setValue("srs/relearningSteps", stepsToText(srs.relearningSteps));
}

namespace {

// The colours of each theme's palette, by group and role; 0 leaves Fusion's
// colour. Fusion's standard palette follows a dark desktop since Qt 6.5,
// so the light one is spelled out too.
struct PaletteColour {
  QPalette::ColorGroup group;
  QPalette::ColorRole role;
  QRgb light, dark;
};
constexpr PaletteColour kPalette[] = {
    {QPalette::All, QPalette::Window, 0xefeff1, 0x26272b},
    {QPalette::All, QPalette::WindowText, 0x1c1d21, 0xe6e6e8},
    {QPalette::All, QPalette::Base, 0xffffff, 0x1c1d21},
    {QPalette::All, QPalette::AlternateBase, 0xf5f5f7, 0x232429},
    {QPalette::All, QPalette::ToolTipBase, 0xffffe8, 0x333439},
    {QPalette::All, QPalette::ToolTipText, 0x1c1d21, 0xe6e6e8},
    {QPalette::All, QPalette::PlaceholderText, 0x62636a, 0x9d9ea4},
    {QPalette::All, QPalette::Text, 0x1c1d21, 0xe6e6e8},
    {QPalette::All, QPalette::Button, 0xe4e4e8, 0x313237},
    {QPalette::All, QPalette::ButtonText, 0x1c1d21, 0xe6e6e8},
    {QPalette::All, QPalette::BrightText, 0xffffff, 0xffffff},
    // The dark one is dark enough for white text on a selection.
    {QPalette::All, QPalette::Highlight, 0x1f6fd6, 0x2f72d0},
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    // Spelled out, or a platform with an accent of its own (Windows 11)
    // would bring the desktop's to the controls that use it.
    {QPalette::All, QPalette::Accent, 0x1f6fd6, 0x2f72d0},
#endif
    {QPalette::All, QPalette::HighlightedText, 0xffffff, 0xffffff},
    {QPalette::All, QPalette::Link, 0x1a62c0, 0x6aaaff},
    {QPalette::All, QPalette::Light, 0xffffff, 0x45464c},
    {QPalette::All, QPalette::Midlight, 0xe0e0e4, 0},
    {QPalette::All, QPalette::Mid, 0xb8b8be, 0x3a3b40},
    {QPalette::All, QPalette::Dark, 0x9a9aa0, 0x18181b},
    {QPalette::All, QPalette::Shadow, 0x505055, 0},
    {QPalette::Disabled, QPalette::WindowText, 0x9a9ba0, 0x7a7b80},
    {QPalette::Disabled, QPalette::Text, 0x9a9ba0, 0x7a7b80},
    {QPalette::Disabled, QPalette::ButtonText, 0x9a9ba0, 0x7a7b80},
    {QPalette::Disabled, QPalette::PlaceholderText, 0x9a9ba0, 0x7a7b80},
    // A disabled field sinks into the window, so that it doesn't look
    // enabled.
    {QPalette::Disabled, QPalette::Base, 0xefeff1, 0x26272b},
    {QPalette::Disabled, QPalette::Button, 0xe9e9ec, 0x2c2d31},
    {QPalette::Disabled, QPalette::Highlight, 0xa8a9ae, 0x55565c},
};

} // namespace

void applyTheme(QApplication &app, bool dark) {
  // Taller controls and menu arrows (AppStyle) over visible field edges
  // (EdgeStyle) over Fusion.
  app.setStyle(new AppStyle(new EdgeStyle(QStyleFactory::create("Fusion"))));
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
  // Title bars and native dialogs follow the theme too.
  QGuiApplication::styleHints()->setColorScheme(dark ? Qt::ColorScheme::Dark
                                                     : Qt::ColorScheme::Light);
#endif
  QPalette p;
  for (const PaletteColour &c : kPalette) {
    if (QRgb rgb = dark ? c.dark : c.light) {
      p.setColor(c.group, c.role, QColor(rgb));
    }
  }
  app.setPalette(p);
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
    bool ok = false;
    std::int64_t n = m.captured(1).toLongLong(&ok);
    QString unit = m.captured(2);
    std::int64_t mult = unit == "s"   ? 1
                        : unit == "h" ? 3600
                        : unit == "d" ? 86400
                                      : 60;
    // At most ten years a step: longer is a typo, and the product of a long
    // run of digits would overflow.
    constexpr std::int64_t kMaxStep = std::int64_t{3650} * 86400;
    if (!ok || n > kMaxStep / mult) {
      return {};
    }
    out.push_back(n * mult);
  }
  return out;
}

} // namespace vcube
