#pragma once

// File helpers shared by the storage tests.
#include "io/library.hpp"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>

#include <algorithm>

#include <catch2/catch_test_macros.hpp>

namespace vcube::testing {

// The built-in alg sets of the source tree.
inline const QString kBuiltins =
    QStringLiteral(VCUBE_SOURCE_DIR "/resources/algsets");

// Writes a file, making its folder if needed.
inline void writeText(const QString &path, const QByteArray &text) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile f(path);
  REQUIRE(f.open(QIODevice::WriteOnly));
  f.write(text);
}

// A file's contents; empty if it can't be read.
inline QByteArray readText(const QString &path) {
  QFile f(path);
  return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// Files in `dir` whose names start with `prefix`.
inline QStringList filesStarting(const QString &dir, const QString &prefix) {
  return QDir(dir).entryList({prefix + "*"}, QDir::Files);
}

// The library in `dir`, with the built-in sets, loaded.
inline AlgLibrary loadedLibrary(const QString &dir) {
  AlgLibrary lib(dir, kBuiltins);
  lib.load();
  return lib;
}

// How many of the library's errors contain `text`.
inline auto errorsContaining(const AlgLibrary &lib, const QString &text) {
  return std::ranges::count_if(
      lib.errors(), [&text](const QString &e) { return e.contains(text); });
}

} // namespace vcube::testing
