#include "app/sounds.hpp"

#include <QApplication>
#include <QTimer>

#ifdef VCUBE_HAVE_MULTIMEDIA
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QSoundEffect>
#include <QStandardPaths>
#include <QUrl>

#include <cmath>
#include <numbers>
#endif

namespace vcube {

namespace {

#ifdef VCUBE_HAVE_MULTIMEDIA

// A short sine beep as a 16-bit mono WAV file.
QByteArray beepWav(double hz, int ms) {
  constexpr int kRate = 44100;
  int samples = kRate * ms / 1000;
  QByteArray pcm;
  pcm.reserve(samples * 2);
  for (int i = 0; i < samples; ++i) {
    double t = static_cast<double>(i) / kRate;
    // Fade in and out over 5 ms so that it doesn't click.
    double edge = std::min(
        {1.0, t / 0.005, static_cast<double>(samples - i) / kRate / 0.005});
    auto v = static_cast<qint16>(
        std::lround(9000.0 * edge * std::sin(2 * std::numbers::pi * hz * t)));
    pcm.append(static_cast<char>(v & 0xff));
    pcm.append(static_cast<char>((v >> 8) & 0xff));
  }
  auto u32 = [](QByteArray &b, quint32 v) {
    for (int i = 0; i < 4; ++i) {
      b.append(static_cast<char>((v >> (8 * i)) & 0xff));
    }
  };
  auto u16 = [](QByteArray &b, quint16 v) {
    b.append(static_cast<char>(v & 0xff));
    b.append(static_cast<char>((v >> 8) & 0xff));
  };
  QByteArray wav("RIFF");
  u32(wav, static_cast<quint32>(36 + pcm.size()));
  wav.append("WAVEfmt ");
  u32(wav, 16);
  u16(wav, 1); // PCM
  u16(wav, 1); // mono
  u32(wav, kRate);
  u32(wav, kRate * 2);
  u16(wav, 2);
  u16(wav, 16);
  wav.append("data");
  u32(wav, static_cast<quint32>(pcm.size()));
  wav.append(pcm);
  return wav;
}

QSoundEffect *beep() {
  static QSoundEffect *effect = [] {
    QString dir =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(dir);
    QString path = QDir(dir).filePath("inspection-call.wav");
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
      f.write(beepWav(880, 120));
      f.close();
    }
    auto *e = new QSoundEffect(qApp);
    e->setSource(QUrl::fromLocalFile(path));
    e->setVolume(0.6f);
    return e;
  }();
  return effect;
}

void playBeep() {
  QSoundEffect *e = beep();
  if (e->status() == QSoundEffect::Error) {
    QApplication::beep();
  } else {
    e->play();
  }
}

#else

void playBeep() { QApplication::beep(); }

#endif

} // namespace

void prepareSounds() {
#ifdef VCUBE_HAVE_MULTIMEDIA
  beep();
#endif
}

void playInspectionCall(int seconds) {
  playBeep();
  if (seconds >= 12) {
    QTimer::singleShot(200, qApp, [] { playBeep(); });
  }
}

} // namespace vcube
