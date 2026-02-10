#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QString>

#include "globals.h"

namespace AppSettings {
inline QString dataRootPath() {
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
    const QString base = QDir(dataRootPath()).filePath(AppGlobals::dataFolderName());
    QDir dir(base);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    return base;
}

inline QString dbPath(const QString &customPath = QString()) {
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
