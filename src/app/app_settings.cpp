#include "app/app_settings.hpp"

#include <QPalette>
#include <QRegularExpression>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>

namespace vcube {

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
  s.holdMs = q.value("timer/holdMs", s.holdMs).toInt();
  s.inspection = q.value("timer/inspection", s.inspection).toBool();
  s.decimals = std::clamp(q.value("timer/decimals", s.decimals).toInt(), 1, 3);
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
  q.setValue("timer/holdMs", holdMs);
  q.setValue("timer/inspection", inspection);
  q.setValue("timer/decimals", decimals);
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
  for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
    p.setColor(QPalette::Disabled, role, QColor(0x7a, 0x7b, 0x80));
  }
  app.setPalette(p);
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
    std::int64_t mult = unit == "s" ? 1 : unit == "h" ? 3600 : unit == "d" ? 86400 : 60;
    out.push_back(n * mult);
  }
  return out;
}

} // namespace vcube
