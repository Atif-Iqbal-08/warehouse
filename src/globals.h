#pragma once

#include <QString>

namespace AppGlobals {
// Centralized labels and folder names keep the UI, installer and runtime storage
// logic aligned without scattering string literals across the app.
inline QString appName() { return QStringLiteral("Warehouse SKU Generator"); }
inline QString appVersion() { return QStringLiteral("1.0.0"); }
inline QString organizationName() { return QStringLiteral("Skylark Drones"); }

inline QString dataFolderName() { return QStringLiteral("data"); }
inline QString dbFileName() { return QStringLiteral("sku.db"); }
inline QString imagesFolderName() { return QStringLiteral("images"); }
inline QString fallbackDataRoot() { return QStringLiteral(".warehouse_sku_generator"); }
inline QString installerBootstrapFileName() { return QStringLiteral("installer_bootstrap.ini"); }

inline QString noImageText() { return QStringLiteral("No Image"); }
inline QString noDateText() { return QStringLiteral("Date: -"); }

inline QString roleKeyView() { return QStringLiteral("view"); }
inline QString roleKeyAdd() { return QStringLiteral("add"); }
inline QString roleKeyFull() { return QStringLiteral("full"); }

inline QString roleNameView() { return QStringLiteral("View Only"); }
inline QString roleNameAdd() { return QStringLiteral("Add Data"); }
inline QString roleNameFull() { return QStringLiteral("Full Access"); }

inline QString baseRoleLabelView() { return QStringLiteral("View Only (Print)"); }
inline QString baseRoleLabelAdd() { return QStringLiteral("Add Data (No Delete)"); }
inline QString baseRoleLabelFull() { return QStringLiteral("Full Access"); }
}
