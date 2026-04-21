#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QString>

#include "globals.h"

namespace AppSettings {
inline QString dataRootPath() {
    // Prefer the platform-specific writable app-data directory, then fall back to a
    // repo-local folder when that location is unavailable.
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) {
        base = QDir(QDir::homePath()).filePath(AppGlobals::fallbackDataRoot());
    }
    QDir dir(base);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    return base;
}

inline QString dataDirPath() {
    // Runtime DB files and generated assets live under a dedicated child directory.
    const QString base = QDir(dataRootPath()).filePath(AppGlobals::dataFolderName());
    QDir dir(base);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    return base;
}

inline QString dbPath(const QString &customPath = QString()) {
    // Callers can override the DB path explicitly for portable or admin-managed setups.
    if (!customPath.trimmed().isEmpty()) {
        return customPath.trimmed();
    }
    return QDir(dataDirPath()).filePath(AppGlobals::dbFileName());
}

inline QString imagesDirPath() {
    return QDir(dataDirPath()).filePath(AppGlobals::imagesFolderName());
}

inline QString legacyDataDirPath() {
    return QDir(QCoreApplication::applicationDirPath()).filePath(AppGlobals::dataFolderName());
}
}
