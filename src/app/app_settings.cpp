#include "app/app_settings.hpp"

#include "train/algset.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QPalette>
#include <QRegularExpression>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>

#include <algorithm>

namespace vcube {

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
  s.animationMs = q.value("ui/animationMs", s.animationMs).toInt();
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
  s.holdMs = q.value("timer/holdMs", s.holdMs).toInt();
  s.inspection = q.value("timer/inspection", s.inspection).toBool();
  s.decimals = std::clamp(q.value("timer/decimals", s.decimals).toInt(), 1, 3);
  s.autoStop = q.value("timer/autoStop", s.autoStop).toBool();
  s.hideBeforeInspection =
      q.value("timer/hideBeforeInspection", s.hideBeforeInspection).toBool();
  s.simplifiedCounts =
      q.value("solves/simplifiedCounts", s.simplifiedCounts).toBool();
  s.optimalAnyHold = q.value("solves/optimalAnyHold", s.optimalAnyHold).toBool();
  s.srs.newPerDay = q.value("srs/newPerDay", s.srs.newPerDay).toInt();
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
  return s;
}

void AppSettings::save() const {
  QSettings q;
  q.setValue("ui/darkTheme", darkTheme);
  q.setValue("ui/animationMs", animationMs);
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
  q.setValue("timer/decimals", decimals);
  q.setValue("timer/autoStop", autoStop);
  q.setValue("timer/hideBeforeInspection", hideBeforeInspection);
  q.setValue("solves/simplifiedCounts", simplifiedCounts);
  q.setValue("solves/optimalAnyHold", optimalAnyHold);
  q.setValue("srs/newPerDay", srs.newPerDay);
  q.setValue("srs/learningSteps", stepsToText(srs.learningSteps));
  q.setValue("srs/relearningSteps", stepsToText(srs.relearningSteps));
}

void applyTheme(QApplication &app, bool dark) {
  app.setStyle(QStyleFactory::create("Fusion"));
  if (!dark) {
    app.setPalette(app.style()->standardPalette());
    return;
  }
  QPalette p;
  QColor window(0x26, 0x27, 0x2b);
  QColor base(0x1c, 0x1d, 0x21);
  QColor text(0xe6, 0xe6, 0xe8);
  QColor accent(0x3d, 0x8e, 0xf0);
  p.setColor(QPalette::Window, window);
  p.setColor(QPalette::WindowText, text);
  p.setColor(QPalette::Base, base);
  p.setColor(QPalette::AlternateBase, QColor(0x23, 0x24, 0x29));
  p.setColor(QPalette::ToolTipBase, QColor(0x33, 0x34, 0x39));
  p.setColor(QPalette::ToolTipText, text);
  p.setColor(QPalette::PlaceholderText, QColor(0x8a, 0x8b, 0x91));
  p.setColor(QPalette::Text, text);
  p.setColor(QPalette::Button, QColor(0x31, 0x32, 0x37));
  p.setColor(QPalette::ButtonText, text);
  p.setColor(QPalette::BrightText, Qt::white);
  p.setColor(QPalette::Highlight, accent);
  p.setColor(QPalette::HighlightedText, Qt::white);
  p.setColor(QPalette::Link, accent.lighter(120));
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
