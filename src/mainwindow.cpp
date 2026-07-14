#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "app_settings.h"
#include "globals.h"
#include "qrcodegen.hpp"
#include <QAbstractItemView>
#include <QAbstractButton>
#include <QComboBox>
#include <QCoreApplication>
#include <QClipboard>
#include <QDate>
#include <QDateTime>
#include <QDateEdit>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QEvent>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QItemSelectionModel>
#include <QHeaderView>
#include <QFrame>
#include <QFont>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QGroupBox>
#include <QLineEdit>
#include <QIcon>
#include <QIntValidator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QAction>
#include <QCheckBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPushButton>
#include <QMessageBox>
#include <QTextBrowser>
#include <QTabWidget>
#include <QTime>
#include <QPrintDialog>
#include <QPrinter>
#include <QPageLayout>
#include <QPageSize>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QProcess>
#include <QProcessEnvironment>
#include <QColor>
#include <QPen>
#include <QSet>
#include <QStringConverter>
#include <QSignalBlocker>
#include <algorithm>
#include <QSpinBox>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTableView>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QTemporaryFile>
#include <QUuid>
#include <QUrl>
#include <QApplication>
#include <QGridLayout>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QScrollArea>
#include <QScreen>
#include <QVBoxLayout>
#include <QTimer>
#include <QMouseEvent>
#include <QPalette>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QSettings>
#include <QSharedPointer>
#include <QStatusBar>
#include <QStyle>
#include <exception>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#include <wincrypt.h>
#endif

namespace {
// Image-heavy detail dialogs reuse this view so users can zoom and pan
// product photos without losing the default fit-to-window behavior.
class ZoomableImageView final : public QGraphicsView {
public:
    explicit ZoomableImageView(QWidget *parent = nullptr)
        : QGraphicsView(parent)
        , m_scene(new QGraphicsScene(this)) {
        setScene(m_scene);
        setDragMode(QGraphicsView::ScrollHandDrag);
        setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
        setResizeAnchor(QGraphicsView::AnchorViewCenter);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);
    }

    void setImage(const QPixmap &pixmap) {
        m_scene->clear();
        m_pixmapItem = nullptr;
        m_zoomFactor = 1.0;
        m_userZoomed = false;

        if (pixmap.isNull()) {
            return;
        }

        m_pixmapItem = m_scene->addPixmap(pixmap);
        m_scene->setSceneRect(m_pixmapItem->boundingRect());
        fitCurrentImage();
    }

protected:
    void wheelEvent(QWheelEvent *event) override {
        if (!m_pixmapItem) {
            QGraphicsView::wheelEvent(event);
            return;
        }

        const int deltaY = event->angleDelta().y();
        if (deltaY == 0) {
            QGraphicsView::wheelEvent(event);
            return;
        }

        const qreal step = (deltaY > 0) ? 1.15 : (1.0 / 1.15);
        const qreal nextZoom = m_zoomFactor * step;
        if (nextZoom < 0.05 || nextZoom > 32.0) {
            event->accept();
            return;
        }

        scale(step, step);
        m_zoomFactor = nextZoom;
        m_userZoomed = true;
        event->accept();
    }

    void resizeEvent(QResizeEvent *event) override {
        QGraphicsView::resizeEvent(event);
        if (m_pixmapItem && !m_userZoomed) {
            fitCurrentImage();
        }
    }

private:
    void fitCurrentImage() {
        if (!m_pixmapItem) {
            return;
        }
        fitInView(m_pixmapItem->boundingRect(), Qt::KeepAspectRatio);
        m_zoomFactor = transform().m11();
        if (m_zoomFactor <= 0.0) {
            m_zoomFactor = 1.0;
        }
    }

    QGraphicsScene *m_scene = nullptr;
    QGraphicsPixmapItem *m_pixmapItem = nullptr;
    qreal m_zoomFactor = 1.0;
    bool m_userZoomed = false;
};

// The bundled seed files are simple CSV exports, so a focused local parser keeps
// the import path easy to inspect and avoids pulling in a heavier dependency.
QStringList parseCsvLine(const QString &line) {
    QStringList fields;
    QString field;
    bool inQuotes = false;

    for (int i = 0; i < line.size(); ++i) {
        QChar ch = line.at(i);
        if (inQuotes) {
            if (ch == '"') {
                if (i + 1 < line.size() && line.at(i + 1) == '"') {
                    field += '"';
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                field += ch;
            }
        } else {
            if (ch == '"') {
                inQuotes = true;
            } else if (ch == ',') {
                fields << field;
                field.clear();
            } else {
                field += ch;
            }
        }
    }
    fields << field;
    return fields;
}

QString normalizedText(const QString &text) {
    return text.trimmed();
}

constexpr int kRuleNameRole = Qt::UserRole + 1;

QString formatRuleDisplayText(const QString &digit, const QString &name) {
    const QString trimmedDigit = normalizedText(digit);
    const QString trimmedName = normalizedText(name);
    if (trimmedDigit.isEmpty()) {
        return trimmedName;
    }
    if (trimmedName.isEmpty()) {
        return trimmedDigit;
    }
    return QString("%1 %2").arg(trimmedDigit, trimmedName);
}

QString comboRuleName(const QComboBox *combo) {
    if (!combo) {
        return QString();
    }
    const int index = combo->currentIndex();
    if (index >= 0) {
        const QString storedName = combo->itemData(index, kRuleNameRole).toString().trimmed();
        if (!storedName.isEmpty()) {
            return storedName;
        }
    }
    return combo->currentText().trimmed();
}

int findRuleComboIndex(const QComboBox *combo, const QString &ruleName) {
    if (!combo) {
        return -1;
    }

    const QString target = normalizedText(ruleName);
    if (target.isEmpty()) {
        return -1;
    }

    for (int i = 0; i < combo->count(); ++i) {
        const QString storedName = combo->itemData(i, kRuleNameRole).toString().trimmed();
        if (!storedName.isEmpty() && QString::compare(storedName, target, Qt::CaseInsensitive) == 0) {
            return i;
        }
        if (QString::compare(combo->itemText(i).trimmed(), target, Qt::CaseInsensitive) == 0) {
            return i;
        }
    }

    return -1;
}

QString formatDimensionValue(double value) {
    QString text = QString::number(value, 'f', 3);
    while (text.contains('.') && (text.endsWith('0') || text.endsWith('.'))) {
        text.chop(1);
    }
    return text;
}

bool tryNormalizeDimensionsCm(const QString &input, QString *normalizedOut) {
    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty()) {
        if (normalizedOut) {
            *normalizedOut = QString();
        }
        return true;
    }

    static const QRegularExpression pattern(
        QStringLiteral(R"(^\s*(\d+(?:\.\d+)?)\s*(?:cm)?\s*[xX×]\s*(\d+(?:\.\d+)?)\s*(?:cm)?\s*[xX×]\s*(\d+(?:\.\d+)?)\s*(?:cm)?\s*$)"),
        QRegularExpression::CaseInsensitiveOption);

    const QRegularExpressionMatch match = pattern.match(trimmed);
    if (!match.hasMatch()) {
        return false;
    }

    bool ok1 = false;
    bool ok2 = false;
    bool ok3 = false;
    const double length = match.captured(1).toDouble(&ok1);
    const double width = match.captured(2).toDouble(&ok2);
    const double height = match.captured(3).toDouble(&ok3);
    if (!ok1 || !ok2 || !ok3 || length <= 0.0 || width <= 0.0 || height <= 0.0) {
        return false;
    }

    if (normalizedOut) {
        *normalizedOut = QString("%1 cm x %2 cm x %3 cm")
                             .arg(formatDimensionValue(length),
                                  formatDimensionValue(width),
                                  formatDimensionValue(height));
    }
    return true;
}

QString normalizeBarcodePrefix(const QString &prefix) {
    const QString normalized = prefix.trimmed().left(2).toUpper();
    if (normalized == "SD" || normalized == "SK" || normalized == "SM") {
        return normalized;
    }
    return "SK";
}

QString escapeCsvField(const QString &value) {
    QString field = value;
    const bool needsQuotes = field.contains('"') || field.contains(',') || field.contains('\n') || field.contains('\r');
    field.replace('"', "\"\"");
    if (needsQuotes) {
        field = "\"" + field + "\"";
    }
    return field;
}

QString loadDocumentationMarkdown(const QString &fileName) {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath("docs/" + fileName),
        QDir(appDir).filePath("../docs/" + fileName),
        QDir(appDir).filePath("../../docs/" + fileName),
        QDir::current().filePath("docs/" + fileName)
    };

    for (const QString &path : candidates) {
        QFile file(path);
        if (!file.exists()) {
            continue;
        }
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        QTextStream in(&file);
        in.setEncoding(QStringConverter::Utf8);
        return in.readAll();
    }

    QString message = QString("Documentation file not found: %1\n\nSearched in:\n").arg(fileName);
    for (const QString &path : candidates) {
        message += " - " + QDir::toNativeSeparators(path) + '\n';
    }
    return message;
}

QString commandLineValue(const QStringList &args, const QString &name) {
    for (int i = 1; i < args.size(); ++i) {
        const QString arg = args.at(i);
        if (arg == name && i + 1 < args.size()) {
            return args.at(i + 1).trimmed();
        }
        const QString prefix = name + "=";
        if (arg.startsWith(prefix, Qt::CaseInsensitive)) {
            return arg.mid(prefix.size()).trimmed();
        }
    }
    return QString();
}

bool hasCommandLineFlag(const QStringList &args, const QString &name) {
    for (int i = 1; i < args.size(); ++i) {
        if (args.at(i).compare(name, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

QString tableSelectionStyleSheet() {
    return QStringLiteral(R"(
QTableView {
    selection-background-color: #ff7c38;
    selection-color: #141414;
}
QTableView::item:selected {
    background: #ff7c38;
    color: #141414;
}
QTableView::item:selected:!active {
    background: #c85a1a;
    color: #eaeaea;
}
)");
}

QString highlightedSkuFieldStyle() {
    return QStringLiteral("QLineEdit { font-size: 15px; font-weight: 700; color: #ff7c38; border: 1px solid #383838; background-color: #202020; border-radius: 7px; padding: 5px 10px; }");
}

QString versionLabelStyle() {
    return QStringLiteral("QLabel { color: #6e6e6e; padding-right: 6px; font-weight: 500; font-size: 9pt; }");
}

// Clamps a dialog's minimum and preferred size to the available screen so that,
// on small/high-DPI-scaled displays (e.g. 14" laptops at 150% scaling), buttons
// and controls near the bottom/edges of the dialog never end up pushed off-screen.
void sizeDialogToScreen(QWidget *dialog, QWidget *anchor, int preferredWidth, int preferredHeight,
                        int minWidth, int minHeight) {
    if (!dialog) {
        return;
    }
    const QScreen *screen = (anchor && anchor->screen()) ? anchor->screen() : QGuiApplication::primaryScreen();
    const QRect avail = screen ? screen->availableGeometry() : QRect(0, 0, preferredWidth, preferredHeight);
    const int maxWidth = qMax(320, static_cast<int>(avail.width() * 0.92));
    const int maxHeight = qMax(240, static_cast<int>(avail.height() * 0.88));
    dialog->setMinimumSize(qMin(minWidth, maxWidth), qMin(minHeight, maxHeight));
    dialog->resize(qMin(preferredWidth, maxWidth), qMin(preferredHeight, maxHeight));
}

QString g_runLogUsername = QStringLiteral("anonymous");
QString g_runLogUserId = QStringLiteral("-");
QString g_runLogRole = QStringLiteral("unknown");

const QString kEmbeddedDeveloperUsername = QStringLiteral("Admin");
const QString kEmbeddedDeveloperPassword = QStringLiteral("Skylark@321");
const QString kEmbeddedDeveloperRole = QStringLiteral("master_admin");
const QString kEmbeddedDeveloperUserId = QStringLiteral("DEV-ADMIN");
const QString kEmbeddedDeveloperFullName = QStringLiteral("Admin");
const QString kEmbeddedDeveloperEmail = QStringLiteral("admin@localhost");

QString normalizedLogIdentity(const QString &value, const QString &fallback) {
    const QString trimmed = value.trimmed();
    return trimmed.isEmpty() ? fallback : trimmed;
}

void setRunLogIdentity(const QString &username, const QString &userId, const QString &role) {
    g_runLogUsername = normalizedLogIdentity(username, QStringLiteral("anonymous"));
    g_runLogUserId = normalizedLogIdentity(userId, QStringLiteral("-"));
    g_runLogRole = normalizedLogIdentity(role, QStringLiteral("unknown"));
}

QString runLogIdentityPrefix() {
    return QStringLiteral("user=%1 user_id=%2 role=%3")
        .arg(g_runLogUsername, g_runLogUserId, g_runLogRole);
}

QString quoteWindowsArgument(const QString &arg) {
    QString escaped = arg;
    escaped.replace('"', "\\\"");
    return "\"" + escaped + "\"";
}

QString configuredRunLogPath() {
    QString localAppData = QProcessEnvironment::systemEnvironment().value("LOCALAPPDATA").trimmed();
    if (localAppData.isEmpty()) {
        localAppData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation).trimmed();
    }
    if (localAppData.isEmpty()) {
        localAppData = AppSettings::dataRootPath();
    }

    const QString logFolder = QDir(localAppData).filePath("Warehouse SKU Logs");

    QDir dir(logFolder);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    const QString monthlyFile = QStringLiteral("app_run_%1.log")
                                    .arg(QDate::currentDate().toString("yyyy-MM"));
    return dir.filePath(monthlyFile);
}

void appendRunLog(const QString &message);

QString installerBootstrapPath() {
    QString programData = QProcessEnvironment::systemEnvironment().value("PROGRAMDATA").trimmed();
    if (programData.isEmpty()) {
        programData = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation).trimmed();
    }
    if (programData.isEmpty()) {
        return QString();
    }
    return QDir(QDir(programData).filePath(AppGlobals::appName())).filePath(AppGlobals::installerBootstrapFileName());
}

void importInstallerBootstrapSettingsIfNeeded() {
    const QString bootstrapPath = installerBootstrapPath();
    if (bootstrapPath.isEmpty() || !QFileInfo::exists(bootstrapPath)) {
        return;
    }

    QSettings bootstrap(bootstrapPath, QSettings::IniFormat);
    const QString dbPath = bootstrap.value("bootstrap/db_path").toString().trimmed();
    const QString backupPath = bootstrap.value("bootstrap/backup_path").toString().trimmed();
    const QString logConsent = bootstrap.value("bootstrap/log_consent").toString().trimmed().toLower();

    if (dbPath.isEmpty() && backupPath.isEmpty() && logConsent.isEmpty()) {
        return;
    }

    const QString fingerprint = QString::fromLatin1(
        QCryptographicHash::hash(QStringList{dbPath, backupPath, logConsent}.join('\n').toUtf8(),
                                 QCryptographicHash::Sha256)
            .toHex());

    QSettings settings;
    const QString appliedFingerprint = settings.value("installer_bootstrap/fingerprint").toString().trimmed();
    if (!fingerprint.isEmpty() && appliedFingerprint == fingerprint) {
        return;
    }

    if (!dbPath.isEmpty()) {
        settings.setValue("db/path", dbPath);
    }
    if (!backupPath.isEmpty()) {
        settings.setValue("backup/path", backupPath);
    }
    if (!logConsent.isEmpty()) {
        settings.setValue("logging/installer_consent", logConsent == "1" || logConsent == "true" || logConsent == "yes");
    }

    settings.setValue("installer_bootstrap/fingerprint", fingerprint);
    settings.sync();

    appendRunLog(QString("Imported installer bootstrap settings from %1")
                     .arg(QDir::toNativeSeparators(bootstrapPath)));
}

void appendRunLog(const QString &message) {
    const QString trimmed = message.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    const QString logPath = configuredRunLogPath();
    QFile file(logPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << QDateTime::currentDateTime().toString(Qt::ISODate) << " | ";
    if (!trimmed.startsWith("user=", Qt::CaseInsensitive)) {
        out << runLogIdentityPrefix() << " | ";
    }
    out << trimmed << '\n';
}
} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setupUi();
    importInstallerBootstrapSettingsIfNeeded();
    m_currentRoleKey = AppGlobals::roleKeyView();
    m_currentBaseRole = UserRole::ViewOnly;
    m_access = accessPolicyForBaseRole(UserRole::ViewOnly);
    applyAccessControl(m_currentRoleKey);
    syncRunLogIdentity();
    appendRunLog("MainWindow setupUi complete");

    if (!selectDatabaseOnStartup()) {
        if (m_relaunchingElevated) {
            setStatus("Restarting with administrator privileges...", true);
            appendRunLog("Database startup relaunch requested with UAC elevation");
        } else {
            setStatus("Database selection canceled.", false);
            appendRunLog("Database selection canceled");
        }
        QTimer::singleShot(0, this, &QWidget::close);
        return;
    }

    if (!m_db.isOpen()) {
        if (!m_customDbPath.trimmed().isEmpty()) {
            if (!initDb()) {
                setStatus("Database initialization failed.", false);
                appendRunLog("initDb failed");
                return;
            }
        } else {
            setStatus("Database setup skipped for this session. Use File -> Load DB... when ready.", false);
            appendRunLog("Database setup skipped for this session");
            updateNoDbBanner();
            return;
        }
    }
    appendRunLog("initDb ok");
    updateNoDbBanner();

    m_currentUsername.clear();
    m_currentUserId.clear();
    m_currentUserFullName.clear();
    m_currentRoleKey = AppGlobals::roleKeyView();
    m_currentBaseRole = UserRole::ViewOnly;
    m_access = accessPolicyForBaseRole(UserRole::ViewOnly);
    syncRunLogIdentity();

    if (!promptLogin()) {
        setStatus("Login is required.", false);
        QTimer::singleShot(0, this, &QWidget::close);
        return;
    }

    appendRunLog("loadCategories start");
    loadCategories();
    appendRunLog("onCategoryChanged start");
    onCategoryChanged();
    appendRunLog("searchRecords start");
    searchRecords();
    appendRunLog("loadSkuList start");
    loadSkuList();
    appendRunLog("loadHistorySkuList start");
    loadHistorySkuList();
    appendRunLog("updateDashboardMetrics start");
    updateDashboardMetrics();
    scheduleAutomatedBackups();
    appendRunLog("MainWindow initial data loaded");
}

MainWindow::~MainWindow() {
    if (m_db.isOpen()) {
        m_db.close();
    }
    appendRunLog("MainWindow destroyed");
    delete ui;
}

MainWindow::AccessPolicy MainWindow::accessPolicyForBaseRole(UserRole role) const {
    AccessPolicy policy;
    policy.canPrint = true;

    switch (role) {
    case UserRole::ViewOnly:
        policy.canAdd = false;
        policy.canEdit = false;
        policy.canDelete = false;
        policy.canSerialEdit = false;
        policy.canSerialDelete = false;
        policy.canManageUsers = false;
        policy.canBackupRestore = false;
        policy.canExport = true;
        break;
    case UserRole::AddOnly:
        policy.canAdd = true;
        policy.canEdit = true;
        policy.canDelete = false;
        policy.canSerialEdit = false;
        policy.canSerialDelete = false;
        policy.canManageUsers = false;
        policy.canBackupRestore = false;
        policy.canExport = true;
        break;
    case UserRole::FullAccess:
        policy.canAdd = true;
        policy.canEdit = true;
        policy.canDelete = true;
        policy.canSerialEdit = true;
        policy.canSerialDelete = true;
        policy.canManageUsers = true;
        policy.canBackupRestore = true;
        policy.canExport = true;
        break;
    }

    return policy;
}

void MainWindow::applyAccessControl(const QString &roleKey) {
    UserRole baseRole = UserRole::ViewOnly;
    if (!fetchRoleInfo(roleKey, &baseRole, nullptr)) {
        baseRole = baseRoleFromStorage(roleKey);
    }

    m_currentRoleKey = roleKey;
    m_currentBaseRole = baseRole;
    m_access = rolePolicyFor(roleKey);
    applyUserOverrides(m_currentUsername, &m_access);

    if (m_saveButton) {
        m_saveButton->setEnabled(m_access.canAdd);
    }
    if (m_updateButton) {
        m_updateButton->setEnabled(m_access.canEdit);
    }
    if (m_deleteSkuButton) {
        m_deleteSkuButton->setEnabled(m_access.canDelete);
    }
    if (m_generateBarcodeButton) {
        m_generateBarcodeButton->setEnabled(m_access.canAdd);
    }
    if (m_printBarcodeButton) {
        m_printBarcodeButton->setEnabled(m_access.canPrint);
    }
    if (m_deleteBarcodeButton) {
        m_deleteBarcodeButton->setEnabled(m_access.canSerialDelete);
    }
    if (m_historyEditButton) {
        m_historyEditButton->setEnabled(m_access.canSerialEdit);
    }
    if (m_historyDeleteButton) {
        m_historyDeleteButton->setEnabled(m_access.canSerialDelete);
    }
    if (m_actionManageUsers) {
        m_actionManageUsers->setEnabled(m_access.canManageUsers);
    }
    if (m_actionSettings) {
        m_actionSettings->setEnabled(m_access.canManageUsers);
    }
    if (m_actionSwitchUser) {
        m_actionSwitchUser->setEnabled(true);
    }
    if (m_actionExportDb) {
        m_actionExportDb->setEnabled(m_access.canBackupRestore);
    }
    if (m_actionRestoreBackup) {
        m_actionRestoreBackup->setEnabled(m_access.canBackupRestore);
    }
    if (m_actionSaveDb) {
        m_actionSaveDb->setEnabled(m_access.canBackupRestore);
    }
    if (m_actionExportSkuCsv) {
        m_actionExportSkuCsv->setEnabled(m_access.canExport);
    }
    if (m_actionExportBarcodeSummaryCsv) {
        m_actionExportBarcodeSummaryCsv->setEnabled(m_access.canExport);
    }
    if (m_backupDbButton) {
        m_backupDbButton->setEnabled(m_access.canBackupRestore);
    }

    syncRunLogIdentity();
}

bool MainWindow::requireAccess(bool allowed, const QString &message) {
    if (allowed) {
        return true;
    }

    const QString userMessage = message.isEmpty() ? QStringLiteral("Access denied.") : message;
    QMessageBox::warning(this, "Access Denied", userMessage);
    logAction("ACCESS_DENIED",
              "permission",
              QString(),
              QString(),
              userMessage,
              "Security",
              "PERMISSION",
              false,
              userMessage);
    return false;
}

QString MainWindow::normalizeUsername(const QString &username) const {
    return normalizedText(username).toLower();
}

QString MainWindow::baseRoleToStorage(UserRole role) const {
    switch (role) {
    case UserRole::ViewOnly:
        return AppGlobals::roleKeyView();
    case UserRole::AddOnly:
        return AppGlobals::roleKeyAdd();
    case UserRole::FullAccess:
        return AppGlobals::roleKeyFull();
    }
    return AppGlobals::roleKeyView();
}

MainWindow::UserRole MainWindow::baseRoleFromStorage(const QString &role) const {
    const QString value = role.trimmed().toLower();
    if (value == AppGlobals::roleKeyView() || value == "view_only" || value == "viewonly" || value == "viewer") {
        return UserRole::ViewOnly;
    }
    if (value == AppGlobals::roleKeyAdd() || value == "add_only" || value == "addonly" || value == "operator") {
        return UserRole::AddOnly;
    }
    if (value == AppGlobals::roleKeyFull() || value == "full_access" || value == "fullaccess" || value == "admin" || value == "master_admin" || value == "supervisor") {
        return UserRole::FullAccess;
    }
    return UserRole::ViewOnly;
}

QString MainWindow::baseRoleToDisplay(UserRole role) const {
    switch (role) {
    case UserRole::ViewOnly:
        return AppGlobals::baseRoleLabelView();
    case UserRole::AddOnly:
        return AppGlobals::baseRoleLabelAdd();
    case UserRole::FullAccess:
        return AppGlobals::baseRoleLabelFull();
    }
    return AppGlobals::baseRoleLabelView();
}

QString MainWindow::roleDisplayName(const QString &roleKey) const {
    QString displayName;
    UserRole baseRole = UserRole::ViewOnly;
    if (fetchRoleInfo(roleKey, &baseRole, &displayName) && !displayName.isEmpty()) {
        return displayName;
    }
    return baseRoleToDisplay(baseRoleFromStorage(roleKey));
}

QString MainWindow::roleKeyFromName(const QString &name) const {
    QString key = normalizedText(name).toLower();
    key.replace(' ', '_');
    key.remove(QRegularExpression("[^a-z0-9_\\-]"));
    return key;
}

QString MainWindow::generateSalt() const {
    const qulonglong partA = QRandomGenerator::global()->generate64();
    const qulonglong partB = QRandomGenerator::global()->generate64();
    return QString("%1%2")
        .arg(partA, 16, 16, QLatin1Char('0'))
        .arg(partB, 16, 16, QLatin1Char('0'));
}

QString MainWindow::hashPasswordWithSalt(const QString &password, const QString &salt) const {
    const QByteArray data = (salt + password).toUtf8();
    const QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256);
    return QString::fromLatin1(hash.toHex());
}

bool MainWindow::ensureDefaultRoles() {
    QSqlQuery check(m_db);
    if (!check.exec("SELECT COUNT(*) FROM app_roles")) {
        return false;
    }

    const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
    QSqlQuery insert(m_db);
    insert.prepare(
        "INSERT OR IGNORE INTO app_roles ("
        "role_key, display_name, base_role, "
        "can_add, can_edit, can_delete, can_serial_edit, can_serial_delete, "
        "can_print, can_manage_users, can_backup_restore, can_export, created_at"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");

    struct RoleSeed {
        QString key;
        QString name;
        UserRole baseRole;
        AccessPolicy policy;
    };

    const AccessPolicy viewPolicy = accessPolicyForBaseRole(UserRole::ViewOnly);
    const AccessPolicy addPolicy = accessPolicyForBaseRole(UserRole::AddOnly);
    const AccessPolicy fullPolicy = accessPolicyForBaseRole(UserRole::FullAccess);

    AccessPolicy supervisorPolicy = fullPolicy;
    supervisorPolicy.canManageUsers = false;
    supervisorPolicy.canBackupRestore = false;

    AccessPolicy operatorPolicy = addPolicy;
    operatorPolicy.canEdit = false;
    operatorPolicy.canSerialEdit = false;
    operatorPolicy.canSerialDelete = false;
    operatorPolicy.canDelete = false;

    const RoleSeed seeds[] = {
        { AppGlobals::roleKeyView(), AppGlobals::roleNameView(), UserRole::ViewOnly, viewPolicy },
        { AppGlobals::roleKeyAdd(), AppGlobals::roleNameAdd(), UserRole::AddOnly, addPolicy },
        { AppGlobals::roleKeyFull(), AppGlobals::roleNameFull(), UserRole::FullAccess, fullPolicy },
        { QStringLiteral("master_admin"), QStringLiteral("Master Admin"), UserRole::FullAccess, fullPolicy },
        { QStringLiteral("supervisor"), QStringLiteral("Supervisor"), UserRole::FullAccess, supervisorPolicy },
        { QStringLiteral("operator"), QStringLiteral("Operator"), UserRole::AddOnly, operatorPolicy },
        { QStringLiteral("viewer"), QStringLiteral("Viewer"), UserRole::ViewOnly, viewPolicy }
    };

    for (const auto &seed : seeds) {
        insert.addBindValue(seed.key);
        insert.addBindValue(seed.name);
        insert.addBindValue(baseRoleToStorage(seed.baseRole));
        insert.addBindValue(seed.policy.canAdd ? 1 : 0);
        insert.addBindValue(seed.policy.canEdit ? 1 : 0);
        insert.addBindValue(seed.policy.canDelete ? 1 : 0);
        insert.addBindValue(seed.policy.canSerialEdit ? 1 : 0);
        insert.addBindValue(seed.policy.canSerialDelete ? 1 : 0);
        insert.addBindValue(seed.policy.canPrint ? 1 : 0);
        insert.addBindValue(seed.policy.canManageUsers ? 1 : 0);
        insert.addBindValue(seed.policy.canBackupRestore ? 1 : 0);
        insert.addBindValue(seed.policy.canExport ? 1 : 0);
        insert.addBindValue(now);
        if (!insert.exec()) {
            return false;
        }
        insert.finish();

        QSqlQuery patchDefaults(m_db);
        patchDefaults.prepare(
            "UPDATE app_roles SET "
            "can_add = COALESCE(can_add, ?), "
            "can_edit = COALESCE(can_edit, ?), "
            "can_delete = COALESCE(can_delete, ?), "
            "can_serial_edit = COALESCE(can_serial_edit, ?), "
            "can_serial_delete = COALESCE(can_serial_delete, ?), "
            "can_print = COALESCE(can_print, ?), "
            "can_manage_users = COALESCE(can_manage_users, ?), "
            "can_backup_restore = COALESCE(can_backup_restore, ?), "
            "can_export = COALESCE(can_export, ?) "
            "WHERE role_key = ?");
        patchDefaults.addBindValue(seed.policy.canAdd ? 1 : 0);
        patchDefaults.addBindValue(seed.policy.canEdit ? 1 : 0);
        patchDefaults.addBindValue(seed.policy.canDelete ? 1 : 0);
        patchDefaults.addBindValue(seed.policy.canSerialEdit ? 1 : 0);
        patchDefaults.addBindValue(seed.policy.canSerialDelete ? 1 : 0);
        patchDefaults.addBindValue(seed.policy.canPrint ? 1 : 0);
        patchDefaults.addBindValue(seed.policy.canManageUsers ? 1 : 0);
        patchDefaults.addBindValue(seed.policy.canBackupRestore ? 1 : 0);
        patchDefaults.addBindValue(seed.policy.canExport ? 1 : 0);
        patchDefaults.addBindValue(seed.key);
        if (!patchDefaults.exec()) {
            return false;
        }
    }

    return true;
}

bool MainWindow::fetchRoleInfo(const QString &roleKey, UserRole *baseRole, QString *displayName) const {
    const QString key = normalizedText(roleKey);
    if (key.isEmpty()) {
        return false;
    }

    QSqlQuery q(m_db);
    q.prepare("SELECT display_name, base_role FROM app_roles WHERE role_key = ?");
    q.addBindValue(key);
    if (!q.exec() || !q.next()) {
        return false;
    }

    if (displayName) {
        *displayName = q.value(0).toString();
    }
    if (baseRole) {
        *baseRole = baseRoleFromStorage(q.value(1).toString());
    }
    return true;
}

void MainWindow::loadRolesIntoCombo(QComboBox *combo) {
    if (!combo) {
        return;
    }

    combo->clear();
    if (!ensureDefaultRoles()) {
        combo->addItem(baseRoleToDisplay(UserRole::ViewOnly), baseRoleToStorage(UserRole::ViewOnly));
        combo->addItem(baseRoleToDisplay(UserRole::AddOnly), baseRoleToStorage(UserRole::AddOnly));
        combo->addItem(baseRoleToDisplay(UserRole::FullAccess), baseRoleToStorage(UserRole::FullAccess));
        return;
    }

    QSqlQuery q(m_db);
    q.prepare("SELECT role_key, display_name FROM app_roles ORDER BY display_name");
    if (!q.exec()) {
        combo->addItem(baseRoleToDisplay(UserRole::ViewOnly), baseRoleToStorage(UserRole::ViewOnly));
        combo->addItem(baseRoleToDisplay(UserRole::AddOnly), baseRoleToStorage(UserRole::AddOnly));
        combo->addItem(baseRoleToDisplay(UserRole::FullAccess), baseRoleToStorage(UserRole::FullAccess));
        return;
    }

    while (q.next()) {
        combo->addItem(q.value(1).toString(), q.value(0).toString());
    }
}

bool MainWindow::createUserAccount(const QString &username,
                                   const QString &password,
                                   const QString &roleKey,
                                   const QString &email,
                                   const QString &userId,
                                   const QString &fullName,
                                   QString *errorMessage) {
    const QString normalizedUser = normalizeUsername(username);
    if (normalizedUser.isEmpty()) {
        if (errorMessage) {
            *errorMessage = "Username is required.";
        }
        return false;
    }
    if (password.isEmpty()) {
        if (errorMessage) {
            *errorMessage = "Password is required.";
        }
        return false;
    }
    QString passwordReason;
    if (!isStrongPassword(password, &passwordReason)) {
        if (errorMessage) {
            *errorMessage = passwordReason;
        }
        return false;
    }
    if (normalizedText(fullName).isEmpty()) {
        if (errorMessage) {
            *errorMessage = "Name is required.";
        }
        return false;
    }
    if (normalizedText(userId).isEmpty()) {
        if (errorMessage) {
            *errorMessage = "User ID is required.";
        }
        return false;
    }
    if (normalizedText(email).isEmpty()) {
        if (errorMessage) {
            *errorMessage = "Email is required.";
        }
        return false;
    }
    const QString trimmedRoleKey = normalizedText(roleKey);
    if (trimmedRoleKey.isEmpty()) {
        if (errorMessage) {
            *errorMessage = "Role is required.";
        }
        return false;
    }
    ensureDefaultRoles();
    if (!fetchRoleInfo(trimmedRoleKey, nullptr, nullptr)) {
        if (errorMessage) {
            *errorMessage = "Selected role does not exist.";
        }
        return false;
    }

    QSqlQuery check(m_db);
    check.prepare("SELECT COUNT(*) FROM app_users WHERE username = ?");
    check.addBindValue(normalizedUser);
    if (!check.exec() || !check.next()) {
        if (errorMessage) {
            *errorMessage = "Unable to validate username.";
        }
        return false;
    }
    if (check.value(0).toInt() > 0) {
        if (errorMessage) {
            *errorMessage = "Username already exists.";
        }
        return false;
    }

    const QString salt = generateSalt();
    const QString hash = hashPasswordWithSalt(password, salt);

    QSqlQuery insert(m_db);
    insert.prepare(
        "INSERT INTO app_users (username, user_id, full_name, email, password_hash, password_salt, role, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    insert.addBindValue(normalizedUser);
    insert.addBindValue(normalizedText(userId));
    insert.addBindValue(normalizedText(fullName));
    insert.addBindValue(normalizedText(email));
    insert.addBindValue(hash);
    insert.addBindValue(salt);
    insert.addBindValue(trimmedRoleKey);
    const QString createdAt = QDateTime::currentDateTime().toString(Qt::ISODate);
    insert.addBindValue(createdAt);

    if (!insert.exec()) {
        if (errorMessage) {
            *errorMessage = "Failed to create user account.";
        }
        return false;
    }

    return true;
}

bool MainWindow::verifyUserCredentials(const QString &username,
                                       const QString &password,
                                       QString *roleKeyOut) {
    const QString normalizedUser = normalizeUsername(username);
    if (normalizedUser.isEmpty() || password.isEmpty()) {
        return false;
    }

    QSqlQuery q(m_db);
    q.prepare("SELECT password_hash, password_salt, role FROM app_users WHERE username = ?");
    q.addBindValue(normalizedUser);
    if (!q.exec() || !q.next()) {
        return false;
    }

    const QString storedHash = q.value(0).toString();
    const QString salt = q.value(1).toString();
    const QString storedRole = q.value(2).toString();
    const QString inputHash = hashPasswordWithSalt(password, salt);

    if (storedHash != inputHash) {
        return false;
    }

    if (roleKeyOut) {
        *roleKeyOut = storedRole.trimmed();
    }
    return true;
}

bool MainWindow::promptCreateUser(const QString &forcedRoleKey, bool allowCancel) {
    QDialog dialog(this);
    dialog.setWindowTitle("Create User Account");
    dialog.setStyleSheet(qApp ? qApp->styleSheet() : m_darkStyleSheet);
    QFormLayout *formLayout = new QFormLayout(&dialog);

    QLineEdit *nameField = new QLineEdit(&dialog);
    QLineEdit *userIdField = new QLineEdit(&dialog);
    QLineEdit *emailField = new QLineEdit(&dialog);
    QLineEdit *usernameField = new QLineEdit(&dialog);
    QLineEdit *passwordField = new QLineEdit(&dialog);
    QLineEdit *confirmField = new QLineEdit(&dialog);
    passwordField->setEchoMode(QLineEdit::Password);
    confirmField->setEchoMode(QLineEdit::Password);

    QLabel *roleLabel = new QLabel(roleDisplayName(forcedRoleKey), &dialog);
    formLayout->addRow("Name", nameField);
    formLayout->addRow("User ID", userIdField);
    formLayout->addRow("Email", emailField);
    formLayout->addRow("Username", usernameField);
    formLayout->addRow("Password", passwordField);
    formLayout->addRow("Confirm Password", confirmField);
    formLayout->addRow("Role", roleLabel);

    QLabel *errorLabel = new QLabel(&dialog);
    errorLabel->setStyleSheet("color: #c62828;");
    formLayout->addRow(errorLabel);

    QDialogButtonBox *buttons = new QDialogButtonBox(&dialog);
    QPushButton *createButton = buttons->addButton("Create", QDialogButtonBox::AcceptRole);
    QPushButton *cancelButton = nullptr;
    if (allowCancel) {
        cancelButton = buttons->addButton(QDialogButtonBox::Cancel);
    }
    formLayout->addRow(buttons);

    connect(createButton, &QPushButton::clicked, &dialog, [&]() {
        const QString fullName = normalizedText(nameField->text());
        const QString userId = normalizedText(userIdField->text());
        const QString email = normalizedText(emailField->text());
        const QString username = normalizeUsername(usernameField->text());
        const QString password = passwordField->text();
        const QString confirm = confirmField->text();
        if (fullName.isEmpty() || userId.isEmpty() || email.isEmpty() || username.isEmpty() || password.isEmpty()) {
            errorLabel->setText("All fields are required.");
            return;
        }
        if (password != confirm) {
            errorLabel->setText("Passwords do not match.");
            return;
        }
        QString passwordReason;
        if (!isStrongPassword(password, &passwordReason)) {
            errorLabel->setText(passwordReason);
            return;
        }
        QString error;
        if (!createUserAccount(username, password, forcedRoleKey, email, userId, fullName, &error)) {
            errorLabel->setText(error);
            return;
        }
        dialog.accept();
    });

    if (cancelButton) {
        connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    }

    return dialog.exec() == QDialog::Accepted;
}

bool MainWindow::ensureInitialAdmin() {
    if (!ensureDefaultRoles()) {
        return false;
    }

    const QString username = normalizeUsername(kEmbeddedDeveloperUsername);
    QSqlQuery q(m_db);
    q.prepare("SELECT COUNT(*) FROM app_users WHERE username = ?");
    q.addBindValue(username);
    if (!q.exec() || !q.next()) {
        appendRunLog(QString("Embedded developer account check failed for %1")
                         .arg(kEmbeddedDeveloperUsername));
        return false;
    }

    const QString salt = generateSalt();
    const QString hash = hashPasswordWithSalt(kEmbeddedDeveloperPassword, salt);

    if (q.value(0).toInt() > 0) {
        QSqlQuery update(m_db);
        update.prepare(
            "UPDATE app_users SET user_id = ?, full_name = ?, email = ?, password_hash = ?, password_salt = ?, role = ? "
            "WHERE username = ?");
        update.addBindValue(kEmbeddedDeveloperUserId);
        update.addBindValue(kEmbeddedDeveloperFullName);
        update.addBindValue(kEmbeddedDeveloperEmail);
        update.addBindValue(hash);
        update.addBindValue(salt);
        update.addBindValue(kEmbeddedDeveloperRole);
        update.addBindValue(username);
        if (!update.exec()) {
            appendRunLog(QString("Embedded developer account sync failed for %1: %2")
                             .arg(kEmbeddedDeveloperUsername, update.lastError().text()));
            return false;
        }

        logAction("EMBEDDED_ACCOUNT_SYNC",
                  kEmbeddedDeveloperUsername,
                  QString(),
                  QString("{\"role\":\"%1\"}").arg(kEmbeddedDeveloperRole),
                  "Embedded developer credentials synced on startup",
                  "Security",
                  "PERMISSION_CHANGE",
                  true,
                  QString(),
                  kEmbeddedDeveloperUsername);

        appendRunLog(QString("Embedded developer account synced for %1")
                         .arg(kEmbeddedDeveloperUsername));
        return true;
    }

    QString error;
    const bool created = createUserAccount(kEmbeddedDeveloperUsername,
                                           kEmbeddedDeveloperPassword,
                                           kEmbeddedDeveloperRole,
                                           kEmbeddedDeveloperEmail,
                                           kEmbeddedDeveloperUserId,
                                           kEmbeddedDeveloperFullName,
                                           &error);
    if (!created) {
        appendRunLog(QString("Embedded developer account bootstrap failed for %1: %2")
                         .arg(kEmbeddedDeveloperUsername, error));
        return false;
    }

    logAction("BOOTSTRAP_USER_CREATE",
              kEmbeddedDeveloperUsername,
              QString(),
              QString("{\"role\":\"%1\"}").arg(kEmbeddedDeveloperRole),
              "Created from embedded developer credentials",
              "Security",
              "PERMISSION_CHANGE",
              true,
              QString(),
              kEmbeddedDeveloperUsername);

    appendRunLog(QString("Embedded developer account created for %1")
                     .arg(kEmbeddedDeveloperUsername));
    return true;
}

bool MainWindow::promptLogin() {
    QDialog dialog(this);
    dialog.setWindowTitle("User Login");
    dialog.setStyleSheet(qApp ? qApp->styleSheet() : m_darkStyleSheet);
    QFormLayout *formLayout = new QFormLayout(&dialog);

    QLineEdit *usernameField = new QLineEdit(&dialog);
    QLineEdit *passwordField = new QLineEdit(&dialog);
    passwordField->setEchoMode(QLineEdit::Password);

    formLayout->addRow("Username", usernameField);
    formLayout->addRow("Password", passwordField);

    QLabel *errorLabel = new QLabel(&dialog);
    errorLabel->setStyleSheet("color: #c62828;");
    formLayout->addRow(errorLabel);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText("Login");
    formLayout->addRow(buttons);

    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        const QString username = normalizeUsername(usernameField->text());
        const QString password = passwordField->text();
        if (username.isEmpty() || password.isEmpty()) {
            errorLabel->setText("Username and password are required.");
            appendRunLog("Login attempt missing credentials");
            return;
        }
        QString roleKey;
        if (!verifyUserCredentials(username, password, &roleKey)) {
            errorLabel->setText("Invalid username or password.");
            passwordField->clear();
            logAction("LOGIN_FAILED",
                      username,
                      QString(),
                      QString(),
                      "Invalid credentials",
                      "Security",
                      "LOGIN",
                      false,
                      "Invalid credentials",
                      username);
            appendRunLog(QString("Login failed for %1").arg(username));
            return;
        }

        m_currentUsername = username;
        m_currentUserId.clear();
        m_currentUserFullName.clear();
        QSqlQuery userInfo(m_db);
        userInfo.prepare("SELECT user_id, full_name FROM app_users WHERE username = ?");
        userInfo.addBindValue(username);
        if (userInfo.exec() && userInfo.next()) {
            m_currentUserId = userInfo.value(0).toString();
            m_currentUserFullName = userInfo.value(1).toString();
        }
        applyAccessControl(roleKey);
        syncRunLogIdentity();
        updateWindowTitleWithUser();
        setStatus(QString("Logged in as %1 (%2).").arg(m_currentUsername, roleDisplayName(m_currentRoleKey)), true);
        logAction("LOGIN_SUCCESS",
                  m_currentUsername,
                  QString(),
                  QString(),
                  QString(),
                  "Security",
                  "LOGIN",
                  true,
                  QString(),
                  m_currentUsername);
        appendRunLog(QString("Login success for %1").arg(m_currentUsername));
        QTimer::singleShot(0, this, [this]() {
            if (!isVisible()) {
                show();
            }
            showNormal();
            raise();
            activateWindow();
        });
        dialog.accept();
    });

    connect(buttons, &QDialogButtonBox::rejected, &dialog, [&]() {
        appendRunLog("Login dialog rejected");
        dialog.reject();
    });

    return dialog.exec() == QDialog::Accepted;
}

void MainWindow::updateWindowTitleWithUser() {
    const QString baseTitle = QCoreApplication::applicationName();
    if (m_currentUsername.isEmpty()) {
        setWindowTitle(baseTitle);
        return;
    }
    setWindowTitle(QString("%1 - %2 (%3)")
                       .arg(baseTitle, m_currentUsername, roleDisplayName(m_currentRoleKey)));
}

void MainWindow::loadUsersIntoTable(QTableWidget *table) {
    if (!table) {
        return;
    }

    table->setRowCount(0);

    QSqlQuery q(m_db);
    q.prepare(
        "SELECT username, full_name, user_id, email, role, created_at, "
        "perm_add, perm_edit, perm_delete, perm_serial_edit, perm_serial_delete, "
        "perm_print, perm_manage_users, perm_backup_restore, perm_export "
        "FROM app_users ORDER BY username");
    if (!q.exec()) {
        return;
    }

    int row = 0;
    while (q.next()) {
        table->insertRow(row);
        const QString username = q.value(0).toString();
        const QString fullName = q.value(1).toString();
        const QString userId = q.value(2).toString();
        const QString email = q.value(3).toString();
        const QString roleValue = q.value(4).toString();
        const QString createdAt = q.value(5).toString();
        const bool hasOverride =
            !q.isNull(6) || !q.isNull(7) || !q.isNull(8) || !q.isNull(9) || !q.isNull(10) ||
            !q.isNull(11) || !q.isNull(12) || !q.isNull(13) || !q.isNull(14);
        const QString overrideLabel = hasOverride ? "Custom Override" : "Role Default";

        table->setItem(row, 0, new QTableWidgetItem(username));
        table->setItem(row, 1, new QTableWidgetItem(fullName));
        table->setItem(row, 2, new QTableWidgetItem(userId));
        table->setItem(row, 3, new QTableWidgetItem(email));
        table->setItem(row, 4, new QTableWidgetItem(roleDisplayName(roleValue)));
        if (table->columnCount() >= 7) {
            table->setItem(row, 5, new QTableWidgetItem(overrideLabel));
            table->setItem(row, 6, new QTableWidgetItem(createdAt));
        } else {
            table->setItem(row, 5, new QTableWidgetItem(createdAt));
        }
        ++row;
    }

    table->resizeColumnsToContents();
    table->horizontalHeader()->setStretchLastSection(true);
}

void MainWindow::showSettingsDialog() {
    if (!requireAccess(m_access.canManageUsers, "You don't have permission to manage users.")) {
        return;
    }

    ensureDefaultRoles();

    QDialog dialog(this);
    dialog.setWindowTitle("Settings");
    dialog.setSizeGripEnabled(true);
    sizeDialogToScreen(&dialog, this, 1180, 760, 900, 620);
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    QScrollArea *settingsScrollArea = new QScrollArea(&dialog);
    settingsScrollArea->setWidgetResizable(true);
    settingsScrollArea->setFrameShape(QFrame::NoFrame);

    QWidget *settingsContent = new QWidget(settingsScrollArea);
    QVBoxLayout *contentLayout = new QVBoxLayout(settingsContent);
    contentLayout->setContentsMargins(6, 6, 6, 6);
    contentLayout->setSpacing(12);
    settingsScrollArea->setWidget(settingsContent);
    layout->addWidget(settingsScrollArea, 1);

    // Roles section
    QGroupBox *rolesBox = new QGroupBox("Roles", settingsContent);
    QVBoxLayout *rolesLayout = new QVBoxLayout(rolesBox);
    QLabel *rolesInfo = new QLabel("Add or update roles and toggle Add/Edit/Delete/Serial/Backup/Export permissions.", rolesBox);
    rolesLayout->addWidget(rolesInfo);

    QTableWidget *rolesTable = new QTableWidget(rolesBox);
    rolesTable->setColumnCount(3);
    rolesTable->setHorizontalHeaderLabels({ "Role Name", "Access Level", "Created" });
    rolesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    rolesTable->setSelectionMode(QAbstractItemView::SingleSelection);
    rolesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    rolesTable->setMinimumHeight(170);
    rolesLayout->addWidget(rolesTable);

    auto loadRolesTable = [&]() {
        rolesTable->setRowCount(0);
        QSqlQuery q(m_db);
        q.prepare("SELECT display_name, base_role, created_at FROM app_roles ORDER BY display_name");
        if (!q.exec()) {
            return;
        }
        int row = 0;
        while (q.next()) {
            rolesTable->insertRow(row);
            const QString name = q.value(0).toString();
            const QString baseRole = q.value(1).toString();
            const QString createdAt = q.value(2).toString();
            rolesTable->setItem(row, 0, new QTableWidgetItem(name));
            rolesTable->setItem(row, 1, new QTableWidgetItem(baseRoleToDisplay(baseRoleFromStorage(baseRole))));
            rolesTable->setItem(row, 2, new QTableWidgetItem(createdAt));
            ++row;
        }
        rolesTable->resizeColumnsToContents();
        rolesTable->horizontalHeader()->setStretchLastSection(true);
    };

    loadRolesTable();

    QFormLayout *roleForm = new QFormLayout();
    QLineEdit *roleNameField = new QLineEdit(rolesBox);
    QComboBox *roleAccessCombo = new QComboBox(rolesBox);
    roleAccessCombo->addItem(baseRoleToDisplay(UserRole::ViewOnly), baseRoleToStorage(UserRole::ViewOnly));
    roleAccessCombo->addItem(baseRoleToDisplay(UserRole::AddOnly), baseRoleToStorage(UserRole::AddOnly));
    roleAccessCombo->addItem(baseRoleToDisplay(UserRole::FullAccess), baseRoleToStorage(UserRole::FullAccess));
    QCheckBox *roleCanAdd = new QCheckBox("Add", rolesBox);
    QCheckBox *roleCanEdit = new QCheckBox("Edit", rolesBox);
    QCheckBox *roleCanDelete = new QCheckBox("Delete", rolesBox);
    QCheckBox *roleCanSerialEdit = new QCheckBox("Serial Edit", rolesBox);
    QCheckBox *roleCanSerialDelete = new QCheckBox("Serial Delete", rolesBox);
    QCheckBox *roleCanManageUsers = new QCheckBox("Manage Users", rolesBox);
    QCheckBox *roleCanBackup = new QCheckBox("Backup/Restore", rolesBox);
    QCheckBox *roleCanExport = new QCheckBox("Export", rolesBox);
    QCheckBox *roleCanPrint = new QCheckBox("Print", rolesBox);

    auto *rolePermLayout = new QGridLayout();
    rolePermLayout->addWidget(roleCanAdd, 0, 0);
    rolePermLayout->addWidget(roleCanEdit, 0, 1);
    rolePermLayout->addWidget(roleCanDelete, 0, 2);
    rolePermLayout->addWidget(roleCanSerialEdit, 1, 0);
    rolePermLayout->addWidget(roleCanSerialDelete, 1, 1);
    rolePermLayout->addWidget(roleCanManageUsers, 1, 2);
    rolePermLayout->addWidget(roleCanBackup, 2, 0);
    rolePermLayout->addWidget(roleCanExport, 2, 1);
    rolePermLayout->addWidget(roleCanPrint, 2, 2);

    auto setRoleChecks = [&](const AccessPolicy &policy) {
        roleCanAdd->setChecked(policy.canAdd);
        roleCanEdit->setChecked(policy.canEdit);
        roleCanDelete->setChecked(policy.canDelete);
        roleCanSerialEdit->setChecked(policy.canSerialEdit);
        roleCanSerialDelete->setChecked(policy.canSerialDelete);
        roleCanManageUsers->setChecked(policy.canManageUsers);
        roleCanBackup->setChecked(policy.canBackupRestore);
        roleCanExport->setChecked(policy.canExport);
        roleCanPrint->setChecked(policy.canPrint);
    };
    setRoleChecks(accessPolicyForBaseRole(UserRole::ViewOnly));

    roleForm->addRow("Role Name", roleNameField);
    roleForm->addRow("Access Level", roleAccessCombo);
    roleForm->addRow("Permissions", rolePermLayout);
    rolesLayout->addLayout(roleForm);

    QLabel *rolesStatus = new QLabel(rolesBox);
    rolesStatus->setStyleSheet("color: #c62828;");
    rolesLayout->addWidget(rolesStatus);

    QPushButton *addRoleButton = new QPushButton("Add / Update Role", rolesBox);
    rolesLayout->addWidget(addRoleButton);

    connect(roleAccessCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&]() {
        const UserRole baseRole = baseRoleFromStorage(roleAccessCombo->currentData().toString());
        setRoleChecks(accessPolicyForBaseRole(baseRole));
    });

    connect(rolesTable, &QTableWidget::itemSelectionChanged, &dialog, [&]() {
        const int row = rolesTable->currentRow();
        if (row < 0) {
            return;
        }
        const QString roleName = rolesTable->item(row, 0)->text();
        if (roleName.isEmpty()) {
            return;
        }

        QSqlQuery roleQuery(m_db);
        roleQuery.prepare("SELECT role_key, base_role FROM app_roles WHERE display_name = ?");
        roleQuery.addBindValue(roleName);
        QString roleKey = roleKeyFromName(roleName);
        QString baseRole = baseRoleToStorage(UserRole::ViewOnly);
        if (roleQuery.exec() && roleQuery.next()) {
            roleKey = roleQuery.value(0).toString();
            baseRole = roleQuery.value(1).toString();
        }

        roleNameField->setText(roleName);
        {
            QSignalBlocker blocker(roleAccessCombo);
            const int index = roleAccessCombo->findData(baseRole);
            roleAccessCombo->setCurrentIndex(index >= 0 ? index : 0);
        }
        setRoleChecks(rolePolicyFor(roleKey));
    });

    contentLayout->addWidget(rolesBox);

    // Users section
    QGroupBox *usersBox = new QGroupBox("Users", settingsContent);
    QVBoxLayout *usersLayout = new QVBoxLayout(usersBox);
    QLabel *usersInfo = new QLabel("Add user accounts and assign roles.", usersBox);
    usersLayout->addWidget(usersInfo);

    QTableWidget *usersTable = new QTableWidget(usersBox);
    usersTable->setColumnCount(7);
    usersTable->setHorizontalHeaderLabels({ "Username", "Name", "User ID", "Email", "Role", "Permission Mode", "Created" });
    usersTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    usersTable->setSelectionMode(QAbstractItemView::SingleSelection);
    usersTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    usersTable->setMinimumHeight(200);
    usersLayout->addWidget(usersTable);
    loadUsersIntoTable(usersTable);

    QFormLayout *userForm = new QFormLayout();
    QLineEdit *nameField = new QLineEdit(usersBox);
    QLineEdit *userIdField = new QLineEdit(usersBox);
    QLineEdit *emailField = new QLineEdit(usersBox);
    QLineEdit *usernameField = new QLineEdit(usersBox);
    QLineEdit *passwordField = new QLineEdit(usersBox);
    QLineEdit *confirmField = new QLineEdit(usersBox);
    passwordField->setEchoMode(QLineEdit::Password);
    confirmField->setEchoMode(QLineEdit::Password);

    QComboBox *roleCombo = new QComboBox(usersBox);
    loadRolesIntoCombo(roleCombo);

    QCheckBox *customOverrideCheck = new QCheckBox("Custom permission override", usersBox);
    QCheckBox *userCanAdd = new QCheckBox("Add", usersBox);
    QCheckBox *userCanEdit = new QCheckBox("Edit", usersBox);
    QCheckBox *userCanDelete = new QCheckBox("Delete", usersBox);
    QCheckBox *userCanSerialEdit = new QCheckBox("Serial Edit", usersBox);
    QCheckBox *userCanSerialDelete = new QCheckBox("Serial Delete", usersBox);
    QCheckBox *userCanManageUsers = new QCheckBox("Manage Users", usersBox);
    QCheckBox *userCanBackup = new QCheckBox("Backup/Restore", usersBox);
    QCheckBox *userCanExport = new QCheckBox("Export", usersBox);
    QCheckBox *userCanPrint = new QCheckBox("Print", usersBox);

    auto *userPermLayout = new QGridLayout();
    userPermLayout->addWidget(userCanAdd, 0, 0);
    userPermLayout->addWidget(userCanEdit, 0, 1);
    userPermLayout->addWidget(userCanDelete, 0, 2);
    userPermLayout->addWidget(userCanSerialEdit, 1, 0);
    userPermLayout->addWidget(userCanSerialDelete, 1, 1);
    userPermLayout->addWidget(userCanManageUsers, 1, 2);
    userPermLayout->addWidget(userCanBackup, 2, 0);
    userPermLayout->addWidget(userCanExport, 2, 1);
    userPermLayout->addWidget(userCanPrint, 2, 2);

    auto applyPolicyToUserChecks = [&](const AccessPolicy &policy) {
        userCanAdd->setChecked(policy.canAdd);
        userCanEdit->setChecked(policy.canEdit);
        userCanDelete->setChecked(policy.canDelete);
        userCanSerialEdit->setChecked(policy.canSerialEdit);
        userCanSerialDelete->setChecked(policy.canSerialDelete);
        userCanManageUsers->setChecked(policy.canManageUsers);
        userCanBackup->setChecked(policy.canBackupRestore);
        userCanExport->setChecked(policy.canExport);
        userCanPrint->setChecked(policy.canPrint);
    };

    auto setUserOverrideEnabled = [&]() {
        const bool enabled = customOverrideCheck->isChecked();
        userCanAdd->setEnabled(enabled);
        userCanEdit->setEnabled(enabled);
        userCanDelete->setEnabled(enabled);
        userCanSerialEdit->setEnabled(enabled);
        userCanSerialDelete->setEnabled(enabled);
        userCanManageUsers->setEnabled(enabled);
        userCanBackup->setEnabled(enabled);
        userCanExport->setEnabled(enabled);
        userCanPrint->setEnabled(enabled);
    };

    userForm->addRow("Name", nameField);
    userForm->addRow("User ID", userIdField);
    userForm->addRow("Email", emailField);
    userForm->addRow("Username", usernameField);
    userForm->addRow("Password", passwordField);
    userForm->addRow("Confirm Password", confirmField);
    userForm->addRow("Role", roleCombo);
    userForm->addRow(customOverrideCheck);
    userForm->addRow("Override Permissions", userPermLayout);
    usersLayout->addLayout(userForm);

    QLabel *usersStatus = new QLabel(usersBox);
    usersStatus->setStyleSheet("color: #c62828;");
    usersLayout->addWidget(usersStatus);

    QPushButton *addUserButton = new QPushButton("Add User", usersBox);
    usersLayout->addWidget(addUserButton);

    QPushButton *applyOverrideButton = new QPushButton("Apply Override To Selected User", usersBox);
    QPushButton *clearOverrideButton = new QPushButton("Clear Override For Selected User", usersBox);
    usersLayout->addWidget(applyOverrideButton);
    usersLayout->addWidget(clearOverrideButton);

    const AccessPolicy initialUserPolicy = rolePolicyFor(roleCombo->currentData().toString());
    applyPolicyToUserChecks(initialUserPolicy);
    customOverrideCheck->setChecked(false);
    setUserOverrideEnabled();

    connect(roleCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&]() {
        if (!customOverrideCheck->isChecked()) {
            applyPolicyToUserChecks(rolePolicyFor(roleCombo->currentData().toString()));
        }
    });
    connect(customOverrideCheck, &QCheckBox::toggled, &dialog, [&]() {
        setUserOverrideEnabled();
        if (!customOverrideCheck->isChecked()) {
            applyPolicyToUserChecks(rolePolicyFor(roleCombo->currentData().toString()));
        }
    });

    contentLayout->addWidget(usersBox);
    contentLayout->addStretch(1);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    layout->addWidget(buttons);

    connect(addRoleButton, &QPushButton::clicked, &dialog, [&]() {
        const QString roleName = normalizedText(roleNameField->text());
        const QString roleKey = roleKeyFromName(roleName);
        if (roleName.isEmpty() || roleKey.isEmpty()) {
            rolesStatus->setStyleSheet("color: #c62828;");
            rolesStatus->setText("Role name is required.");
            return;
        }
        const QString baseRole = roleAccessCombo->currentData().toString();
        AccessPolicy newPolicy;
        newPolicy.canAdd = roleCanAdd->isChecked();
        newPolicy.canEdit = roleCanEdit->isChecked();
        newPolicy.canDelete = roleCanDelete->isChecked();
        newPolicy.canSerialEdit = roleCanSerialEdit->isChecked();
        newPolicy.canSerialDelete = roleCanSerialDelete->isChecked();
        newPolicy.canManageUsers = roleCanManageUsers->isChecked();
        newPolicy.canBackupRestore = roleCanBackup->isChecked();
        newPolicy.canExport = roleCanExport->isChecked();
        newPolicy.canPrint = roleCanPrint->isChecked();
        const AccessPolicy oldPolicy = rolePolicyFor(roleKey);

        QSqlQuery insert(m_db);
        insert.prepare("INSERT OR IGNORE INTO app_roles (role_key, display_name, base_role, created_at) VALUES (?, ?, ?, ?)");
        insert.addBindValue(roleKey);
        insert.addBindValue(roleName);
        insert.addBindValue(baseRole);
        insert.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));

        if (!insert.exec()) {
            rolesStatus->setStyleSheet("color: #c62828;");
            rolesStatus->setText("Failed to add role. Role name may already exist.");
            return;
        }

        QSqlQuery update(m_db);
        update.prepare("UPDATE app_roles SET display_name = ?, base_role = ? WHERE role_key = ?");
        update.addBindValue(roleName);
        update.addBindValue(baseRole);
        update.addBindValue(roleKey);
        if (!update.exec() || !upsertRolePermissions(roleKey, newPolicy)) {
            rolesStatus->setStyleSheet("color: #c62828;");
            rolesStatus->setText("Failed to save role permissions.");
            return;
        }

        rolesStatus->setStyleSheet("color: #1b5e20;");
        rolesStatus->setText("Role saved.");
        logAction("ROLE_PERMISSION_UPDATE",
                  roleKey,
                  rolePolicySummary(oldPolicy),
                  rolePolicySummary(newPolicy),
                  QString(),
                  "Security",
                  "PERMISSION_CHANGE",
                  true,
                  QString(),
                  roleKey);
        roleNameField->clear();
        roleAccessCombo->setCurrentIndex(0);
        loadRolesTable();
        loadRolesIntoCombo(roleCombo);
        applyAccessControl(m_currentRoleKey);
    });

    connect(addUserButton, &QPushButton::clicked, &dialog, [&]() {
        const QString fullName = normalizedText(nameField->text());
        const QString userId = normalizedText(userIdField->text());
        const QString email = normalizedText(emailField->text());
        const QString username = normalizeUsername(usernameField->text());
        const QString password = passwordField->text();
        const QString confirm = confirmField->text();

        if (fullName.isEmpty() || userId.isEmpty() || email.isEmpty() || username.isEmpty() || password.isEmpty()) {
            usersStatus->setStyleSheet("color: #c62828;");
            usersStatus->setText("All fields are required.");
            return;
        }
        if (password != confirm) {
            usersStatus->setStyleSheet("color: #c62828;");
            usersStatus->setText("Passwords do not match.");
            return;
        }

        const QString roleKey = roleCombo->currentData().toString();
        QString error;
        if (!createUserAccount(username, password, roleKey, email, userId, fullName, &error)) {
            usersStatus->setStyleSheet("color: #c62828;");
            usersStatus->setText(error);
            return;
        }

        if (customOverrideCheck->isChecked()) {
            AccessPolicy overridePolicy;
            overridePolicy.canAdd = userCanAdd->isChecked();
            overridePolicy.canEdit = userCanEdit->isChecked();
            overridePolicy.canDelete = userCanDelete->isChecked();
            overridePolicy.canSerialEdit = userCanSerialEdit->isChecked();
            overridePolicy.canSerialDelete = userCanSerialDelete->isChecked();
            overridePolicy.canManageUsers = userCanManageUsers->isChecked();
            overridePolicy.canBackupRestore = userCanBackup->isChecked();
            overridePolicy.canExport = userCanExport->isChecked();
            overridePolicy.canPrint = userCanPrint->isChecked();
            upsertUserPermissionOverride(username, &overridePolicy);
        } else {
            upsertUserPermissionOverride(username, nullptr);
        }

        usersStatus->setStyleSheet("color: #1b5e20;");
        usersStatus->setText("User account created.");
        logAction("USER_CREATE",
                  username,
                  QString(),
                  QString("{\"role\":\"%1\",\"custom_override\":%2}")
                      .arg(roleKey, customOverrideCheck->isChecked() ? "true" : "false"),
                  QString(),
                  "Security",
                  "PERMISSION_CHANGE",
                  true,
                  QString(),
                  username);
        nameField->clear();
        userIdField->clear();
        emailField->clear();
        usernameField->clear();
        passwordField->clear();
        confirmField->clear();
        roleCombo->setCurrentIndex(0);
        customOverrideCheck->setChecked(false);
        setUserOverrideEnabled();
        loadUsersIntoTable(usersTable);
    });

    connect(usersTable, &QTableWidget::itemSelectionChanged, &dialog, [&]() {
        const int row = usersTable->currentRow();
        if (row < 0) {
            return;
        }
        const QString selectedUser = usersTable->item(row, 0)->text();
        if (selectedUser.isEmpty()) {
            return;
        }

        QSqlQuery q(m_db);
        q.prepare(
            "SELECT role, "
            "perm_add, perm_edit, perm_delete, perm_serial_edit, perm_serial_delete, "
            "perm_print, perm_manage_users, perm_backup_restore, perm_export "
            "FROM app_users WHERE username = ?");
        q.addBindValue(selectedUser);
        if (!q.exec() || !q.next()) {
            return;
        }

        const QString roleKey = q.value(0).toString();
        const int roleIndex = roleCombo->findData(roleKey);
        if (roleIndex >= 0) {
            roleCombo->setCurrentIndex(roleIndex);
        }

        AccessPolicy effectivePolicy = rolePolicyFor(roleKey);
        const bool hasOverride =
            !q.isNull(1) || !q.isNull(2) || !q.isNull(3) || !q.isNull(4) || !q.isNull(5) ||
            !q.isNull(6) || !q.isNull(7) || !q.isNull(8) || !q.isNull(9);
        if (hasOverride) {
            if (!q.isNull(1)) effectivePolicy.canAdd = q.value(1).toInt() != 0;
            if (!q.isNull(2)) effectivePolicy.canEdit = q.value(2).toInt() != 0;
            if (!q.isNull(3)) effectivePolicy.canDelete = q.value(3).toInt() != 0;
            if (!q.isNull(4)) effectivePolicy.canSerialEdit = q.value(4).toInt() != 0;
            if (!q.isNull(5)) effectivePolicy.canSerialDelete = q.value(5).toInt() != 0;
            if (!q.isNull(6)) effectivePolicy.canPrint = q.value(6).toInt() != 0;
            if (!q.isNull(7)) effectivePolicy.canManageUsers = q.value(7).toInt() != 0;
            if (!q.isNull(8)) effectivePolicy.canBackupRestore = q.value(8).toInt() != 0;
            if (!q.isNull(9)) effectivePolicy.canExport = q.value(9).toInt() != 0;
        }
        customOverrideCheck->setChecked(hasOverride);
        userCanAdd->setChecked(effectivePolicy.canAdd);
        userCanEdit->setChecked(effectivePolicy.canEdit);
        userCanDelete->setChecked(effectivePolicy.canDelete);
        userCanSerialEdit->setChecked(effectivePolicy.canSerialEdit);
        userCanSerialDelete->setChecked(effectivePolicy.canSerialDelete);
        userCanManageUsers->setChecked(effectivePolicy.canManageUsers);
        userCanBackup->setChecked(effectivePolicy.canBackupRestore);
        userCanExport->setChecked(effectivePolicy.canExport);
        userCanPrint->setChecked(effectivePolicy.canPrint);
        setUserOverrideEnabled();
    });

    connect(applyOverrideButton, &QPushButton::clicked, &dialog, [&]() {
        const int row = usersTable->currentRow();
        if (row < 0) {
            usersStatus->setStyleSheet("color: #c62828;");
            usersStatus->setText("Select a user first.");
            return;
        }
        const QString selectedUser = usersTable->item(row, 0)->text();
        if (selectedUser.isEmpty()) {
            usersStatus->setStyleSheet("color: #c62828;");
            usersStatus->setText("Invalid user selection.");
            return;
        }
        AccessPolicy overridePolicy;
        overridePolicy.canAdd = userCanAdd->isChecked();
        overridePolicy.canEdit = userCanEdit->isChecked();
        overridePolicy.canDelete = userCanDelete->isChecked();
        overridePolicy.canSerialEdit = userCanSerialEdit->isChecked();
        overridePolicy.canSerialDelete = userCanSerialDelete->isChecked();
        overridePolicy.canManageUsers = userCanManageUsers->isChecked();
        overridePolicy.canBackupRestore = userCanBackup->isChecked();
        overridePolicy.canExport = userCanExport->isChecked();
        overridePolicy.canPrint = userCanPrint->isChecked();
        if (!upsertUserPermissionOverride(selectedUser, &overridePolicy)) {
            usersStatus->setStyleSheet("color: #c62828;");
            usersStatus->setText("Failed to apply override.");
            return;
        }
        usersStatus->setStyleSheet("color: #1b5e20;");
        usersStatus->setText("Override applied.");
        customOverrideCheck->setChecked(true);
        loadUsersIntoTable(usersTable);
        if (normalizeUsername(selectedUser) == normalizeUsername(m_currentUsername)) {
            applyAccessControl(m_currentRoleKey);
        }
        logAction("USER_PERMISSION_OVERRIDE",
                  selectedUser,
                  QString(),
                  rolePolicySummary(overridePolicy),
                  QString(),
                  "Security",
                  "PERMISSION_CHANGE",
                  true,
                  QString(),
                  selectedUser);
    });

    connect(clearOverrideButton, &QPushButton::clicked, &dialog, [&]() {
        const int row = usersTable->currentRow();
        if (row < 0) {
            usersStatus->setStyleSheet("color: #c62828;");
            usersStatus->setText("Select a user first.");
            return;
        }
        const QString selectedUser = usersTable->item(row, 0)->text();
        if (selectedUser.isEmpty()) {
            usersStatus->setStyleSheet("color: #c62828;");
            usersStatus->setText("Invalid user selection.");
            return;
        }
        if (!upsertUserPermissionOverride(selectedUser, nullptr)) {
            usersStatus->setStyleSheet("color: #c62828;");
            usersStatus->setText("Failed to clear override.");
            return;
        }
        usersStatus->setStyleSheet("color: #1b5e20;");
        usersStatus->setText("Override cleared.");
        customOverrideCheck->setChecked(false);
        setUserOverrideEnabled();
        loadUsersIntoTable(usersTable);
        if (normalizeUsername(selectedUser) == normalizeUsername(m_currentUsername)) {
            applyAccessControl(m_currentRoleKey);
        }
        logAction("USER_PERMISSION_OVERRIDE_CLEAR",
                  selectedUser,
                  QString(),
                  QString(),
                  QString(),
                  "Security",
                  "PERMISSION_CHANGE",
                  true,
                  QString(),
                  selectedUser);
    });

    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    dialog.exec();
}

void MainWindow::showUserAccountsDialog() {
    showSettingsDialog();
}

void MainWindow::showSkuDetailsDialog(const QString &sku) {
    const QString trimmedSku = sku.trimmed().toUpper();
    if (trimmedSku.isEmpty()) return;

    QSqlQuery q(m_db);
    q.prepare(
        "SELECT sku, part_name, part_number, category_code, sub_category, "
        "item_serial, unique_variation, description, storage, rack_number, bin_number, "
        "dimensions, weight_value, weight_unit, product_family, image_blob, image_path, comments, created_at "
        "FROM sku_catalog_active WHERE sku = ?");
    q.addBindValue(trimmedSku);
    if (!q.exec() || !q.next()) {
        setStatus("Unable to load SKU details.", false);
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QString("Product Information  —  %1").arg(trimmedSku));
    sizeDialogToScreen(&dialog, this, 1000, 680, 900, 620);
    dialog.setSizeGripEnabled(true);

    // ── root: image left | details right ─────────────────────────────────────
    auto *root = new QHBoxLayout(&dialog);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(20);

    // ── LEFT: zoomable product image ──────────────────────────────────────────
    auto *imageView = new ZoomableImageView(&dialog);
    imageView->setMinimumSize(340, 340);
    imageView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    imageView->setObjectName("skuDetailImageView");
    imageView->setStyleSheet("background-color:#1a1a1a; border:1px solid #383838; border-radius:10px;");

    const QByteArray blob = q.value(15).toByteArray();
    const QString legacyPath = q.value(16).toString();
    QPixmap px;
    if (!blob.isEmpty()) {
        px.loadFromData(blob);
    } else if (!legacyPath.isEmpty()) {
        px.load(resolveImagePath(legacyPath));
    }
    if (px.isNull()) {
        imageView->setImage(QPixmap());
    } else {
        imageView->setImage(px);
    }

    // ── RIGHT: scrollable detail panel ───────────────────────────────────────
    auto *scrollArea = new QScrollArea(&dialog);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Never let the viewport paint a palette background; the dialog's dark
    // stylesheet background must show through even if the OS palette changes.
    scrollArea->viewport()->setAutoFillBackground(false);

    auto *detailWidget = new QWidget(scrollArea);
    detailWidget->setAutoFillBackground(false);
    auto *detailLayout = new QVBoxLayout(detailWidget);
    detailLayout->setSpacing(12);
    detailLayout->setContentsMargins(4, 4, 12, 4);

    // Part name as a header
    const QString partName = q.value(1).toString();
    auto *titleLabel = new QLabel(partName, detailWidget);
    titleLabel->setWordWrap(true);
    titleLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(14);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    titleLabel->setObjectName("skuDetailTitle");
    detailLayout->addWidget(titleLabel);

    auto *divider = new QFrame(detailWidget);
    divider->setFrameShape(QFrame::HLine);
    divider->setObjectName("skuDetailDivider");
    detailLayout->addWidget(divider);

    // ── Quick-info grid (two columns) ─────────────────────────────────────────
    auto *grid = new QGridLayout();
    grid->setHorizontalSpacing(20);
    grid->setVerticalSpacing(6);

    auto addField = [&](int row, int col, const QString &label, const QString &value) {
        auto *lbl = new QLabel(label + ":", detailWidget);
        lbl->setObjectName("skuDetailFieldLabel");
        QFont lf = lbl->font();
        lf.setBold(true);
        lf.setPointSize(9);
        lbl->setFont(lf);

        auto *val = new QLabel(value.isEmpty() ? "—" : value, detailWidget);
        val->setWordWrap(true);
        val->setTextInteractionFlags(Qt::TextSelectableByMouse);
        val->setObjectName("skuDetailFieldValue");

        grid->addWidget(lbl, row, col * 2);
        grid->addWidget(val, row, col * 2 + 1);
    };

    const QString weight = QString("%1 %2").arg(q.value(12).toString(), q.value(13).toString()).trimmed();
    addField(0, 0, "SKU",            q.value(0).toString());
    addField(0, 1, "Part Number",    q.value(2).toString());
    addField(1, 0, "Category",       q.value(3).toString());
    addField(1, 1, "Sub-Category",   q.value(4).toString());
    addField(2, 0, "Item Serial",    q.value(5).toString());
    addField(2, 1, "Variation",      q.value(6).toString());
    addField(3, 0, "Dimensions",     q.value(11).toString());
    addField(3, 1, "Weight",         weight);
    addField(4, 0, "Storage Zone",   q.value(8).toString());
    addField(4, 1, "Product Family", q.value(14).toString());
    addField(5, 0, "Rack Number",    q.value(9).toString());
    addField(5, 1, "Bin Number",     q.value(10).toString());
    addField(6, 0, "Created At",     q.value(18).toString());

    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(3, 1);
    detailLayout->addLayout(grid);

    auto addTextSection = [&](const QString &heading, const QString &content) {
        auto *hdr = new QLabel(heading, detailWidget);
        hdr->setObjectName("skuDetailSectionHeader");
        QFont hf = hdr->font();
        hf.setBold(true);
        hf.setPointSize(10);
        hdr->setFont(hf);
        detailLayout->addWidget(hdr);

        auto *box = new QPlainTextEdit(detailWidget);
        box->setPlainText(content.isEmpty() ? "—" : content);
        box->setReadOnly(true);
        box->setMinimumHeight(90);
        box->setMaximumHeight(140);
        box->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        box->setObjectName("skuDetailTextBox");
        detailLayout->addWidget(box);
    };

    addTextSection("Description", q.value(7).toString());
    addTextSection("Comments",    q.value(17).toString());

    detailLayout->addStretch(1);
    scrollArea->setWidget(detailWidget);

    // ── Close button ─────────────────────────────────────────────────────────
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    auto *rightPanel = new QVBoxLayout();
    rightPanel->setSpacing(10);
    rightPanel->addWidget(scrollArea, 1);
    rightPanel->addWidget(buttons);

    root->addWidget(imageView, 1);
    root->addLayout(rightPanel, 1);

    dialog.exec();
}

QString MainWindow::extractSkuFromDisplay(const QString &text) const {
    QString sku = text;
    const int pipeIndex = sku.indexOf('|');
    if (pipeIndex >= 0) {
        sku = sku.left(pipeIndex);
    }
    return sku.trimmed().toUpper();
}

bool MainWindow::verifyAdminCredentials(const QString &username, const QString &password) {
    QString roleKey;
    if (!verifyUserCredentials(username, password, &roleKey)) {
        return false;
    }

    AccessPolicy policy = rolePolicyFor(roleKey);
    applyUserOverrides(username, &policy);
    return policy.canManageUsers;
}

bool MainWindow::isPermissionDeniedOpenError(const QString &errorText) const {
    const QString normalized = errorText.trimmed().toLower();
    if (normalized.isEmpty()) {
        return false;
    }
    return normalized.contains("permission denied") ||
           normalized.contains("access is denied") ||
           normalized.contains("readonly") ||
           normalized.contains("read-only") ||
           normalized.contains("unable to open database file") ||
           normalized.contains("authorization denied");
}

bool MainWindow::requestUacElevationForDatabase(const QString &dbPath, const QString &errorText) {
#ifndef Q_OS_WIN
    Q_UNUSED(dbPath);
    Q_UNUSED(errorText);
    return false;
#else
    const QString nativePath = QDir::toNativeSeparators(dbPath);

    QMessageBox prompt(this);
    prompt.setIcon(QMessageBox::Warning);
    prompt.setWindowTitle("Database Permission Required");
    prompt.setText(QString("Windows blocked database access:\n%1").arg(nativePath));
    prompt.setInformativeText(
        "You can relaunch once as Administrator (UAC prompt) or select another writable folder.");
    if (!errorText.trimmed().isEmpty()) {
        prompt.setDetailedText(errorText.trimmed());
    }
    QPushButton *runAsAdmin = prompt.addButton("Run as Administrator", QMessageBox::AcceptRole);
    QPushButton *chooseFolder = prompt.addButton("Choose Another Folder", QMessageBox::ActionRole);
    QPushButton *cancel = prompt.addButton(QMessageBox::Cancel);
    prompt.exec();

    if (prompt.clickedButton() == chooseFolder || prompt.clickedButton() == cancel) {
        return false;
    }
    if (prompt.clickedButton() != runAsAdmin) {
        return false;
    }

    const QString executable = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    const QString parameters = quoteWindowsArgument("--db-path") + " " +
                               quoteWindowsArgument(dbPath) + " " +
                               quoteWindowsArgument("--uac-db-relaunch");

    const HINSTANCE result = ShellExecuteW(
        nullptr,
        L"runas",
        reinterpret_cast<LPCWSTR>(executable.utf16()),
        reinterpret_cast<LPCWSTR>(parameters.utf16()),
        nullptr,
        SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        appendRunLog(QString("UAC relaunch failed for %1 | shell result=%2")
                         .arg(nativePath)
                         .arg(reinterpret_cast<INT_PTR>(result)));
        QMessageBox::warning(this,
                             "Elevation Failed",
                             "Unable to relaunch with Administrator privileges. "
                             "Choose another writable database folder.");
        return false;
    }

    m_relaunchingElevated = true;
    appendRunLog(QString("UAC relaunch requested for database path: %1").arg(nativePath));
    return true;
#endif
}

bool MainWindow::promptAdminAuthorization(const QString &action, QString *commentOut, bool requireComment) {
    QDialog dialog(this);
    dialog.setWindowTitle(QString("Authorization Required - %1").arg(action));
    QFormLayout *form = new QFormLayout(&dialog);

    QLineEdit *usernameField = new QLineEdit(&dialog);
    usernameField->setText(m_currentUsername);
    QLineEdit *passwordField = new QLineEdit(&dialog);
    passwordField->setEchoMode(QLineEdit::Password);
    QPlainTextEdit *reasonField = new QPlainTextEdit(&dialog);
    reasonField->setPlaceholderText("Enter reason");
    reasonField->setFixedHeight(90);

    form->addRow("Admin Username", usernameField);
    form->addRow("Admin Password", passwordField);
    form->addRow("Reason", reasonField);

    QLabel *errorLabel = new QLabel(&dialog);
    errorLabel->setStyleSheet("color: #c62828;");
    form->addRow(errorLabel);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText("Authorize");
    form->addRow(buttons);

    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        const QString username = normalizeUsername(usernameField->text());
        const QString password = passwordField->text();
        const QString comment = reasonField->toPlainText().trimmed();

        if (username.isEmpty() || password.isEmpty()) {
            errorLabel->setText("Admin credentials are required.");
            return;
        }
        if (!verifyAdminCredentials(username, password)) {
            errorLabel->setText("Invalid admin credentials.");
            return;
        }
        if (requireComment && comment.size() < 20) {
            errorLabel->setText("Reason must be at least 20 characters.");
            return;
        }

        if (commentOut) {
            *commentOut = comment;
        }
        dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    const bool ok = (dialog.exec() == QDialog::Accepted);
    if (!ok) {
        logAction("AUTHORIZATION_DENIED",
                  action,
                  QString(),
                  QString(),
                  QStringLiteral("Authorization dialog canceled or failed"),
                  "Security",
                  "PERMISSION",
                  false,
                  "Authorization canceled or failed");
    }
    return ok;
}

bool MainWindow::logAction(const QString &action,
                           const QString &entity,
                           const QString &oldValue,
                           const QString &newValue,
                           const QString &comment,
                           const QString &module,
                           const QString &actionType,
                           bool success,
                           const QString &errorMessage,
                           const QString &recordId) {
    QString normalizedType = actionType.trimmed().toUpper();
    if (normalizedType.isEmpty()) {
        const QString upper = action.trimmed().toUpper();
        if (upper.contains("CREATE") || upper.contains("ADD")) {
            normalizedType = "CREATE";
        } else if (upper.contains("UPDATE") || upper.contains("EDIT")) {
            normalizedType = "EDIT";
        } else if (upper.contains("DELETE")) {
            normalizedType = "DELETE";
        } else if (upper.contains("IMPORT")) {
            normalizedType = "IMPORT";
        } else if (upper.contains("EXPORT")) {
            normalizedType = "EXPORT";
        } else if (upper.contains("LOGIN")) {
            normalizedType = "LOGIN";
        } else if (upper.contains("PERMISSION") || upper.contains("ROLE") || upper.contains("USER")) {
            normalizedType = "PERMISSION_CHANGE";
        } else if (upper.contains("BACKUP") || upper.contains("RESTORE")) {
            normalizedType = "BACKUP_RESTORE";
        } else {
            normalizedType = "ACTION";
        }
    }

    QSqlQuery q(m_db);
    q.prepare(
        "INSERT INTO app_audit_log ("
        "timestamp, user, user_id, role, machine_id, module_screen, action_type, action, entity, record_id, "
        "old_value, new_value, comment, result, error_message"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    q.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    q.addBindValue(m_currentUsername.isEmpty() ? QString("unknown") : m_currentUsername);
    q.addBindValue(m_currentUserId);
    q.addBindValue(m_currentRoleKey);
    q.addBindValue(machineId());
    q.addBindValue(module.trimmed().isEmpty() ? QStringLiteral("General") : module.trimmed());
    q.addBindValue(normalizedType);
    q.addBindValue(action);
    q.addBindValue(entity);
    q.addBindValue(recordId);
    q.addBindValue(oldValue);
    q.addBindValue(newValue);
    q.addBindValue(comment);
    q.addBindValue(success ? QStringLiteral("SUCCESS") : QStringLiteral("FAIL"));
    q.addBindValue(errorMessage);
    return q.exec();
}

bool MainWindow::applyRolePolicy(const QString &roleKey, AccessPolicy *policyOut) const {
    if (!policyOut) {
        return false;
    }

    UserRole baseRole = UserRole::ViewOnly;
    QString displayName;
    if (!fetchRoleInfo(roleKey, &baseRole, &displayName)) {
        baseRole = baseRoleFromStorage(roleKey);
    }
    AccessPolicy policy = accessPolicyForBaseRole(baseRole);

    QSqlQuery q(m_db);
    q.prepare(
        "SELECT "
        "can_add, can_edit, can_delete, can_serial_edit, can_serial_delete, "
        "can_print, can_manage_users, can_backup_restore, can_export "
        "FROM app_roles WHERE role_key = ?");
    q.addBindValue(roleKey);
    if (q.exec() && q.next()) {
        auto applyIfSet = [&](int idx, bool *field) {
            if (!field || q.isNull(idx)) {
                return;
            }
            *field = q.value(idx).toInt() != 0;
        };
        applyIfSet(0, &policy.canAdd);
        applyIfSet(1, &policy.canEdit);
        applyIfSet(2, &policy.canDelete);
        applyIfSet(3, &policy.canSerialEdit);
        applyIfSet(4, &policy.canSerialDelete);
        applyIfSet(5, &policy.canPrint);
        applyIfSet(6, &policy.canManageUsers);
        applyIfSet(7, &policy.canBackupRestore);
        applyIfSet(8, &policy.canExport);
    }

    *policyOut = policy;
    return true;
}

MainWindow::AccessPolicy MainWindow::rolePolicyFor(const QString &roleKey) const {
    AccessPolicy policy = accessPolicyForBaseRole(baseRoleFromStorage(roleKey));
    applyRolePolicy(roleKey, &policy);
    return policy;
}

bool MainWindow::applyUserOverrides(const QString &username, AccessPolicy *policy) const {
    if (!policy) {
        return false;
    }
    const QString normalizedUser = normalizeUsername(username);
    if (normalizedUser.isEmpty()) {
        return false;
    }

    QSqlQuery q(m_db);
    q.prepare(
        "SELECT "
        "perm_add, perm_edit, perm_delete, perm_serial_edit, perm_serial_delete, "
        "perm_print, perm_manage_users, perm_backup_restore, perm_export "
        "FROM app_users WHERE username = ?");
    q.addBindValue(normalizedUser);
    if (!q.exec() || !q.next()) {
        return false;
    }

    auto applyOverride = [&](int idx, bool *field) {
        if (!field || q.isNull(idx)) {
            return;
        }
        *field = q.value(idx).toInt() != 0;
    };
    applyOverride(0, &policy->canAdd);
    applyOverride(1, &policy->canEdit);
    applyOverride(2, &policy->canDelete);
    applyOverride(3, &policy->canSerialEdit);
    applyOverride(4, &policy->canSerialDelete);
    applyOverride(5, &policy->canPrint);
    applyOverride(6, &policy->canManageUsers);
    applyOverride(7, &policy->canBackupRestore);
    applyOverride(8, &policy->canExport);
    return true;
}

bool MainWindow::upsertRolePermissions(const QString &roleKey, const AccessPolicy &policy) {
    const QString key = normalizedText(roleKey);
    if (key.isEmpty()) {
        return false;
    }

    QSqlQuery update(m_db);
    update.prepare(
        "UPDATE app_roles SET "
        "can_add = ?, can_edit = ?, can_delete = ?, can_serial_edit = ?, can_serial_delete = ?, "
        "can_print = ?, can_manage_users = ?, can_backup_restore = ?, can_export = ? "
        "WHERE role_key = ?");
    update.addBindValue(policy.canAdd ? 1 : 0);
    update.addBindValue(policy.canEdit ? 1 : 0);
    update.addBindValue(policy.canDelete ? 1 : 0);
    update.addBindValue(policy.canSerialEdit ? 1 : 0);
    update.addBindValue(policy.canSerialDelete ? 1 : 0);
    update.addBindValue(policy.canPrint ? 1 : 0);
    update.addBindValue(policy.canManageUsers ? 1 : 0);
    update.addBindValue(policy.canBackupRestore ? 1 : 0);
    update.addBindValue(policy.canExport ? 1 : 0);
    update.addBindValue(key);
    return update.exec();
}

QString MainWindow::rolePolicySummary(const AccessPolicy &policy) const {
    QJsonObject obj;
    obj.insert("can_add", policy.canAdd);
    obj.insert("can_edit", policy.canEdit);
    obj.insert("can_delete", policy.canDelete);
    obj.insert("can_serial_edit", policy.canSerialEdit);
    obj.insert("can_serial_delete", policy.canSerialDelete);
    obj.insert("can_print", policy.canPrint);
    obj.insert("can_manage_users", policy.canManageUsers);
    obj.insert("can_backup_restore", policy.canBackupRestore);
    obj.insert("can_export", policy.canExport);
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

bool MainWindow::hasPermissionOverride(const QString &username) const {
    const QString normalizedUser = normalizeUsername(username);
    if (normalizedUser.isEmpty()) {
        return false;
    }
    QSqlQuery q(m_db);
    q.prepare(
        "SELECT "
        "perm_add IS NOT NULL OR perm_edit IS NOT NULL OR perm_delete IS NOT NULL OR "
        "perm_serial_edit IS NOT NULL OR perm_serial_delete IS NOT NULL OR perm_print IS NOT NULL OR "
        "perm_manage_users IS NOT NULL OR perm_backup_restore IS NOT NULL OR perm_export IS NOT NULL "
        "FROM app_users WHERE username = ?");
    q.addBindValue(normalizedUser);
    if (!q.exec() || !q.next()) {
        return false;
    }
    return q.value(0).toInt() != 0;
}

bool MainWindow::upsertUserPermissionOverride(const QString &username, const AccessPolicy *policy) {
    const QString normalizedUser = normalizeUsername(username);
    if (normalizedUser.isEmpty()) {
        return false;
    }

    QSqlQuery q(m_db);
    q.prepare(
        "UPDATE app_users SET "
        "perm_add = ?, perm_edit = ?, perm_delete = ?, perm_serial_edit = ?, perm_serial_delete = ?, "
        "perm_print = ?, perm_manage_users = ?, perm_backup_restore = ?, perm_export = ? "
        "WHERE username = ?");

    auto bindValue = [&](bool value) { q.addBindValue(value ? 1 : 0); };
    auto bindNull = [&]() { q.addBindValue(QVariant()); };

    if (policy) {
        bindValue(policy->canAdd);
        bindValue(policy->canEdit);
        bindValue(policy->canDelete);
        bindValue(policy->canSerialEdit);
        bindValue(policy->canSerialDelete);
        bindValue(policy->canPrint);
        bindValue(policy->canManageUsers);
        bindValue(policy->canBackupRestore);
        bindValue(policy->canExport);
    } else {
        bindNull();
        bindNull();
        bindNull();
        bindNull();
        bindNull();
        bindNull();
        bindNull();
        bindNull();
        bindNull();
    }
    q.addBindValue(normalizedUser);
    return q.exec();
}

QString MainWindow::machineId() const {
    const QByteArray unique = QSysInfo::machineUniqueId();
    if (!unique.isEmpty()) {
        return QString::fromLatin1(unique.toHex());
    }
    return QSysInfo::machineHostName();
}

bool MainWindow::isStrongPassword(const QString &password, QString *reasonOut) const {
    if (password.size() < 10) {
        if (reasonOut) {
            *reasonOut = "Password must be at least 10 characters.";
        }
        return false;
    }
    if (!password.contains(QRegularExpression("[A-Z]"))) {
        if (reasonOut) {
            *reasonOut = "Password must include an uppercase letter.";
        }
        return false;
    }
    if (!password.contains(QRegularExpression("[a-z]"))) {
        if (reasonOut) {
            *reasonOut = "Password must include a lowercase letter.";
        }
        return false;
    }
    if (!password.contains(QRegularExpression("\\d"))) {
        if (reasonOut) {
            *reasonOut = "Password must include a number.";
        }
        return false;
    }
    if (!password.contains(QRegularExpression("[^A-Za-z0-9]"))) {
        if (reasonOut) {
            *reasonOut = "Password must include a special character.";
        }
        return false;
    }
    return true;
}

QString MainWindow::configuredBackupRoot() const {
    QSettings settings;
    QString backupRoot = settings.value("backup/path").toString().trimmed();
    if (backupRoot.isEmpty()) {
        backupRoot = QDir(dataRootPath()).filePath("backups");
    }
    QDir dir(backupRoot);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    return dir.absolutePath();
}

QString MainWindow::automatedBackupFilePath(const QString &classification) const {
    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    const QString fileName = QString("sku_backup_%1_%2.encdb")
                                 .arg(classification.trimmed().isEmpty() ? "daily" : classification.trimmed().toLower(),
                                      stamp);
    return QDir(configuredBackupRoot()).filePath(fileName);
}

bool MainWindow::createEncryptedBackup(const QString &sourcePath,
                                       const QString &targetPath,
                                       QString *checksumOut,
                                       qint64 *sizeOut,
                                       QString *metadataOut,
                                       QString *errorOut) {
    // Read the live SQLite file into memory first so the encryption and verification
    // stages operate on a stable byte sequence.
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        if (errorOut) {
            *errorOut = "Unable to read source database.";
        }
        return false;
    }
    const QByteArray plain = source.readAll();
    source.close();
    if (plain.isEmpty()) {
        if (errorOut) {
            *errorOut = "Source database is empty.";
        }
        return false;
    }

    const bool headerLooksValid = plain.startsWith("SQLite format 3");
    int pageSize = 0;
    if (plain.size() >= 18) {
        const quint8 hi = static_cast<quint8>(plain.at(16));
        const quint8 lo = static_cast<quint8>(plain.at(17));
        pageSize = static_cast<int>((hi << 8) | lo);
        if (pageSize == 1) {
            pageSize = 65536;
        }
    }

    QByteArray encrypted;
    QByteArray verifyBytes;
    QString method = "xor";
    bool encryptedOk = false;

#ifdef Q_OS_WIN
    // Prefer DPAPI on Windows so backups stay machine/user protected without asking
    // operators to manage an extra secret.
    DATA_BLOB inBlob;
    inBlob.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(plain.constData()));
    inBlob.cbData = static_cast<DWORD>(plain.size());
    DATA_BLOB outBlob;
    ZeroMemory(&outBlob, sizeof(outBlob));

    if (CryptProtectData(&inBlob, L"WarehouseSkuBackup", nullptr, nullptr, nullptr, 0, &outBlob)) {
        method = "dpapi";
        encrypted = QByteArray(reinterpret_cast<const char *>(outBlob.pbData), static_cast<int>(outBlob.cbData));
        LocalFree(outBlob.pbData);
        encryptedOk = true;

        DATA_BLOB decryptIn;
        decryptIn.pbData = reinterpret_cast<BYTE *>(encrypted.data());
        decryptIn.cbData = static_cast<DWORD>(encrypted.size());
        DATA_BLOB decryptOut;
        ZeroMemory(&decryptOut, sizeof(decryptOut));
        if (CryptUnprotectData(&decryptIn, nullptr, nullptr, nullptr, nullptr, 0, &decryptOut)) {
            verifyBytes = QByteArray(reinterpret_cast<const char *>(decryptOut.pbData), static_cast<int>(decryptOut.cbData));
            LocalFree(decryptOut.pbData);
        } else {
            verifyBytes.clear();
        }
    }
#endif

    if (!encryptedOk) {
        // Non-Windows builds (or DPAPI failures) fall back to a deterministic XOR
        // stream derived from the machine identity. It is weaker than DPAPI but keeps
        // the backup format reversible for restore and verification.
        const QByteArray key = QCryptographicHash::hash((machineId() + "|warehouse-backup").toUtf8(),
                                                        QCryptographicHash::Sha256);
        encrypted = plain;
        for (int i = 0; i < encrypted.size(); ++i) {
            encrypted[i] = encrypted[i] ^ key.at(i % key.size());
        }
        verifyBytes = encrypted;
        for (int i = 0; i < verifyBytes.size(); ++i) {
            verifyBytes[i] = verifyBytes[i] ^ key.at(i % key.size());
        }
    }

    if (verifyBytes.isEmpty() || !verifyBytes.startsWith("SQLite format 3")) {
        if (errorOut) {
            *errorOut = "Backup verification failed.";
        }
        return false;
    }

    QFile out(targetPath);
    if (!out.open(QIODevice::WriteOnly)) {
        if (errorOut) {
            *errorOut = "Unable to write backup file.";
        }
        return false;
    }
    if (out.write(encrypted) != encrypted.size()) {
        out.close();
        if (errorOut) {
            *errorOut = "Failed to write complete backup file.";
        }
        return false;
    }
    out.close();

    const QString checksum = QString::fromLatin1(
        QCryptographicHash::hash(encrypted, QCryptographicHash::Sha256).toHex());
    if (checksumOut) {
        *checksumOut = checksum;
    }
    if (sizeOut) {
        *sizeOut = encrypted.size();
    }

    QJsonObject meta;
    meta.insert("encryption_method", method);
    meta.insert("source_size", static_cast<qint64>(plain.size()));
    meta.insert("encrypted_size", static_cast<qint64>(encrypted.size()));
    meta.insert("sqlite_header_valid", headerLooksValid);
    if (pageSize > 0) {
        meta.insert("page_size", pageSize);
    }
    meta.insert("restore_validation", verifyBytes.startsWith("SQLite format 3") ? "ok" : "failed");
    if (metadataOut) {
        *metadataOut = QString::fromUtf8(QJsonDocument(meta).toJson(QJsonDocument::Compact));
    }
    return true;
}

bool MainWindow::restoreEncryptedBackup(const QString &sourcePath,
                                        const QString &targetPath,
                                        QString *errorOut) {
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        if (errorOut) {
            *errorOut = "Unable to read encrypted backup file.";
        }
        return false;
    }
    const QByteArray encrypted = source.readAll();
    source.close();
    if (encrypted.isEmpty()) {
        if (errorOut) {
            *errorOut = "Encrypted backup is empty.";
        }
        return false;
    }

    QByteArray plain;
    bool decrypted = false;

#ifdef Q_OS_WIN
    DATA_BLOB inBlob;
    inBlob.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(encrypted.constData()));
    inBlob.cbData = static_cast<DWORD>(encrypted.size());
    DATA_BLOB outBlob;
    ZeroMemory(&outBlob, sizeof(outBlob));
    if (CryptUnprotectData(&inBlob, nullptr, nullptr, nullptr, nullptr, 0, &outBlob)) {
        plain = QByteArray(reinterpret_cast<const char *>(outBlob.pbData), static_cast<int>(outBlob.cbData));
        LocalFree(outBlob.pbData);
        decrypted = true;
    }
#endif

    if (!decrypted) {
        const QByteArray key = QCryptographicHash::hash((machineId() + "|warehouse-backup").toUtf8(),
                                                        QCryptographicHash::Sha256);
        plain = encrypted;
        for (int i = 0; i < plain.size(); ++i) {
            plain[i] = plain[i] ^ key.at(i % key.size());
        }
    }

    if (!plain.startsWith("SQLite format 3")) {
        if (errorOut) {
            *errorOut = "Backup decryption failed. File may be corrupted or created on a different system.";
        }
        return false;
    }

    QFileInfo sourceInfo(sourcePath);
    QFileInfo targetInfo(targetPath);
    if (sourceInfo.exists() && targetInfo.exists()) {
        const QString sourceCanonical = sourceInfo.canonicalFilePath();
        const QString targetCanonical = targetInfo.canonicalFilePath();
        if (!sourceCanonical.isEmpty() && !targetCanonical.isEmpty() && sourceCanonical == targetCanonical) {
            if (errorOut) {
                *errorOut = "Restore target cannot be the same as source backup file.";
            }
            return false;
        }
    }

    const QString targetDir = QFileInfo(targetPath).absolutePath();
    if (!targetDir.isEmpty()) {
        QDir dir(targetDir);
        if (!dir.exists() && !dir.mkpath(".")) {
            if (errorOut) {
                *errorOut = "Unable to create restore target directory.";
            }
            return false;
        }
    }

    QFile out(targetPath);
    if (out.exists()) {
        QFile::remove(targetPath);
    }
    if (!out.open(QIODevice::WriteOnly)) {
        if (errorOut) {
            *errorOut = "Unable to write restored database file.";
        }
        return false;
    }
    if (out.write(plain) != plain.size()) {
        out.close();
        if (errorOut) {
            *errorOut = "Failed to write complete restored database file.";
        }
        return false;
    }
    out.close();
    return true;
}

int MainWindow::totalQuantityForSku(const QString &sku) const {
    if (sku.trimmed().isEmpty()) {
        return 0;
    }
    QSqlQuery q(m_db);
    q.prepare("SELECT COUNT(*) FROM barcode_log_active WHERE sku = ?");
    q.addBindValue(sku.trimmed().toUpper());
    if (q.exec() && q.next()) {
        return q.value(0).toInt();
    }
    return 0;
}

QString MainWindow::numberToWords(int value) const {
    if (value == 0) {
        return "Zero";
    }
    if (value < 0) {
        return QString("Minus %1").arg(numberToWords(-value));
    }

    static const QStringList ones = {
        "", "One", "Two", "Three", "Four", "Five", "Six", "Seven", "Eight", "Nine",
        "Ten", "Eleven", "Twelve", "Thirteen", "Fourteen", "Fifteen", "Sixteen",
        "Seventeen", "Eighteen", "Nineteen"
    };
    static const QStringList tens = {
        "", "", "Twenty", "Thirty", "Forty", "Fifty", "Sixty", "Seventy", "Eighty", "Ninety"
    };

    auto chunkToWords = [&](int num) {
        QStringList words;
        if (num >= 100) {
            words << ones.at(num / 100) << "Hundred";
            num %= 100;
        }
        if (num >= 20) {
            words << tens.at(num / 10);
            if (num % 10) {
                words << ones.at(num % 10);
            }
        } else if (num > 0) {
            words << ones.at(num);
        }
        return words.join(' ');
    };

    QStringList parts;
    int remaining = value;
    const int billions = remaining / 1000000000;
    if (billions > 0) {
        parts << QString("%1 Billion").arg(chunkToWords(billions));
        remaining %= 1000000000;
    }
    const int millions = remaining / 1000000;
    if (millions > 0) {
        parts << QString("%1 Million").arg(chunkToWords(millions));
        remaining %= 1000000;
    }
    const int thousands = remaining / 1000;
    if (thousands > 0) {
        parts << QString("%1 Thousand").arg(chunkToWords(thousands));
        remaining %= 1000;
    }
    if (remaining > 0) {
        parts << chunkToWords(remaining);
    }

    return parts.join(' ');
}

void MainWindow::updateQuantityWordsLabels(const QString &sku) {
    const QString trimmedSku = sku.trimmed().toUpper();
    if (trimmedSku.isEmpty()) {
        if (m_searchQuantityWordsValueLabel) {
            m_searchQuantityWordsValueLabel->setText("-");
        }
        if (m_barcodeQuantityWordsValueLabel) {
            m_barcodeQuantityWordsValueLabel->setText("-");
        }
        return;
    }

    const int total = totalQuantityForSku(trimmedSku);
    const QString words = numberToWords(total);
    if (m_searchQuantityWordsValueLabel) {
        m_searchQuantityWordsValueLabel->setText(words);
    }
    if (m_barcodeQuantityWordsValueLabel) {
        m_barcodeQuantityWordsValueLabel->setText(words);
    }
}

void MainWindow::resetBarcodeFieldsForSkuChange() {
    if (m_barcodeQuantityField) {
        m_barcodeQuantityField->setText("1");
    }
    if (m_barcodeNextSerialField) {
        m_barcodeNextSerialField->setText("1");
    }
    if (m_barcodeValueField) {
        m_barcodeValueField->clear();
    }
    if (m_barcodePreview) {
        m_barcodePreview->setText("No QR Code");
        m_barcodePreview->setPixmap(QPixmap());
    }
    m_barcodeImage = QImage();
    m_lastGeneratedBarcodes.clear();
    populateBarcodeModel({});
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::Resize && m_latestSkuScrollArea) {
        if (watched == m_latestSkuScrollArea || watched == m_latestSkuScrollArea->viewport()) {
            if (!m_refreshingLatestSkuCards) {
                scheduleLatestSkuCardsRefresh(100);
            }
            return false;
        }
    }
    if (event->type() == QEvent::MouseButtonRelease) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            const QVariant skuValue = watched->property("sku");
            if (skuValue.isValid()) {
                showSkuDetailsDialog(skuValue.toString());
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    QMainWindow::closeEvent(event);
    if (event->isAccepted()) {
        appendRunLog("MainWindow closeEvent accepted");
        QCoreApplication::quit();
    }
}

void MainWindow::setupUi() {
    ui = new Ui::MainWindow;
    ui->setupUi(this);

    // Cap the left control panels to ~1/3 so the table gets the remaining 2/3.
    // setMaximumWidth is needed because form widget minimums override stretch alone.
    // On compact displays (14" laptops at 150% scaling => ~1280 logical px wide)
    // tighten the caps so the tables keep a usable share of the window.
    const QScreen *setupScreen = screen() ? screen() : QGuiApplication::primaryScreen();
    const QRect setupAvail = setupScreen ? setupScreen->availableGeometry() : QRect(0, 0, 1920, 1080);
    const bool compactUi = setupAvail.width() < 1500 || setupAvail.height() < 800;
    const int panelMaxWidth = compactUi ? 350 : 420;
    ui->barcodeGroupBox->setMaximumWidth(panelMaxWidth);
    ui->barcodeSkuDetailsGroupBox->setMaximumWidth(panelMaxWidth);
    ui->historySkuGroupBox->setMaximumWidth(panelMaxWidth);

    if (ui->centralwidget && ui->centralwidget->layout()) {
        if (auto *mainLayout = qobject_cast<QVBoxLayout *>(ui->centralwidget->layout())) {
            m_noDbBannerLabel = new QLabel(ui->centralwidget);
            m_noDbBannerLabel->setObjectName("noDbBannerLabel");
            m_noDbBannerLabel->setWordWrap(true);
            m_noDbBannerLabel->setMinimumHeight(34);
            m_noDbBannerLabel->setVisible(true);
            mainLayout->insertWidget(0, m_noDbBannerLabel);
        }
    }

    // ── DARK THEME (fixed) ─────────────────────────────────────────────────────
    // Neutral gray base with a single warm-orange highlight/accent color. This is
    // the app's only theme — there is no theme switcher.
    // Palette: bg #141414 · surface #1a1a1a · elevated #202020 · border #383838
    //          text #eaeaea · muted #969696 · accent/highlight #ff7c38
    const QString darkTheme = QStringLiteral(R"(
QMainWindow { background-color: #141414; color: #eaeaea; }
QWidget { color: #eaeaea; font-family: "Segoe UI", "Inter", sans-serif; font-size: 10pt; }
QDialog { background-color: #1a1a1a; color: #eaeaea; }
QGroupBox { background-color: #1a1a1a; border: 1px solid #383838; border-radius: 9px; margin-top: 18px; padding-top: 8px; }
QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; color: #ff7c38; font-weight: 700; font-size: 9pt; }
QLineEdit, QPlainTextEdit, QSpinBox, QDoubleSpinBox, QComboBox, QDateEdit { background-color: #202020; border: 1px solid #3a3a3a; border-radius: 6px; padding: 5px 9px; color: #eaeaea; selection-background-color: #ff7c38; selection-color: #141414; min-height: 15px; }
QLineEdit:hover, QPlainTextEdit:hover, QSpinBox:hover, QDoubleSpinBox:hover, QComboBox:hover, QDateEdit:hover { border-color: #4f4f4f; }
QLineEdit:focus, QPlainTextEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus, QDateEdit:focus { border: 1.5px solid #ff7c38; background-color: #242424; }
QLineEdit:read-only { color: #969696; background-color: #1a1a1a; }
QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled { color: #454545; background-color: #1a1a1a; border-color: #262626; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox QAbstractItemView, QListView { background-color: #202020; border: 1px solid #383838; color: #eaeaea; selection-background-color: #ff7c38; selection-color: #141414; padding: 2px; outline: none; }
QTableView { background-color: #141414; alternate-background-color: #181818; gridline-color: #1e1e1e; border: 1px solid #383838; border-radius: 8px; color: #eaeaea; }
QTableView::item { padding: 4px 7px; color: #eaeaea; }
QTableView::item:hover { background-color: #202020; }
QHeaderView::section, QTableCornerButton::section { background-color: #1a1a1a; color: #a8a8a8; border: none; border-bottom: 2px solid #ff7c38; border-right: 1px solid #383838; padding: 5px 7px; font-weight: 700; font-size: 9pt; }
QHeaderView::section:last { border-right: none; }
QPushButton { background-color: #232323; border: 1px solid #3d3d3d; border-radius: 6px; padding: 6px 12px; font-weight: 600; color: #d6d6d6; min-height: 13px; }
QPushButton:hover { background-color: #2d2d2d; border-color: #ff7c38; color: #ffffff; }
QPushButton:pressed { background-color: #1a1a1a; border-color: #383838; }
QPushButton:disabled { background-color: #1a1a1a; color: #454545; border-color: #202020; }
QPushButton#generateBarcodeButton, QPushButton#historyRefreshButton, QPushButton#searchButton, QPushButton#backupDbButton, QPushButton#dashboardSearchButton { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #ff8c48,stop:1 #f06820); border: 1px solid #b84810; border-bottom: 3px solid #883000; border-radius: 7px; color: #141414; font-weight: 700; padding: 7px 16px 5px; }
QPushButton#generateBarcodeButton:hover, QPushButton#historyRefreshButton:hover, QPushButton#searchButton:hover, QPushButton#backupDbButton:hover, QPushButton#dashboardSearchButton:hover { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #ffa060,stop:1 #ff8040); border-color: #d87030; border-bottom-color: #a84810; }
QPushButton#generateBarcodeButton:pressed, QPushButton#historyRefreshButton:pressed, QPushButton#searchButton:pressed, QPushButton#backupDbButton:pressed, QPushButton#dashboardSearchButton:pressed { background: #d06010; border-bottom-width: 1px; padding: 9px 16px 5px; }
QPushButton#generateBarcodeButton:disabled, QPushButton#historyRefreshButton:disabled, QPushButton#searchButton:disabled, QPushButton#backupDbButton:disabled, QPushButton#dashboardSearchButton:disabled { background: #281808; color: #604020; border: 1px solid #281808; }
QPushButton#saveButton { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #48c880,stop:1 #28a060); border: 1px solid #188040; border-bottom: 3px solid #0c6030; border-radius: 7px; color: #ffffff; font-weight: 700; padding: 7px 16px 5px; }
QPushButton#saveButton:hover { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #60d898,stop:1 #40b878); border-color: #28a060; border-bottom-color: #1a7840; }
QPushButton#saveButton:pressed { background: #20904a; border-bottom-width: 1px; padding: 9px 16px 5px; }
QPushButton#saveButton:disabled { background: #0a1a10; color: #2a5030; border: 1px solid #0a1a10; }
QPushButton#printBarcodeButton { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #20c8b8,stop:1 #0ea898); border: 1px solid #088880; border-bottom: 3px solid #046858; border-radius: 7px; color: #ffffff; font-weight: 700; padding: 7px 16px 5px; }
QPushButton#printBarcodeButton:hover { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #38d8c8,stop:1 #20b8a8); border-color: #10a090; border-bottom-color: #087868; }
QPushButton#printBarcodeButton:pressed { background: #089080; border-bottom-width: 1px; padding: 9px 16px 5px; }
QPushButton#printBarcodeButton:disabled { background: #061410; color: #1a3830; border: 1px solid #061410; }
QPushButton#updateButton, QPushButton#fillFromSearchButton, QPushButton#historyEditButton, QPushButton#browseImageButton { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #5a9aff,stop:1 #3070e8); border: 1px solid #2050c8; border-bottom: 3px solid #1038a0; border-radius: 7px; color: #ffffff; font-weight: 700; padding: 7px 16px 5px; }
QPushButton#updateButton:hover, QPushButton#fillFromSearchButton:hover, QPushButton#historyEditButton:hover, QPushButton#browseImageButton:hover { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #78b0ff,stop:1 #4888f8); border-color: #3868e0; border-bottom-color: #2050c0; }
QPushButton#updateButton:pressed, QPushButton#fillFromSearchButton:pressed, QPushButton#historyEditButton:pressed, QPushButton#browseImageButton:pressed { background: #1c58d0; border-bottom-width: 1px; padding: 9px 16px 5px; }
QPushButton#updateButton:disabled, QPushButton#fillFromSearchButton:disabled, QPushButton#historyEditButton:disabled, QPushButton#browseImageButton:disabled { background: #080c20; color: #202848; border: 1px solid #080c20; }
QPushButton#deleteSkuButton, QPushButton#deleteBarcodeButton, QPushButton#historyDeleteButton { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #f06060,stop:1 #d02828); border: 1px solid #a81818; border-bottom: 3px solid #800808; border-radius: 7px; color: #ffffff; font-weight: 700; padding: 7px 16px 5px; }
QPushButton#deleteSkuButton:hover, QPushButton#deleteBarcodeButton:hover, QPushButton#historyDeleteButton:hover { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #ff7878,stop:1 #e84040); border-color: #c02020; border-bottom-color: #981010; }
QPushButton#deleteSkuButton:pressed, QPushButton#deleteBarcodeButton:pressed, QPushButton#historyDeleteButton:pressed { background: #b81818; border-bottom-width: 1px; padding: 9px 16px 5px; }
QPushButton#deleteSkuButton:disabled, QPushButton#deleteBarcodeButton:disabled, QPushButton#historyDeleteButton:disabled { background: #180606; color: #402020; border: 1px solid #180606; }
QPushButton#clearSearchButton, QPushButton#clearFormButton, QPushButton#clearBarcodeFieldsButton, QPushButton#historyClearFieldsButton { background: transparent; border: 1px solid #444444; border-radius: 6px; color: #969696; padding: 7px 14px; }
QPushButton#clearSearchButton:hover, QPushButton#clearFormButton:hover, QPushButton#clearBarcodeFieldsButton:hover, QPushButton#historyClearFieldsButton:hover { background: #202020; color: #d0d0d0; border-color: #5a5a5a; }
QPushButton#clearSearchButton:pressed, QPushButton#clearFormButton:pressed, QPushButton#clearBarcodeFieldsButton:pressed, QPushButton#historyClearFieldsButton:pressed { background: #181818; }
QTabWidget::pane { border: 1px solid #383838; border-radius: 9px; top: -1px; background: #1a1a1a; }
QTabBar::tab { background: transparent; color: #969696; padding: 7px 15px; border-top-left-radius: 7px; border-top-right-radius: 7px; margin-right: 2px; border: 1px solid transparent; font-weight: 600; font-size: 9.5pt; }
QTabBar::tab:selected { background: #1a1a1a; color: #ff7c38; border: 1px solid #383838; border-top: 2px solid #ff7c38; border-bottom-color: #1a1a1a; font-weight: 700; }
QTabBar::tab:hover:!selected { background: #202020; color: #cccccc; }
QMenuBar { background-color: #141414; color: #eaeaea; border-bottom: 1px solid #383838; }
QMenuBar::item { background: transparent; padding: 5px 10px; border-radius: 5px; }
QMenuBar::item:selected { background: #202020; }
QMenu { background-color: #1a1a1a; color: #eaeaea; border: 1px solid #383838; border-radius: 8px; padding: 4px 0; }
QMenu::item { padding: 5px 18px; border-radius: 4px; margin: 1px 4px; }
QMenu::item:selected { background: #2d2d2d; color: #ff9a5e; }
QMenu::separator { height: 1px; background: #383838; margin: 4px 8px; }
QStatusBar { background-color: #141414; border-top: 1px solid #383838; color: #6e6e6e; font-size: 9pt; }
QToolTip { background-color: #262626; color: #ececec; border: 1px solid #6a4a30; border-radius: 5px; padding: 5px 9px; font-size: 9pt; }
QScrollArea { background-color: transparent; border: none; }
QScrollArea#latestSkuScrollArea { background-color: #141414; border: 1px solid #383838; border-radius: 9px; }
QWidget#latestSkuContainer { background-color: #141414; }
QScrollBar:vertical { background: transparent; border: none; width: 8px; margin: 4px 2px; }
QScrollBar:horizontal { background: transparent; border: none; height: 8px; margin: 2px 4px; }
QScrollBar::handle:vertical, QScrollBar::handle:horizontal { background: #383838; border-radius: 4px; min-height: 24px; min-width: 24px; }
QScrollBar::handle:vertical:hover, QScrollBar::handle:horizontal:hover { background: #565656; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QLabel#searchImagePreviewLabel, QLabel#barcodePreviewLabel, QLabel#barcodeSkuImageLabel, QLabel#historySkuImageLabel, QLabel#companyLogoLabel { background-color: #1a1a1a; border: 1px solid #383838; border-radius: 8px; padding: 4px; }
QLabel#softwareNameLabel { color: #ff7c38; font-size: 18px; font-weight: 700; }
QLabel#companyNameLabel { color: #eaeaea; font-weight: 700; }
QLabel#authorLabel { color: #ff7c38; font-style: italic; }
QFrame#metricTile, QFrame#skuCard { background-color: #1a1a1a; border: 1px solid #383838; border-radius: 10px; }
QFrame#skuCard:hover { border-color: #ff7c38; background-color: #202020; }
QLabel#metricTileTitle { color: #969696; font-size: 9pt; font-weight: 600; }
QLabel#totalSkusValueLabel, QLabel#totalBarcodesValueLabel, QLabel#quarterBarcodesValueLabel, QLabel#sdSerialsValueLabel, QLabel#skSerialsValueLabel, QLabel#smSerialsValueLabel { font-size: 20px; font-weight: 700; }
QLabel#totalSkusValueLabel, QLabel#skuCardSku { color: #ff7c38; }
QLabel#totalBarcodesValueLabel { color: #ff7c38; }
QLabel#sdSerialsValueLabel { color: #44c492; }
QLabel#skSerialsValueLabel { color: #ffaa55; }
QLabel#smSerialsValueLabel { color: #f06060; }
QLabel#skuCardImage { background-color: #141414; border: 1px solid #383838; border-radius: 6px; }
QLabel#skuCardName { color: #eaeaea; font-weight: 600; }
QLabel#skuCardMeta { color: #969696; }
QLabel#skuCardDate { color: #454545; }
QLabel#searchNotFoundLabel { color: #454545; font-size: 13px; font-weight: 600; background: transparent; }
QCheckBox, QRadioButton { spacing: 6px; }
QCheckBox::indicator, QRadioButton::indicator { width: 15px; height: 15px; border: 1px solid #4a4a4a; background: #202020; border-radius: 4px; }
QRadioButton::indicator { border-radius: 8px; }
QCheckBox::indicator:hover, QRadioButton::indicator:hover { border-color: #ff7c38; }
QCheckBox::indicator:checked, QRadioButton::indicator:checked { background: #ff7c38; border-color: #ff7c38; }
QMessageBox, QInputDialog { background-color: #1e1e1e; }
QMessageBox QLabel, QInputDialog QLabel { color: #ececec; }
QSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::up-button, QDoubleSpinBox::down-button, QDateEdit::up-button, QDateEdit::down-button { background: #2a2a2a; border: none; width: 16px; }
QSpinBox::up-button:hover, QSpinBox::down-button:hover, QDoubleSpinBox::up-button:hover, QDoubleSpinBox::down-button:hover, QDateEdit::up-button:hover, QDateEdit::down-button:hover { background: #3d3d3d; }
QFrame#metricTile:hover { border-color: #4f4f4f; }
QLabel#toastSuccessLabel { background-color: #10281a; color: #52d68a; border: 1px solid #2f7d4a; border-radius: 9px; padding: 11px 18px; font-weight: 600; font-size: 10.5pt; }
QLabel#toastErrorLabel { background-color: #2b1214; color: #ff7a7a; border: 1px solid #8a2a2a; border-radius: 9px; padding: 11px 18px; font-weight: 600; font-size: 10.5pt; }
)");
    m_darkStyleSheet = darkTheme;

    m_mainTabs = ui->mainTabWidget;

    m_backupDbButton = ui->backupDbButton;
    m_totalSkusValueLabel = ui->totalSkusValueLabel;
    m_totalBarcodesValueLabel = ui->totalBarcodesValueLabel;
    m_quarterBarcodesValueLabel = ui->quarterBarcodesValueLabel;
    if (ui->metricsGridLayout && ui->metricsGroupBox) {
        while (QLayoutItem *item = ui->metricsGridLayout->takeAt(0)) {
            if (item->widget()) {
                item->widget()->deleteLater();
            }
            delete item;
        }
        ui->metricsGridLayout->setContentsMargins(10, 10, 10, 10);
        ui->metricsGridLayout->setHorizontalSpacing(12);
        ui->metricsGridLayout->setVerticalSpacing(10);
        for (int col = 0; col < 6; ++col) {
            ui->metricsGridLayout->setColumnStretch(col, 1);
        }

        auto createMetricTile = [&](const QString &titleText, const QString &valueObjectName, QLabel **valueTarget) {
            auto *tile = new QFrame(ui->metricsGroupBox);
            tile->setObjectName("metricTile");
            auto *tileLayout = new QVBoxLayout(tile);
            tileLayout->setContentsMargins(12, 10, 12, 10);
            tileLayout->setSpacing(4);

            auto *titleLabel = new QLabel(titleText, tile);
            titleLabel->setObjectName("metricTileTitle");
            titleLabel->setWordWrap(true);
            titleLabel->setMinimumWidth(0);

            auto *valueLabel = new QLabel("0", tile);
            valueLabel->setObjectName(valueObjectName);

            tileLayout->addWidget(titleLabel);
            tileLayout->addWidget(valueLabel);
            tileLayout->addStretch(1);
            if (valueTarget) {
                *valueTarget = valueLabel;
            }
            return tile;
        };

        struct MetricTileDef {
            QString title;
            QString valueObjectName;
            QLabel **target;
        };

        const MetricTileDef tiles[] = {
            { "Total SKUs", "totalSkusValueLabel", &m_totalSkusValueLabel },
            { "Total Serial Numbers", "totalBarcodesValueLabel", &m_totalBarcodesValueLabel },
            { "This Quarter Incoming", "quarterBarcodesValueLabel", &m_quarterBarcodesValueLabel },
            { "Skylark Drones Serials", "sdSerialsValueLabel", &m_sdSerialsValueLabel },
            { "Skykart Serials", "skSerialsValueLabel", &m_skSerialsValueLabel },
            { "Skylark Drones Manufacturing Pvt. Ltd. Serials", "smSerialsValueLabel", &m_smSerialsValueLabel }
        };

        for (int i = 0; i < 6; ++i) {
            QFrame *tile = createMetricTile(tiles[i].title, tiles[i].valueObjectName, tiles[i].target);
            ui->metricsGridLayout->addWidget(tile, 0, i);
        }
    }
    m_companyLogoLabel = ui->companyLogoLabel;
    m_companyNameLabel = ui->companyNameLabel;
    m_softwareNameLabel = ui->softwareNameLabel;
    m_authorLabel = ui->authorLabel;
    m_historySkuCombo = ui->historySkuComboBox;
    m_historySerialSearchField = ui->historySerialSearchLineEdit;
    m_historyRefreshButton = ui->historyRefreshButton;
    m_historyEditButton = ui->historyEditButton;
    m_historyDeleteButton = ui->historyDeleteButton;
    m_historyClearFieldsButton = ui->historyClearFieldsButton;
    m_historyTableView = ui->historyTableView;
    m_historySkuImageLabel = ui->historySkuImageLabel;
    m_historyPartNameValueLabel = ui->historySkuPartNameValueLabel;
    m_historyPartNumberValueLabel = ui->historySkuPartNumberValueLabel;
    m_historyBarcodeValueLabel = ui->historyBarcodeValueLabel;
    m_historySerialValueLabel = ui->historySerialValueLabel;
    m_historyDateValueLabel = ui->historyDateValueLabel;
    m_historyQuarterValueLabel = ui->historyQuarterValueLabel;
    m_historyYearValueLabel = ui->historyYearValueLabel;
    m_historyModel = new QStandardItemModel(this);
    m_latestSkuScrollArea = ui->latestSkuScrollArea;
    m_latestSkuContainer = ui->latestSkuContainer;
    m_latestSkuGridLayout = ui->latestSkuGridLayout;
    if (m_latestSkuScrollArea) {
        m_latestSkuScrollArea->installEventFilter(this);
        if (m_latestSkuScrollArea->viewport()) {
            m_latestSkuScrollArea->viewport()->installEventFilter(this);
        }
        if (!m_latestSkuRefreshTimer) {
            m_latestSkuRefreshTimer = new QTimer(this);
            m_latestSkuRefreshTimer->setSingleShot(true);
            connect(m_latestSkuRefreshTimer, &QTimer::timeout, this, &MainWindow::refreshLatestSkuCards);
        }
    }

    if (ui->skuLeftLayout && ui->searchGroupBox && ui->searchImageGroupBox) {
        ui->skuLeftLayout->removeWidget(ui->searchGroupBox);
        ui->skuLeftLayout->removeWidget(ui->searchImageGroupBox);

        auto *searchTopLayout = new QHBoxLayout();
        searchTopLayout->setContentsMargins(0, 0, 0, 0);
        searchTopLayout->setSpacing(8);

        ui->searchImageGroupBox->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        ui->searchImageGroupBox->setMinimumWidth(260);
        ui->searchImageGroupBox->setMaximumWidth(300);

        searchTopLayout->addWidget(ui->searchImageGroupBox, 0);
        searchTopLayout->addWidget(ui->searchGroupBox, 1);
        ui->skuLeftLayout->insertLayout(0, searchTopLayout);

        const int tableIndex = ui->skuLeftLayout->indexOf(ui->resultsTableView);
        if (tableIndex >= 0) {
            ui->skuLeftLayout->setStretch(tableIndex, 1);
        }
    }

    m_searchQuantityWordsValueLabel = ui->searchQuantityWordsValueLabel;

    m_searchSku = ui->searchSkuLineEdit;
    m_searchPartNumber = ui->searchPartNumberLineEdit;
    m_searchPartName = ui->searchPartNameLineEdit;
    m_searchButton = ui->searchButton;
    m_clearSearchButton = ui->clearSearchButton;
    m_fillFromSearchButton = ui->fillFromSearchButton;

    m_resultsView = ui->resultsTableView;
    m_resultsModel = new QStandardItemModel(this);
    m_resultsView->setModel(m_resultsModel);
    m_resultsView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_resultsView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_resultsView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_resultsView->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_resultsView->horizontalHeader()->setStretchLastSection(true);
    m_resultsView->horizontalHeader()->setMinimumSectionSize(40);
    m_resultsView->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_resultsView->setContextMenuPolicy(Qt::CustomContextMenu);

    // Overlay label shown when no search results are found.
    m_searchNotFoundLabel = new QLabel("No SKU / part found.", m_resultsView->viewport());
    m_searchNotFoundLabel->setObjectName("searchNotFoundLabel");
    m_searchNotFoundLabel->setAlignment(Qt::AlignCenter);
    m_searchNotFoundLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_searchNotFoundLabel->hide();

    m_statusLabel = ui->statusLabel;
    if (m_statusLabel) {
        m_statusLabel->setWordWrap(true);
        m_statusLabel->hide();
    }
    if (ui->statusbar) {
        m_statusBarMessageLabel = new QLabel(ui->statusbar);
        m_statusBarMessageLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        m_statusBarMessageLabel->setText(QString());
        m_statusBarMessageLabel->setStyleSheet("QLabel { padding-left: 4px; }");

        m_statusBarVersionLabel = new QLabel(ui->statusbar);
        m_statusBarVersionLabel->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
        const QString versionText = QCoreApplication::applicationVersion().trimmed().isEmpty()
                                        ? AppGlobals::appVersion()
                                        : QCoreApplication::applicationVersion().trimmed();
        m_statusBarVersionLabel->setText(QString("Version: %1").arg(versionText));
        m_statusBarVersionLabel->setStyleSheet("QLabel { color: #607d8b; padding-right: 6px; font-weight: 600; }");

        ui->statusbar->addWidget(m_statusBarMessageLabel, 1);
        ui->statusbar->addPermanentWidget(m_statusBarVersionLabel);
    }

    m_imagePreview = ui->searchImagePreviewLabel;
    m_imagePreview->setFixedSize(compactUi ? 176 : 220, compactUi ? 176 : 220);
    m_imagePreview->setFrameShape(QFrame::StyledPanel);
    m_imagePreview->setAlignment(Qt::AlignCenter);

    m_skuField = ui->skuLineEdit;
    m_partNameField = ui->partNameLineEdit;
    m_partNumberField = ui->partNumberLineEdit;
    m_categoryCombo = ui->categoryComboBox;
    m_subCategoryCombo = ui->subCategoryComboBox;
    m_itemSerialSpin = ui->itemSerialSpinBox;
    m_variationSpin = ui->variationSpinBox;
    m_dimensionsField = ui->dimensionsLineEdit;
    m_weightField = ui->weightLineEdit;
    m_descriptionEdit = ui->descriptionPlainTextEdit;
    m_storageField = ui->storageLineEdit;
    m_rackNumberField = ui->rackNumberLineEdit;
    m_binNumberField = ui->binNumberLineEdit;
    m_productFamilyField = ui->productFamilyLineEdit;
    m_imagePathField = ui->imagePathLineEdit;
    m_browseImageButton = ui->browseImageButton;
    m_commentsEdit = ui->commentsPlainTextEdit;
    m_clearFormButton = ui->clearFormButton;
    m_saveButton = ui->saveButton;
    m_updateButton = ui->updateButton;
    m_deleteSkuButton = ui->deleteSkuButton;

    m_barcodeSkuCombo = ui->barcodeSkuComboBox;
    m_barcodePrefixCombo = ui->barcodePrefixComboBox;
    m_barcodeQuantityField = ui->barcodeQuantityLineEdit;
    m_barcodeNextSerialField = ui->barcodeNextSerialLineEdit;
    m_barcodeValueField = ui->barcodeValueLineEdit;
    m_barcodeQuarterCombo = ui->barcodeQuarterComboBox;
    m_barcodeYearCombo = ui->barcodeYearComboBox;
    m_barcodePreview = ui->barcodePreviewLabel;
    m_generateBarcodeButton = ui->generateBarcodeButton;
    m_clearBarcodeFieldsButton = ui->clearBarcodeFieldsButton;
    m_printBarcodeButton = ui->printBarcodeButton;
    m_deleteBarcodeButton = ui->deleteBarcodeButton;
    m_barcodeTableView = ui->barcodeTableView;
    m_barcodeModel = new QStandardItemModel(this);
    m_barcodePartNameValueLabel = ui->barcodePartNameValueLabel;
    m_barcodePartNumberValueLabel = ui->barcodePartNumberValueLabel;
    m_barcodeQuantityWordsValueLabel = ui->barcodeQuantityWordsValueLabel;
    m_barcodeSkuImageLabel = ui->barcodeSkuImageLabel;
    m_barcodeSkuDetailsGroupBox = ui->barcodeSkuDetailsGroupBox;

    m_skuField->setReadOnly(false);
    m_skuField->setAlignment(Qt::AlignCenter);
    m_skuField->setStyleSheet("QLineEdit { font-size: 18px; font-weight: 700; color: #ff9800; border: 1px solid #2a3544; }");
    m_itemSerialSpin->setRange(1, 999);
    m_variationSpin->setRange(1, 35);
    m_descriptionEdit->setFixedHeight(60);
    m_commentsEdit->setFixedHeight(60);
    if (m_dimensionsField) {
        m_dimensionsField->setPlaceholderText("L cm x W cm x H cm");
        m_dimensionsField->setToolTip("Format: 10 cm x 20 cm x 30 cm");
    }
    if (m_imagePathField) {
        m_imagePathField->setReadOnly(true);
        m_imagePathField->setPlaceholderText("Stored in database");
    }

    if (m_weightField) {
        auto *weightValidator = new QDoubleValidator(0.0, 1000000.0, 3, m_weightField);
        weightValidator->setNotation(QDoubleValidator::StandardNotation);
        m_weightField->setValidator(weightValidator);
    }
    if (m_barcodeQuantityField) {
        auto *qtyValidator = new QIntValidator(1, 1000000, m_barcodeQuantityField);
        m_barcodeQuantityField->setValidator(qtyValidator);
        m_barcodeQuantityField->setText("1");
    }
    m_barcodeNextSerialField->setReadOnly(true);
    m_barcodeNextSerialField->setText("1");
    m_barcodeValueField->setReadOnly(true);
    m_barcodePreview->setFixedSize(compactUi ? 132 : 160, compactUi ? 132 : 160);
    m_barcodePreview->setFrameShape(QFrame::StyledPanel);
    m_barcodePreview->setAlignment(Qt::AlignCenter);
    m_barcodeSkuImageLabel->setFixedSize(compactUi ? 140 : 170, compactUi ? 140 : 170);
    m_barcodeSkuImageLabel->setFrameShape(QFrame::StyledPanel);
    m_barcodeSkuImageLabel->setAlignment(Qt::AlignCenter);
    if (m_barcodeSkuDetailsGroupBox) {
        m_barcodeSkuDetailsGroupBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
        m_barcodeSkuDetailsGroupBox->setMaximumHeight(compactUi ? 196 : 230);

        auto registerClickTarget = [this](QWidget *widget) {
            if (!widget) {
                return;
            }
            widget->installEventFilter(this);
        };

        registerClickTarget(m_barcodeSkuDetailsGroupBox);
        const QList<QWidget *> clickTargets = m_barcodeSkuDetailsGroupBox->findChildren<QWidget *>();
        for (QWidget *target : clickTargets) {
            registerClickTarget(target);
        }
    }
    m_historySkuImageLabel->setFixedSize(compactUi ? 148 : 180, compactUi ? 148 : 180);
    m_historySkuImageLabel->setFrameShape(QFrame::StyledPanel);
    m_historySkuImageLabel->setAlignment(Qt::AlignCenter);

    // ── TOOLTIPS ──────────────────────────────────────────────────────────────
    // QR Code Generation tab
    m_generateBarcodeButton->setToolTip(
        "<b>Generate QR Codes</b><br>"
        "Creates a batch of QR code labels for the selected SKU.<br>"
        "<i>Step 1:</i> Select a SKU from the dropdown.<br>"
        "<i>Step 2:</i> Choose the QR prefix (SD or SK) and quantity.<br>"
        "<i>Step 3:</i> Verify the Quarter/Year, then click this button.");
    m_printBarcodeButton->setToolTip(
        "<b>Export QR Codes (PDF)</b><br>"
        "Saves the last generated batch as a printable PDF label sheet.<br>"
        "Generate codes first, then click here to choose a save location.");
    m_deleteBarcodeButton->setToolTip(
        "<b>Delete Selected Serials</b><br>"
        "Permanently removes the selected serial records from this SKU.<br>"
        "<i>Warning:</i> This requires admin authorization and a written reason.");
    m_clearBarcodeFieldsButton->setToolTip(
        "<b>Clear Fields</b><br>"
        "Resets the QR generation form and clears the preview.<br>"
        "Use this when switching to a different SKU or starting a new batch.");

    // QR Code Manager tab
    m_historyRefreshButton->setToolTip(
        "<b>Load History</b><br>"
        "Loads all generated serial records for the selected SKU.<br>"
        "Select a SKU from the dropdown first, then click here.");
    m_historyEditButton->setToolTip(
        "<b>Edit Selected Serial</b><br>"
        "Opens an editor to change the serial number, quarter, year, or date.<br>"
        "<i>Warning:</i> Admin authorization and a written reason are required.");
    m_historyDeleteButton->setToolTip(
        "<b>Delete Selected Serials</b><br>"
        "Soft-deletes the selected rows from the serial history.<br>"
        "Select one or more rows first (hold Ctrl for multi-select).<br>"
        "<i>Warning:</i> Admin authorization and a written reason are required.");
    m_historyClearFieldsButton->setToolTip(
        "<b>Clear Fields</b><br>"
        "Resets the SKU selector, serial search filter, and detail panels.");

    // SKU Generation tab
    m_searchButton->setToolTip(
        "<b>Search</b><br>"
        "Searches the SKU catalog using the filters above.<br>"
        "Leave all fields blank to load the full catalog.");
    m_clearSearchButton->setToolTip(
        "<b>Clear Search</b><br>"
        "Clears all search filters and reloads the full results list.");
    m_fillFromSearchButton->setToolTip(
        "<b>Fill From Search</b><br>"
        "Loads the selected search result into the form below for editing.<br>"
        "Select a row in the results table first.");
    m_saveButton->setToolTip(
        "<b>Generate SKU and Update List</b><br>"
        "Saves a new SKU record using the values in the form.<br>"
        "<i>Required:</i> Part Name, Weight, Category, Sub-Category.");
    m_updateButton->setToolTip(
        "<b>Update Selected Record</b><br>"
        "Saves your edits to the currently loaded SKU record.<br>"
        "Use Fill From Search first to load a record, then edit and click here.");
    m_deleteSkuButton->setToolTip(
        "<b>Delete SKU</b><br>"
        "Soft-deletes this SKU and all its associated serial records.<br>"
        "<i>Warning:</i> Admin authorization and a written reason are required.");
    m_clearFormButton->setToolTip(
        "<b>Clear Forms</b><br>"
        "Clears all form fields and the image preview.<br>"
        "Use this before entering a brand new item.");
    m_browseImageButton->setToolTip(
        "<b>Browse Image</b><br>"
        "Select a product photo to attach to this SKU.<br>"
        "Supported formats: JPG, PNG, BMP, GIF.");

    // Dashboard
    m_backupDbButton->setToolTip(
        "<b>Backup DB</b><br>"
        "Exports a copy of the current database to a file you choose.<br>"
        "Run this before major batch operations or at end of day.");

    const auto enableClearButton = [](QLineEdit *field) {
        if (field) {
            field->setClearButtonEnabled(true);
        }
    };

    enableClearButton(m_searchSku);
    enableClearButton(m_searchPartNumber);
    enableClearButton(m_searchPartName);
    enableClearButton(m_skuField);
    enableClearButton(m_partNameField);
    enableClearButton(m_partNumberField);
    enableClearButton(m_dimensionsField);
    enableClearButton(m_weightField);
    enableClearButton(m_storageField);
    enableClearButton(m_rackNumberField);
    enableClearButton(m_binNumberField);
    enableClearButton(m_productFamilyField);
    enableClearButton(m_imagePathField);
    enableClearButton(m_barcodeNextSerialField);
    enableClearButton(m_barcodeValueField);
    enableClearButton(m_barcodeQuantityField);
    enableClearButton(m_historySerialSearchField);

    if (m_barcodeSkuCombo) {
        m_barcodeSkuCombo->setEditable(true);
        m_barcodeSkuCombo->setInsertPolicy(QComboBox::NoInsert);
        enableClearButton(m_barcodeSkuCombo->lineEdit());
    }
    if (m_historySkuCombo) {
        m_historySkuCombo->setEditable(true);
        m_historySkuCombo->setInsertPolicy(QComboBox::NoInsert);
        enableClearButton(m_historySkuCombo->lineEdit());
    }

    m_barcodeTableView->setModel(m_barcodeModel);
    m_barcodeTableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_barcodeTableView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_barcodeTableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_barcodeTableView->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_barcodeTableView->horizontalHeader()->setStretchLastSection(true);

    m_historyTableView->setModel(m_historyModel);
    m_historyTableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_historyTableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_historyTableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_historyTableView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_historyTableView->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_historyTableView->horizontalHeader()->setStretchLastSection(true);

    if (ui->dashboardLayout) {
        ui->dashboardLayout->setStretchFactor(ui->latestSkuGroupBox, 1);
    }
    if (m_latestSkuGridLayout) {
        m_latestSkuGridLayout->setContentsMargins(12, 12, 12, 12);
        m_latestSkuGridLayout->setHorizontalSpacing(16);
        m_latestSkuGridLayout->setVerticalSpacing(16);
        m_latestSkuGridLayout->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    }

    if (m_barcodeQuarterCombo) {
        m_barcodeQuarterCombo->clear();
        for (int quarter = 1; quarter <= 4; ++quarter) {
            m_barcodeQuarterCombo->addItem(QString("Q%1").arg(quarter), quarter);
        }
        const int defaultQuarter = currentQuarter();
        const int quarterIndex = m_barcodeQuarterCombo->findData(defaultQuarter);
        if (quarterIndex >= 0) {
            m_barcodeQuarterCombo->setCurrentIndex(quarterIndex);
        }
    }
    if (m_barcodeYearCombo) {
        m_barcodeYearCombo->clear();
        for (int year = 2020; year <= 2030; ++year) {
            m_barcodeYearCombo->addItem(QString::number(year), year);
        }
        const int defaultYear = currentYear();
        int yearIndex = m_barcodeYearCombo->findData(defaultYear);
        if (yearIndex < 0) {
            yearIndex = (defaultYear < 2020) ? 0 : (m_barcodeYearCombo->count() - 1);
        }
        if (yearIndex >= 0) {
            m_barcodeYearCombo->setCurrentIndex(yearIndex);
        }
    }
    if (m_barcodePrefixCombo) {
        m_barcodePrefixCombo->clear();
        m_barcodePrefixCombo->addItem("SD - Skylark Drones", "SD");
        m_barcodePrefixCombo->addItem("SK - Skykart", "SK");
        m_barcodePrefixCombo->addItem("SM - SDMPL (Skylark Drones Manufacturing Private Limited)", "SM");
        const int prefixIndex = m_barcodePrefixCombo->findData("SK");
        if (prefixIndex >= 0) {
            m_barcodePrefixCombo->setCurrentIndex(prefixIndex);
        }
        m_barcodePrefixCombo->setToolTip("First two characters used in QR code values.");
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList iconCandidates = {
        QDir(appDir).filePath("Assets/Application Desktop Icon.png"),
        QDir(appDir).filePath("../Assets/Application Desktop Icon.png"),
        QDir(appDir).filePath("../../Assets/Application Desktop Icon.png"),
        QDir::current().filePath("Assets/Application Desktop Icon.png"),
        QDir(appDir).filePath("Assets/logo.png"),
        QDir(appDir).filePath("../Assets/logo.png"),
        QDir(appDir).filePath("../../Assets/logo.png"),
        QDir::current().filePath("Assets/logo.png"),
        QString(":/assets/logo.png")
    };
    for (const auto &candidate : iconCandidates) {
        if (QFile::exists(candidate)) {
            QIcon appIcon(candidate);
            if (!appIcon.isNull()) {
                setWindowIcon(appIcon);
                if (qApp) {
                    qApp->setWindowIcon(appIcon);
                }
                break;
            }
        }
    }

    const QStringList logoCandidates = {
        QDir(appDir).filePath("Assets/logo.png"),
        QDir(appDir).filePath("../Assets/logo.png"),
        QDir(appDir).filePath("../../Assets/logo.png"),
        QDir::current().filePath("Assets/logo.png"),
        QDir(appDir).filePath("Assets/skylark_Drones_logo.png"),
        QDir(appDir).filePath("Assets/skylark Drones logo.png"),
        QString(":/assets/logo.png")
    };
    QString logoPath;
    for (const auto &candidate : logoCandidates) {
        if (QFile::exists(candidate)) {
            logoPath = candidate;
            break;
        }
    }
    m_companyLogoLabel->setFixedSize(80, 80);
    if (!logoPath.isEmpty()) {
        QPixmap logo(logoPath);
        if (!logo.isNull()) {
            m_companyLogoLabel->setPixmap(logo.scaled(m_companyLogoLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        } else {
            m_companyLogoLabel->setText("Logo");
        }
    } else {
        m_companyLogoLabel->setText("Logo");
    }
    m_companyLogoLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    if (m_softwareNameLabel) {
        m_softwareNameLabel->setText("Warehouse SKU and QR Code Manager");
        QFont font = m_softwareNameLabel->font();
        font.setPointSize(14);
        font.setBold(true);
        m_softwareNameLabel->setFont(font);
    }
    if (m_companyNameLabel) {
        m_companyNameLabel->setText("Skylark Drones Pvt. Ltd");
        QFont font = m_companyNameLabel->font();
        font.setPointSize(11);
        font.setBold(true);
        m_companyNameLabel->setFont(font);
    }
    if (m_authorLabel) {
        m_authorLabel->setText("Author: Atif Iqbal");
        QFont font = m_authorLabel->font();
        font.setPointSize(10);
        font.setItalic(true);
        m_authorLabel->setFont(font);
    }

    if (m_backupDbButton) {
        connect(m_backupDbButton, &QPushButton::clicked, this, &MainWindow::onBackupDbClicked);
    }

    connect(m_searchButton, &QPushButton::clicked, this, &MainWindow::searchRecords);
    connect(m_clearSearchButton, &QPushButton::clicked, this, &MainWindow::clearSearch);
    connect(m_fillFromSearchButton, &QPushButton::clicked, this, &MainWindow::fillFormFromSearch);

    // Live search: re-run on every keystroke so the table updates instantly.
    connect(m_searchSku, &QLineEdit::textChanged, this, &MainWindow::searchRecords);
    connect(m_searchPartNumber, &QLineEdit::textChanged, this, &MainWindow::searchRecords);
    connect(m_searchPartName, &QLineEdit::textChanged, this, &MainWindow::searchRecords);

    connect(m_clearFormButton, &QPushButton::clicked, this, &MainWindow::clearFormFields);
    connect(m_saveButton, &QPushButton::clicked, this, &MainWindow::saveForm);
    connect(m_updateButton, &QPushButton::clicked, this, &MainWindow::updateSelected);
    connect(m_deleteSkuButton, &QPushButton::clicked, this, &MainWindow::deleteSelectedSku);
    connect(m_browseImageButton, &QPushButton::clicked, this, &MainWindow::browseImage);
    connect(m_generateBarcodeButton, &QPushButton::clicked, this, &MainWindow::generateBarcodes);
    connect(m_clearBarcodeFieldsButton, &QPushButton::clicked, this, &MainWindow::clearBarcodeFields);
    connect(m_printBarcodeButton, &QPushButton::clicked, this, &MainWindow::printBarcodes);
    connect(m_deleteBarcodeButton, &QPushButton::clicked, this, &MainWindow::deleteSelectedBarcodes);
    connect(m_historyRefreshButton, &QPushButton::clicked, this, &MainWindow::loadHistoryForSelectedSku);
    connect(m_historyDeleteButton, &QPushButton::clicked, this, &MainWindow::deleteSelectedHistoryBarcodes);
    connect(m_historyEditButton, &QPushButton::clicked, this, &MainWindow::editSelectedHistoryBarcode);
    connect(m_historyClearFieldsButton, &QPushButton::clicked, this, &MainWindow::clearHistoryFields);

    connect(m_categoryCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::onCategoryChanged);
    connect(m_subCategoryCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::onSubCategoryChanged);
    connect(m_itemSerialSpin, qOverload<int>(&QSpinBox::valueChanged), this, &MainWindow::onSerialOrVariationChanged);
    connect(m_variationSpin, qOverload<int>(&QSpinBox::valueChanged), this, &MainWindow::onSerialOrVariationChanged);
    connect(m_resultsView->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &MainWindow::onSearchSelectionChanged);
    connect(m_barcodeSkuCombo, qOverload<int>(&QComboBox::activated), this, &MainWindow::onBarcodeSkuActivated);
    if (m_barcodeQuarterCombo) {
        connect(m_barcodeQuarterCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, &MainWindow::onBarcodePeriodChanged);
    }
    if (m_barcodeYearCombo) {
        connect(m_barcodeYearCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, &MainWindow::onBarcodePeriodChanged);
    }
    if (m_barcodeSkuCombo->lineEdit()) {
        connect(m_barcodeSkuCombo->lineEdit(), &QLineEdit::textChanged, this, &MainWindow::onBarcodeSkuInputChanged);
        connect(m_barcodeSkuCombo->lineEdit(), &QLineEdit::editingFinished, this, [this]() {
            const QString sku = extractSkuFromDisplay(m_barcodeSkuCombo->lineEdit()->text());
            if (sku.isEmpty()) {
                return;
            }
            const int index = m_barcodeSkuCombo->findData(sku);
            if (index >= 0) {
                m_barcodeSkuCombo->setCurrentIndex(index);
                onBarcodeSkuActivated(index);
            } else if (skuExists(sku)) {
                resetBarcodeFieldsForSkuChange();
                updateNextBarcodeSerial();
            }
        });
    }
    connect(m_skuField, &QLineEdit::editingFinished, this, &MainWindow::updateNextBarcodeSerial);
    connect(m_barcodeTableView->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &MainWindow::onBarcodeTableSelectionChanged);
    connect(m_historySkuCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::onHistorySkuChanged);
    if (m_historySkuCombo->lineEdit()) {
        connect(m_historySkuCombo->lineEdit(), &QLineEdit::textChanged, this, &MainWindow::onHistorySkuInputChanged);
        connect(m_historySkuCombo->lineEdit(), &QLineEdit::editingFinished, this, [this]() {
            const QString sku = extractSkuFromDisplay(m_historySkuCombo->lineEdit()->text());
            if (sku.isEmpty()) {
                return;
            }
            const int index = m_historySkuCombo->findData(sku);
            if (index >= 0) {
                m_historySkuCombo->setCurrentIndex(index);
            }
            loadHistoryForSelectedSku();
        });
    }
    if (m_historySerialSearchField) {
        connect(m_historySerialSearchField, &QLineEdit::textChanged, this, &MainWindow::onHistorySerialSearchChanged);
    }
    connect(m_historyTableView->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &MainWindow::onHistoryTableSelectionChanged);
    connect(m_resultsView, &QTableView::customContextMenuRequested, this, &MainWindow::showResultsContextMenu);
    connect(m_historyTableView, &QTableView::customContextMenuRequested, this, &MainWindow::showHistoryContextMenu);
    connect(m_mainTabs, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);

    createMenusAndToolbars();
    registerUiInteractionLogging();
    m_printSettings.load();
    applyDarkTheme();
    updateNoDbBanner();
}

void MainWindow::createMenusAndToolbars() {
    QMenu *fileMenu = menuBar()->addMenu("&File");
    m_actionLoadDb = fileMenu->addAction("Load DB...");
    m_actionRestoreBackup = fileMenu->addAction("Restore Backup...");
    m_actionExportDb = fileMenu->addAction("Export DB...");
    m_actionSaveDb = fileMenu->addAction("Save DB");
    fileMenu->addSeparator();
    m_actionExportSkuCsv = fileMenu->addAction("Export SKU Master (CSV)...");
    m_actionExportBarcodeSummaryCsv = fileMenu->addAction("Export QR Summary (CSV)...");
    m_actionExportSerialsXls = fileMenu->addAction("Export All Generated Serial Numbers (Excel)...");
    fileMenu->addSeparator();
    m_actionUninstall = fileMenu->addAction("Uninstall...");
    m_actionExit = fileMenu->addAction("Exit");

    QMenu *viewMenu = menuBar()->addMenu("&View");
    m_actionFullScreen = viewMenu->addAction("Full Screen");
    m_actionFullScreen->setCheckable(true);

    QMenu *optionsMenu = menuBar()->addMenu("&Options");
    m_actionPrintSettings = optionsMenu->addAction("QR Code / Sticker Settings...");

    QMenu *securityMenu = menuBar()->addMenu("&Security");
    m_actionManageUsers = securityMenu->addAction("User Access Control...");
    m_actionSwitchUser = securityMenu->addAction("Switch User...");
    m_actionSettings = m_actionManageUsers;

    QMenu *helpMenu = menuBar()->addMenu("&Help");
    m_actionHelpGuides = helpMenu->addAction("User and Technical Guides...");
    m_actionSkuReference = helpMenu->addAction("SKU Reference");
    m_actionSkuMap = helpMenu->addAction("SKU MAP");

    // Menu tooltips (menus only show them when explicitly enabled).
    for (QMenu *menu : { fileMenu, viewMenu, optionsMenu, securityMenu, helpMenu }) {
        menu->setToolTipsVisible(true);
    }
    m_actionLoadDb->setToolTip("Open an existing database file, or create a new one at a chosen location.");
    m_actionRestoreBackup->setToolTip("Replace the current database with a previously exported backup file.");
    m_actionExportDb->setToolTip("Save a copy of the current database to a file of your choice.");
    m_actionSaveDb->setToolTip("Flush all pending changes to the database file on disk.");
    m_actionExportSkuCsv->setToolTip("Export the SKU master list (SKU and Part Name) as a CSV file.");
    m_actionExportBarcodeSummaryCsv->setToolTip("Export a per-SKU summary of generated QR serials as a CSV file.");
    m_actionExportSerialsXls->setToolTip("Export every generated serial number to an Excel-compatible file.");
    m_actionUninstall->setToolTip("Launch the uninstaller to remove the application from this computer.");
    m_actionExit->setToolTip("Close the application.");
    m_actionFullScreen->setToolTip("Toggle full-screen mode (recommended on small laptop displays).");
    m_actionPrintSettings->setToolTip("Adjust QR sticker dimensions, margins, logo size and element positions.");
    m_actionManageUsers->setToolTip("Create, edit or remove user accounts and their permissions.");
    m_actionSwitchUser->setToolTip("Log out and sign in as a different user.");
    m_actionHelpGuides->setToolTip("Read the built-in user guide and technical documentation.");
    m_actionSkuReference->setToolTip("View the SKU numbering reference charts.");
    m_actionSkuMap->setToolTip("Open the A0 warehouse SKU map diagram (PDF).");

    connect(m_actionLoadDb, &QAction::triggered, this, &MainWindow::onLoadDbTriggered);
    connect(m_actionRestoreBackup, &QAction::triggered, this, &MainWindow::onRestoreBackupTriggered);
    connect(m_actionExportDb, &QAction::triggered, this, &MainWindow::onExportDbTriggered);
    connect(m_actionSaveDb, &QAction::triggered, this, &MainWindow::onSaveDbTriggered);
    if (m_actionExportSkuCsv) {
        connect(m_actionExportSkuCsv, &QAction::triggered, this, &MainWindow::exportSkuMasterCsv);
    }
    if (m_actionExportBarcodeSummaryCsv) {
        connect(m_actionExportBarcodeSummaryCsv, &QAction::triggered, this, &MainWindow::exportBarcodeSummaryCsv);
    }
    if (m_actionExportSerialsXls) {
        connect(m_actionExportSerialsXls, &QAction::triggered, this, &MainWindow::exportAllSerialsXls);
    }
    connect(m_actionUninstall, &QAction::triggered, this, &MainWindow::onUninstallTriggered);
    connect(m_actionExit, &QAction::triggered, this, &MainWindow::onExitTriggered);
    connect(m_actionFullScreen, &QAction::toggled, this, &MainWindow::onFullScreenToggled);
    connect(m_actionPrintSettings, &QAction::triggered, this, &MainWindow::onPrintSettingsTriggered);
    connect(m_actionManageUsers, &QAction::triggered, this, &MainWindow::onManageUsersTriggered);
    connect(m_actionSwitchUser, &QAction::triggered, this, &MainWindow::onSwitchUserTriggered);
    connect(m_actionHelpGuides, &QAction::triggered, this, &MainWindow::onHelpGuidesTriggered);
    connect(m_actionSkuReference, &QAction::triggered, this, &MainWindow::onSkuReferenceTriggered);
    if (m_actionSkuMap) {
        connect(m_actionSkuMap, &QAction::triggered, this, &MainWindow::onSkuMapTriggered);
    }
}

void MainWindow::syncRunLogIdentity() {
    setRunLogIdentity(m_currentUsername, m_currentUserId, m_currentRoleKey);
}

void MainWindow::appendRunLogWithUser(const QString &message) const {
    const QString username = m_currentUsername.trimmed().isEmpty() ? QStringLiteral("anonymous")
                                                                    : m_currentUsername.trimmed();
    const QString userId = m_currentUserId.trimmed().isEmpty() ? QStringLiteral("-")
                                                                : m_currentUserId.trimmed();
    const QString role = m_currentRoleKey.trimmed().isEmpty() ? QStringLiteral("unknown")
                                                               : m_currentRoleKey.trimmed();
    QString module = QStringLiteral("General");
    if (m_mainTabs) {
        const int current = m_mainTabs->currentIndex();
        if (current >= 0) {
            const QString tabText = m_mainTabs->tabText(current).trimmed();
            if (!tabText.isEmpty()) {
                module = tabText;
            }
        }
    }

    appendRunLog(QString("user=%1 user_id=%2 role=%3 module=%4 | %5")
                     .arg(username, userId, role, module, message.trimmed()));
}

void MainWindow::registerUiInteractionLogging() {
    auto wireButton = [this](QPushButton *button, const QString &name) {
        if (!button) {
            return;
        }
        connect(button, &QPushButton::clicked, this, [this, button, name]() {
            QString id = name.trimmed();
            if (id.isEmpty()) {
                id = button->objectName().trimmed();
            }
            QString label = button->text().trimmed();
            label.replace("&", "");
            appendRunLogWithUser(QString("pressed button=%1 label=\"%2\"").arg(id, label));
        });
    };

    auto wireAction = [this](QAction *action, const QString &name) {
        if (!action) {
            return;
        }
        connect(action, &QAction::triggered, this, [this, action, name](bool checked) {
            QString id = name.trimmed();
            if (id.isEmpty()) {
                id = action->objectName().trimmed();
            }
            QString label = action->text().trimmed();
            label.replace("&", "");
            QString detail = QString("triggered action=%1 label=\"%2\"").arg(id, label);
            if (action->isCheckable()) {
                detail += QString(" checked=%1").arg(checked ? "true" : "false");
            }
            appendRunLogWithUser(detail);
        });
    };

    wireButton(m_backupDbButton, "backupDbButton");
    wireButton(m_searchButton, "searchButton");
    wireButton(m_clearSearchButton, "clearSearchButton");
    wireButton(m_fillFromSearchButton, "fillFromSearchButton");
    wireButton(m_clearFormButton, "clearFormButton");
    wireButton(m_saveButton, "saveButton");
    wireButton(m_updateButton, "updateButton");
    wireButton(m_deleteSkuButton, "deleteSkuButton");
    wireButton(m_browseImageButton, "browseImageButton");
    wireButton(m_generateBarcodeButton, "generateBarcodeButton");
    wireButton(m_clearBarcodeFieldsButton, "clearBarcodeFieldsButton");
    wireButton(m_printBarcodeButton, "printBarcodeButton");
    wireButton(m_deleteBarcodeButton, "deleteBarcodeButton");
    wireButton(m_historyRefreshButton, "historyRefreshButton");
    wireButton(m_historyEditButton, "historyEditButton");
    wireButton(m_historyDeleteButton, "historyDeleteButton");
    wireButton(m_historyClearFieldsButton, "historyClearFieldsButton");

    wireAction(m_actionLoadDb, "actionLoadDb");
    wireAction(m_actionRestoreBackup, "actionRestoreBackup");
    wireAction(m_actionExportDb, "actionExportDb");
    wireAction(m_actionSaveDb, "actionSaveDb");
    wireAction(m_actionExportSkuCsv, "actionExportSkuCsv");
    wireAction(m_actionExportBarcodeSummaryCsv, "actionExportBarcodeSummaryCsv");
    wireAction(m_actionExportSerialsXls, "actionExportSerialsXls");
    wireAction(m_actionUninstall, "actionUninstall");
    wireAction(m_actionExit, "actionExit");
    wireAction(m_actionFullScreen, "actionFullScreen");
    wireAction(m_actionPrintSettings, "actionPrintSettings");
    wireAction(m_actionManageUsers, "actionManageUsers");
    wireAction(m_actionSwitchUser, "actionSwitchUser");
    wireAction(m_actionHelpGuides, "actionHelpGuides");
    wireAction(m_actionSkuReference, "actionSkuReference");
    wireAction(m_actionSkuMap, "actionSkuMap");

    if (m_mainTabs) {
        connect(m_mainTabs, &QTabWidget::currentChanged, this, [this](int index) {
            const QString tabName = (index >= 0) ? m_mainTabs->tabText(index).trimmed() : QString();
            appendRunLogWithUser(QString("switched_tab index=%1 name=\"%2\"")
                                     .arg(index)
                                     .arg(tabName.isEmpty() ? QStringLiteral("unknown") : tabName));
        });
    }
}

void MainWindow::applyDarkTheme() {
    // The app has a single fixed dark theme; there is no theme switcher.
    qApp->setStyleSheet(m_darkStyleSheet);

    const QString selectionStyle = tableSelectionStyleSheet();
    if (m_resultsView) {
        m_resultsView->setStyleSheet(selectionStyle);
    }
    if (m_historyTableView) {
        m_historyTableView->setStyleSheet(selectionStyle);
    }
    if (m_barcodeTableView) {
        m_barcodeTableView->setStyleSheet(selectionStyle);
    }
    if (m_skuField) {
        m_skuField->setStyleSheet(highlightedSkuFieldStyle());
    }
    if (m_statusBarMessageLabel) {
        m_statusBarMessageLabel->setStyleSheet("QLabel { padding-left: 4px; }");
    }
    if (m_statusBarVersionLabel) {
        m_statusBarVersionLabel->setStyleSheet(versionLabelStyle());
    }
}

bool MainWindow::selectDatabaseOnStartup() {
    QSettings settings;
    const QStringList args = QCoreApplication::arguments();
    const QString startupDbPath = commandLineValue(args, "--db-path");
    const bool suppressUacPrompt = hasCommandLineFlag(args, "--uac-db-relaunch");

    // Startup path resolution is intentionally deterministic: explicit CLI override,
    // then the last saved DB, and only then an interactive prompt.
    if (!startupDbPath.isEmpty()) {
        appendRunLog(QString("Startup override DB path requested: %1")
                         .arg(QDir::toNativeSeparators(startupDbPath)));
        if (openDatabaseAt(startupDbPath)) {
            settings.setValue("db/path", startupDbPath);
            return true;
        }
        if (!suppressUacPrompt &&
            isPermissionDeniedOpenError(m_lastDatabaseOpenError) &&
            requestUacElevationForDatabase(startupDbPath, m_lastDatabaseOpenError)) {
            return false;
        }
    }

    const QString savedPath = settings.value("db/path").toString().trimmed();
    if (!savedPath.isEmpty()) {
        if (QFileInfo::exists(savedPath)) {
            if (openDatabaseAt(savedPath)) {
                return true;
            }
            if (!suppressUacPrompt &&
                isPermissionDeniedOpenError(m_lastDatabaseOpenError) &&
                requestUacElevationForDatabase(savedPath, m_lastDatabaseOpenError)) {
                return false;
            }
        } else {
            appendRunLog(QString("Saved database path does not exist: %1")
                             .arg(QDir::toNativeSeparators(savedPath)));
        }

        settings.remove("db/path");
    }

    const QString startDir = dataDirPath();
    while (true) {
        QMessageBox prompt(this);
        prompt.setWindowTitle("Database Setup");
        prompt.setText("No database was found. Choose how to continue.");
        prompt.setInformativeText("Create a new database now, skip for this session, or exit.");
        QPushButton *createButton = prompt.addButton("Create New DB", QMessageBox::AcceptRole);
        QPushButton *skipButton = prompt.addButton("Skip This Time", QMessageBox::ActionRole);
        QPushButton *exitButton = prompt.addButton("Exit", QMessageBox::RejectRole);
        prompt.exec();

        QAbstractButton *clicked = prompt.clickedButton();
        if (!clicked || clicked == exitButton) {
            return false;
        }

        if (clicked == skipButton) {
            settings.remove("db/path");
            m_customDbPath.clear();
            m_lastDatabaseOpenError.clear();
            appendRunLog("Startup database setup skipped by user");
            return true;
        }

        if (clicked == createButton) {
            const QString folder = QFileDialog::getExistingDirectory(
                this,
                "Select Database Folder",
                startDir);
            if (folder.isEmpty()) {
                continue;
            }

            const QString file = QDir(folder).filePath(AppGlobals::dbFileName());
            if (!openDatabaseAt(file)) {
                if (!suppressUacPrompt &&
                    isPermissionDeniedOpenError(m_lastDatabaseOpenError) &&
                    requestUacElevationForDatabase(file, m_lastDatabaseOpenError)) {
                    return false;
                }
                QMessageBox::warning(this,
                                     "Database Error",
                                     "Unable to create or open the database in the selected folder.\n\nCheck permissions and try again.");
                continue;
            }

            settings.setValue("db/path", file);
            return true;
        }
    }
}

bool MainWindow::openDatabaseAt(const QString &path) {
    const QString trimmed = path.trimmed();
    m_lastDatabaseOpenError.clear();
    if (trimmed.isEmpty()) {
        m_lastDatabaseOpenError = "Database path is empty.";
        updateNoDbBanner();
        return false;
    }

    const QFileInfo dbInfo(trimmed);
    QDir dbDir(dbInfo.absolutePath());
    if (!dbDir.exists() && !dbDir.mkpath(".")) {
        m_lastDatabaseOpenError = QString("Failed to create database directory: %1")
                                      .arg(QDir::toNativeSeparators(dbDir.absolutePath()));
        setStatus(m_lastDatabaseOpenError, false);
        appendRunLog(m_lastDatabaseOpenError);
        updateNoDbBanner();
        return false;
    }

    if (m_db.isOpen()) {
        m_db.close();
    }

    // Reuse a single named connection so reloads/backups do not leak duplicate
    // SQLite handles inside the process.
    if (QSqlDatabase::contains("sku_connection")) {
        m_db = QSqlDatabase::database("sku_connection");
    } else {
        m_db = QSqlDatabase::addDatabase("QSQLITE", "sku_connection");
    }
    m_db.setDatabaseName(trimmed);

    if (!m_db.open()) {
        const QString errorText = m_db.lastError().text().trimmed();
        const QString message = errorText.isEmpty() ? "Failed to open database."
                                                    : QString("Failed to open database: %1").arg(errorText);
        m_lastDatabaseOpenError = errorText.isEmpty() ? message : errorText;
        setStatus(message, false);
        appendRunLog(QString("openDatabaseAt failed for %1 | %2")
                         .arg(QDir::toNativeSeparators(trimmed), m_lastDatabaseOpenError));
        updateNoDbBanner();
        return false;
    }

    m_customDbPath = trimmed;
    m_lastDatabaseOpenError.clear();
    QSettings settings;
    settings.setValue("db/path", trimmed);

    if (!ensureSchema()) {
        m_lastDatabaseOpenError = "Database schema check failed.";
        setStatus("Database schema check failed.", false);
        appendRunLog(QString("openDatabaseAt schema check failed for %1")
                         .arg(QDir::toNativeSeparators(trimmed)));
        updateNoDbBanner();
        return false;
    }

    if (!ensureDefaultRoles()) {
        m_lastDatabaseOpenError = "Role setup failed.";
        setStatus("Role setup failed.", false);
        appendRunLog(QString("openDatabaseAt role setup failed for %1")
                         .arg(QDir::toNativeSeparators(trimmed)));
        updateNoDbBanner();
        return false;
    }
    if (!ensureInitialAdmin()) {
        m_lastDatabaseOpenError = "Embedded developer account setup failed.";
        setStatus("Embedded developer account setup failed.", false);
        appendRunLog(QString("openDatabaseAt embedded developer account setup failed for %1")
                         .arg(QDir::toNativeSeparators(trimmed)));
        updateNoDbBanner();
        return false;
    }
    if (!loadRulesIfEmpty()) {
        m_lastDatabaseOpenError = "Unable to load SKU rules.";
        updateNoDbBanner();
        return false;
    }
    if (!loadCatalogIfEmpty()) {
        m_lastDatabaseOpenError = "Unable to load catalog.";
        updateNoDbBanner();
        return false;
    }

    loadCategories();
    onCategoryChanged();
    searchRecords();
    loadSkuList();
    loadHistorySkuList();
    updateDashboardMetrics();
    scheduleAutomatedBackups();

    if (!m_currentUsername.isEmpty()) {
        m_currentUsername.clear();
        m_currentUserId.clear();
        m_currentUserFullName.clear();
        applyAccessControl(AppGlobals::roleKeyView());
        if (!promptLogin()) {
            setStatus("Login required for loaded database.", false);
            QTimer::singleShot(0, this, &QWidget::close);
            updateNoDbBanner();
            return false;
        }
    }

    setStatus(QString("Loaded database: %1").arg(QDir::toNativeSeparators(trimmed)), true);
    appendRunLog(QString("openDatabaseAt succeeded for %1").arg(QDir::toNativeSeparators(trimmed)));
    updateNoDbBanner();
    return true;
}

QString MainWindow::currentDatabasePath() const {
    if (m_db.isOpen()) {
        return m_db.databaseName();
    }
    return dbPath();
}

bool MainWindow::backupDatabaseTo(const QString &targetPath) {
    const QString sourcePath = currentDatabasePath();
    if (sourcePath.isEmpty()) {
        setStatus("No database file to back up.", false);
        logAction("BACKUP_MANUAL",
                  targetPath,
                  QString(),
                  QString(),
                  "No database file to back up",
                  "Backup",
                  "BACKUP_RESTORE",
                  false,
                  "No database file to back up",
                  targetPath);
        return false;
    }
    if (!QFile::exists(sourcePath)) {
        setStatus("Database file not found.", false);
        logAction("BACKUP_MANUAL",
                  targetPath,
                  QString(),
                  QString(),
                  "Database file not found",
                  "Backup",
                  "BACKUP_RESTORE",
                  false,
                  "Database file not found",
                  targetPath);
        return false;
    }

    QString target = targetPath;
    if (target.isEmpty()) {
        return false;
    }

    QFileInfo sourceInfo(sourcePath);
    QFileInfo targetInfo(target);
    if (sourceInfo.exists() && targetInfo.exists()) {
        const QString sourceCanonical = sourceInfo.canonicalFilePath();
        const QString targetCanonical = targetInfo.canonicalFilePath();
        if (!sourceCanonical.isEmpty() && !targetCanonical.isEmpty() && sourceCanonical == targetCanonical) {
            setStatus("Backup target cannot be the same as the source database.", false);
            logAction("BACKUP_MANUAL",
                      target,
                      QString(),
                      QString(),
                      "Backup target cannot be the same as source database",
                      "Backup",
                      "BACKUP_RESTORE",
                      false,
                      "Backup target cannot be same as source",
                      target);
            return false;
        }
    }

    if (QFile::exists(target)) {
        QFile::remove(target);
    }

    const bool wasOpen = m_db.isOpen();
    if (wasOpen) {
        m_db.close();
    }

    const bool ok = QFile::copy(sourcePath, target);

    if (wasOpen) {
        m_db.setDatabaseName(sourcePath);
        m_db.open();
    }

    if (!ok) {
        setStatus("Failed to back up the database.", false);
        logAction("BACKUP_MANUAL",
                  target,
                  QString(),
                  QString(),
                  "Failed to copy database file",
                  "Backup",
                  "BACKUP_RESTORE",
                  false,
                  "Failed to copy database file",
                  target);
        return false;
    }

    setStatus(QString("Database backup saved: %1").arg(QDir::toNativeSeparators(target)), true);
    logAction("BACKUP_MANUAL",
              target,
              QString(),
              target,
              QString(),
              "Backup",
              "BACKUP_RESTORE",
              true,
              QString(),
              target);
    return true;
}

QString MainWindow::defaultBackupPath() const {
    const QString backupDir = configuredBackupRoot();
    QDir dir(backupDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    return dir.filePath(QString("sku_backup_%1.db").arg(stamp));
}

bool MainWindow::pruneBackupRetention(const QString &backupRoot, QString *errorOut) {
    QDir dir(backupRoot);
    if (!dir.exists()) {
        return true;
    }

    const QFileInfoList files = dir.entryInfoList(QStringList() << "*.encdb", QDir::Files, QDir::Time);
    if (files.isEmpty()) {
        return true;
    }

    const QDateTime now = QDateTime::currentDateTime();
    QSet<QString> keepPaths;
    QSet<QString> keptWeeks;
    QSet<QString> keptMonths;
    const QRegularExpression stampRegex("(\\d{8}_\\d{6})");

    struct BackupItem {
        QFileInfo fileInfo;
        QDateTime stamp;
    };
    QList<BackupItem> items;
    for (const QFileInfo &info : files) {
        const QRegularExpressionMatch m = stampRegex.match(info.fileName());
        if (!m.hasMatch()) {
            continue;
        }
        const QDateTime stamp = QDateTime::fromString(m.captured(1), "yyyyMMdd_HHmmss");
        if (!stamp.isValid()) {
            continue;
        }
        items.append({info, stamp});
    }
    std::sort(items.begin(), items.end(), [](const BackupItem &a, const BackupItem &b) {
        return a.stamp > b.stamp;
    });

    for (const BackupItem &item : items) {
        const int ageDays = item.stamp.date().daysTo(now.date());
        const QString absolutePath = item.fileInfo.absoluteFilePath();
        if (ageDays <= 30) {
            keepPaths.insert(absolutePath);
            continue;
        }

        int isoYear = 0;
        const int week = item.stamp.date().weekNumber(&isoYear);
        const QString weekKey = QString("%1-W%2")
                                    .arg(isoYear)
                                    .arg(week, 2, 10, QLatin1Char('0'));
        const QString monthKey = item.stamp.date().toString("yyyy-MM");

        bool keep = false;
        if (!keptWeeks.contains(weekKey)) {
            keptWeeks.insert(weekKey);
            keep = true;
        }
        if (!keptMonths.contains(monthKey)) {
            keptMonths.insert(monthKey);
            keep = true;
        }
        if (keep) {
            keepPaths.insert(absolutePath);
        }
    }

    for (const BackupItem &item : items) {
        const QString absolutePath = item.fileInfo.absoluteFilePath();
        if (keepPaths.contains(absolutePath)) {
            continue;
        }
        if (!QFile::remove(absolutePath)) {
            if (errorOut) {
                *errorOut = QString("Failed to prune old backup: %1").arg(absolutePath);
            }
            return false;
        }
    }
    return true;
}

bool MainWindow::shouldRunBootFallback() const {
    QSqlQuery q(m_db);
    q.prepare("SELECT value FROM app_meta WHERE key = 'backup_last_success_date'");
    if (!q.exec() || !q.next()) {
        return true;
    }
    const QString stored = q.value(0).toString().trimmed();
    if (stored.isEmpty()) {
        return true;
    }
    const QDate lastDate = QDate::fromString(stored, Qt::ISODate);
    if (!lastDate.isValid()) {
        return true;
    }

    const QDate today = QDate::currentDate();
    if (lastDate < today.addDays(-1)) {
        return true;
    }
    if (lastDate < today && QTime::currentTime() > QTime(19, 30)) {
        return true;
    }
    return false;
}

bool MainWindow::runAutomatedBackup(const QString &trigger, bool silent) {
    // Backups run against the database file path rather than the open SQLite handle so
    // scheduled jobs keep working even when no operator has the backup screen open.
    const QString sourcePath = currentDatabasePath();
    if (sourcePath.isEmpty() || !QFile::exists(sourcePath)) {
        const QString msg = "Automated backup skipped: source database unavailable.";
        if (!silent) {
            setStatus(msg, false);
        }
        logAction("AUTO_BACKUP",
                  trigger,
                  QString(),
                  QString(),
                  msg,
                  "Backup",
                  "BACKUP_RESTORE",
                  false,
                  msg,
                  sourcePath);
        return false;
    }

    QString classification = "daily";
    const QDate today = QDate::currentDate();
    if (today.day() == 1) {
        classification = "monthly";
    } else if (today.dayOfWeek() == 7) {
        classification = "weekly";
    }

    const QString backupPath = automatedBackupFilePath(classification);
    const bool wasOpen = m_db.isOpen();
    if (wasOpen) {
        m_db.close();
    }

    QString checksum;
    qint64 fileSize = 0;
    QString metadata;
    QString error;
    const bool ok = createEncryptedBackup(sourcePath, backupPath, &checksum, &fileSize, &metadata, &error);

    if (wasOpen) {
        m_db.setDatabaseName(sourcePath);
        m_db.open();
    }

    QSqlQuery history(m_db);
    history.prepare(
        "INSERT INTO app_backup_history (timestamp, trigger_type, backup_path, checksum, file_size, metadata, result, error_message) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    history.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    history.addBindValue(trigger);
    history.addBindValue(backupPath);
    history.addBindValue(checksum);
    history.addBindValue(fileSize);
    history.addBindValue(metadata);
    history.addBindValue(ok ? "SUCCESS" : "FAIL");
    history.addBindValue(error);
    history.exec();

    QSqlQuery setMeta(m_db);
    setMeta.prepare("INSERT OR REPLACE INTO app_meta (key, value) VALUES (?, ?)");
    setMeta.addBindValue("backup_last_attempt_ts");
    setMeta.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    setMeta.exec();

    if (ok) {
        setMeta.prepare("INSERT OR REPLACE INTO app_meta (key, value) VALUES (?, ?)");
        setMeta.addBindValue("backup_last_success_ts");
        setMeta.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
        setMeta.exec();

        setMeta.prepare("INSERT OR REPLACE INTO app_meta (key, value) VALUES (?, ?)");
        setMeta.addBindValue("backup_last_success_date");
        setMeta.addBindValue(QDate::currentDate().toString(Qt::ISODate));
        setMeta.exec();

        QString retentionError;
        if (!pruneBackupRetention(configuredBackupRoot(), &retentionError)) {
            appendRunLog(retentionError);
        }
    }

    if (!silent) {
        if (ok) {
            setStatus(QString("Automated backup saved: %1").arg(QDir::toNativeSeparators(backupPath)), true);
        } else {
            setStatus(QString("Automated backup failed: %1").arg(error), false);
        }
    }

    logAction("AUTO_BACKUP",
              trigger,
              QString(),
              backupPath,
              ok ? QString() : error,
              "Backup",
              "BACKUP_RESTORE",
              ok,
              error,
              backupPath);
    return ok;
}

void MainWindow::scheduleNextBackupTick() {
    if (!m_autoBackupTimer) {
        return;
    }
    QDateTime next = QDateTime(QDate::currentDate().addDays(1), QTime(17, 30));
    qint64 msecs = QDateTime::currentDateTime().msecsTo(next);
    if (msecs < 1000) {
        msecs = 1000;
    }
    if (msecs > 24ll * 60ll * 60ll * 1000ll) {
        msecs = 24ll * 60ll * 60ll * 1000ll;
    }
    m_autoBackupTimer->start(static_cast<int>(msecs));
}

void MainWindow::evaluateAutomatedBackupWindow() {
    if (!m_db.isOpen()) {
        scheduleNextBackupTick();
        return;
    }

    if (shouldRunBootFallback()) {
        runAutomatedBackup("boot_fallback", true);
    }

    QSqlQuery readMeta(m_db);
    readMeta.prepare("SELECT value FROM app_meta WHERE key = 'backup_last_success_date'");
    QString lastDateValue;
    if (readMeta.exec() && readMeta.next()) {
        lastDateValue = readMeta.value(0).toString();
    }
    const QDate today = QDate::currentDate();
    const QDate lastDate = QDate::fromString(lastDateValue, Qt::ISODate);
    if (lastDate.isValid() && lastDate == today) {
        scheduleNextBackupTick();
        return;
    }

    QString scheduleDateValue;
    int scheduleOffset = -1;
    readMeta.prepare("SELECT value FROM app_meta WHERE key = 'backup_schedule_date'");
    if (readMeta.exec() && readMeta.next()) {
        scheduleDateValue = readMeta.value(0).toString();
    }
    readMeta.prepare("SELECT value FROM app_meta WHERE key = 'backup_schedule_offset_min'");
    if (readMeta.exec() && readMeta.next()) {
        scheduleOffset = readMeta.value(0).toInt();
    }

    if (scheduleDateValue != today.toString(Qt::ISODate) || scheduleOffset < 0 || scheduleOffset > 120) {
        scheduleOffset = QRandomGenerator::global()->bounded(121);
        QSqlQuery setMeta(m_db);
        setMeta.prepare("INSERT OR REPLACE INTO app_meta (key, value) VALUES (?, ?)");
        setMeta.addBindValue("backup_schedule_date");
        setMeta.addBindValue(today.toString(Qt::ISODate));
        setMeta.exec();
        setMeta.prepare("INSERT OR REPLACE INTO app_meta (key, value) VALUES (?, ?)");
        setMeta.addBindValue("backup_schedule_offset_min");
        setMeta.addBindValue(QString::number(scheduleOffset));
        setMeta.exec();
    }

    const QDateTime windowStart(today, QTime(17, 30));
    const QDateTime scheduleAt = windowStart.addSecs(scheduleOffset * 60);
    const QDateTime windowEnd(today, QTime(19, 30));
    const QDateTime now = QDateTime::currentDateTime();

    if (now < scheduleAt) {
        qint64 waitMs = now.msecsTo(scheduleAt);
        if (waitMs < 1000) {
            waitMs = 1000;
        }
        m_autoBackupTimer->start(static_cast<int>(waitMs));
        return;
    }
    if (now >= scheduleAt && now <= windowEnd) {
        runAutomatedBackup("daily_window", true);
        scheduleNextBackupTick();
        return;
    }
    if (now > windowEnd) {
        runAutomatedBackup("missed_window_fallback", true);
        scheduleNextBackupTick();
        return;
    }
    scheduleNextBackupTick();
}

void MainWindow::scheduleAutomatedBackups() {
    if (!m_autoBackupTimer) {
        m_autoBackupTimer = new QTimer(this);
        m_autoBackupTimer->setSingleShot(true);
        connect(m_autoBackupTimer, &QTimer::timeout, this, &MainWindow::evaluateAutomatedBackupWindow);
    }
    evaluateAutomatedBackupWindow();
}

void MainWindow::runEncryptedRestoreFlow(const QString &sourcePath, bool startupWithoutDb) {
    if (sourcePath.trimmed().isEmpty()) {
        return;
    }

    QString suggested = defaultBackupPath();
    if (!suggested.endsWith(".db", Qt::CaseInsensitive)) {
        suggested += ".db";
    }
    QString restoredPath = QFileDialog::getSaveFileName(
        this,
        "Restore Encrypted Backup As",
        suggested,
        "SQLite Database (*.db *.sqlite *.sqlite3);;All Files (*.*)");
    if (restoredPath.isEmpty()) {
        return;
    }
    if (QFileInfo(restoredPath).suffix().isEmpty()) {
        restoredPath += ".db";
    }

    QString restoreError;
    if (!restoreEncryptedBackup(sourcePath, restoredPath, &restoreError)) {
        const QString message = restoreError.isEmpty() ? QString("Failed to restore encrypted backup.")
                                                       : restoreError;
        setStatus(message, false);
        logAction("DB_RESTORE",
                  sourcePath,
                  QString(),
                  restoredPath,
                  message,
                  "Backup",
                  "BACKUP_RESTORE",
                  false,
                  message,
                  sourcePath);
        return;
    }

    const bool restoredOpen = openDatabaseAt(restoredPath);
    bool ready = restoredOpen;
    QString failure;
    if (restoredOpen && startupWithoutDb && m_currentUsername.trimmed().isEmpty()) {
        if (!promptLogin()) {
            ready = false;
            failure = QString("Login required for loaded database.");
            setStatus(failure, false);
            QTimer::singleShot(0, this, &QWidget::close);
        }
    } else if (!restoredOpen) {
        failure = QString("Failed to open restored database.");
    }

    logAction("DB_RESTORE",
              sourcePath,
              QString(),
              restoredPath,
              failure,
              "Backup",
              "BACKUP_RESTORE",
              ready,
              ready ? QString() : (m_lastDatabaseOpenError.isEmpty() ? failure : m_lastDatabaseOpenError),
              sourcePath);
}

void MainWindow::onLoadDbTriggered() {
    const bool startupWithoutDb = !m_db.isOpen() && m_currentUsername.trimmed().isEmpty();
    if (!startupWithoutDb &&
        !requireAccess(m_access.canBackupRestore, "You don't have permission to load or restore databases.")) {
        return;
    }

    const QString startDir = QFileInfo(currentDatabasePath()).absolutePath();
    const QString file = QFileDialog::getOpenFileName(
        this,
        "Load Database",
        startDir,
        "Database Files (*.db *.sqlite *.sqlite3 *.encdb);;SQLite Database (*.db *.sqlite *.sqlite3);;Encrypted Backup (*.encdb);;All Files (*.*)");
    if (file.isEmpty()) {
        return;
    }

    const QString suffix = QFileInfo(file).suffix().trimmed().toLower();
    if (suffix == "encdb") {
        runEncryptedRestoreFlow(file, startupWithoutDb);
        return;
    }

    const bool ok = openDatabaseAt(file);
    bool ready = ok;
    if (ok && startupWithoutDb && m_currentUsername.trimmed().isEmpty()) {
        if (!promptLogin()) {
            ready = false;
            setStatus("Login required for loaded database.", false);
            QTimer::singleShot(0, this, &QWidget::close);
        }
    }

    logAction("DB_LOAD",
              file,
              QString(),
              file,
              ready ? QString() : QString("Failed to load database"),
              "Backup",
              "BACKUP_RESTORE",
              ready,
              ready ? QString() : QString("Failed to load database"),
              file);
}

void MainWindow::onRestoreBackupTriggered() {
    if (!requireAccess(m_access.canBackupRestore, "You don't have permission to restore database backups.")) {
        return;
    }

    const QString startDir = QFileInfo(currentDatabasePath()).absolutePath();
    const QString file = QFileDialog::getOpenFileName(
        this,
        "Select Encrypted Backup",
        startDir,
        "Encrypted Backup (*.encdb);;All Files (*.*)");
    if (file.isEmpty()) {
        return;
    }

    const bool startupWithoutDb = !m_db.isOpen() && m_currentUsername.trimmed().isEmpty();
    runEncryptedRestoreFlow(file, startupWithoutDb);
}

void MainWindow::onExportDbTriggered() {
    if (!requireAccess(m_access.canBackupRestore, "You don't have permission to export or back up databases.")) {
        return;
    }

    const QString suggested = defaultBackupPath();
    const QString file = QFileDialog::getSaveFileName(
        this,
        "Export Database",
        suggested,
        "SQLite Database (*.db *.sqlite *.sqlite3);;All Files (*.*)");
    if (file.isEmpty()) {
        return;
    }
    backupDatabaseTo(file);
}

void MainWindow::exportSkuMasterCsv() {
    if (!requireAccess(m_access.canExport, "You don't have permission to export data.")) {
        return;
    }

    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QDir exportDir(dataDirPath());
    if (!exportDir.exists()) {
        exportDir.mkpath(".");
    }
    const QString defaultName = exportDir.filePath(QString("sku_master_%1.csv").arg(stamp));
    const QString chosen = QFileDialog::getSaveFileName(
        this,
        "Export SKU Master CSV",
        defaultName,
        "CSV Files (*.csv)");
    if (chosen.isEmpty()) {
        return;
    }

    QFileInfo info(chosen);
    QString targetPath = chosen;
    const QRegularExpression stampPattern("\\d{8}_\\d{6}");
    if (!targetPath.contains(stampPattern)) {
        QString suffix = info.suffix();
        if (suffix.isEmpty()) {
            suffix = "csv";
        }
        targetPath = info.dir().filePath(QString("%1_%2.%3").arg(info.completeBaseName(), stamp, suffix));
    }

    QFile file(targetPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        setStatus("Unable to write SKU master CSV.", false);
        return;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << "SKU,Part Name\n";

    QSqlQuery q(m_db);
    q.prepare("SELECT sku, part_name FROM sku_catalog_active ORDER BY sku");
    if (q.exec()) {
        while (q.next()) {
            QStringList cells;
            cells.reserve(2);
            for (int i = 0; i < 2; ++i) {
                cells << escapeCsvField(q.value(i).toString());
            }
            out << cells.join(',') << '\n';
        }
    }

    file.close();
    setStatus(QString("SKU master exported to %1").arg(QDir::toNativeSeparators(targetPath)), true);
    logAction("CSV_EXPORT",
              "SKU_MASTER",
              QString(),
              targetPath,
              QString(),
              "Export",
              "EXPORT",
              true,
              QString(),
              targetPath);
}

void MainWindow::exportBarcodeSummaryCsv() {
    if (!requireAccess(m_access.canExport, "You don't have permission to export data.")) {
        return;
    }

    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QDir exportDir(dataDirPath());
    if (!exportDir.exists()) {
        exportDir.mkpath(".");
    }
    const QString defaultName = exportDir.filePath(QString("qr_summary_%1.csv").arg(stamp));
    const QString chosen = QFileDialog::getSaveFileName(
        this,
        "Export QR Summary CSV",
        defaultName,
        "CSV Files (*.csv)");
    if (chosen.isEmpty()) {
        return;
    }

    QFileInfo info(chosen);
    QString targetPath = chosen;
    const QRegularExpression stampPattern("\\d{8}_\\d{6}");
    if (!targetPath.contains(stampPattern)) {
        QString suffix = info.suffix();
        if (suffix.isEmpty()) {
            suffix = "csv";
        }
        targetPath = info.dir().filePath(QString("%1_%2.%3").arg(info.completeBaseName(), stamp, suffix));
    }

    QFile file(targetPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        setStatus("Unable to write QR summary CSV.", false);
        return;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << "SKU,Part Name,Year,Quarter,Total Serials\n";

    QSqlQuery q(m_db);
    q.prepare(
        "SELECT b.sku, s.part_name, b.year, b.quarter, COUNT(*) "
        "FROM barcode_log_active b "
        "LEFT JOIN sku_catalog_active s ON s.sku = b.sku "
        "GROUP BY b.sku, b.year, b.quarter "
        "ORDER BY b.sku, b.year, b.quarter");
    if (q.exec()) {
        while (q.next()) {
            const QString sku = q.value(0).toString();
            const QString partName = q.value(1).toString();
            const int year = q.value(2).toInt();
            const int quarter = q.value(3).toInt();
            const int total = q.value(4).toInt();
            QStringList cells;
            cells << escapeCsvField(sku)
                  << escapeCsvField(partName)
                  << QString::number(year)
                  << QString("Q%1").arg(quarter)
                  << QString::number(total);
            out << cells.join(',') << '\n';
        }
    }

    file.close();
    setStatus(QString("QR summary exported to %1").arg(QDir::toNativeSeparators(targetPath)), true);
    logAction("CSV_EXPORT",
              "BARCODE_SUMMARY",
              QString(),
              targetPath,
              QString(),
              "Export",
              "EXPORT",
              true,
              QString(),
              targetPath);
}

// ---------------------------------------------------------------------------
// Excel (SpreadsheetML) export — all generated serial numbers grouped by SKU.
// SpreadsheetML is plain XML; Excel opens it natively with a .xls extension
// so no third-party library is required.
// ---------------------------------------------------------------------------
void MainWindow::exportAllSerialsXls() {
    if (!requireAccess(m_access.canExport, "You don't have permission to export data.")) {
        return;
    }
    if (!m_db.isOpen()) {
        setStatus("No database open.", false);
        return;
    }

    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QDir exportDir(dataDirPath());
    if (!exportDir.exists()) {
        exportDir.mkpath(".");
    }
    const QString defaultName = exportDir.filePath(QString("serial_numbers_%1.xls").arg(stamp));
    const QString chosen = QFileDialog::getSaveFileName(
        this,
        "Export All Generated Serial Numbers",
        defaultName,
        "Excel Workbook (*.xls);;All Files (*.*)");
    if (chosen.isEmpty()) {
        return;
    }

    // Ensure timestamp in filename to avoid silent overwrites.
    QString targetPath = chosen;
    {
        const QRegularExpression stampPattern("\\d{8}_\\d{6}");
        if (!targetPath.contains(stampPattern)) {
            QFileInfo info(chosen);
            QString suffix = info.suffix();
            if (suffix.isEmpty()) { suffix = "xls"; }
            targetPath = info.dir().filePath(
                QString("%1_%2.%3").arg(info.completeBaseName(), stamp, suffix));
        }
    }

    // -----------------------------------------------------------------------
    // Fetch all serials joined to their SKU master record, ordered by SKU then
    // year / quarter / serial so the sheet reads chronologically within each SKU.
    // -----------------------------------------------------------------------
    struct SerialRow {
        QString sku, partName, partNumber, category, subCategory;
        QString description, storage, rackNo, binNo;
        int     serial = 0;
        QString barcode, quarter, year, generatedAt;
    };
    QList<SerialRow> rows;

    QSqlQuery q(m_db);
    q.prepare(
        "SELECT "
        "  COALESCE(s.sku, b.sku)         AS sku, "
        "  COALESCE(s.part_name,  '')      AS part_name, "
        "  COALESCE(s.part_number,'')      AS part_number, "
        "  COALESCE(s.category_code,'')    AS category, "
        "  COALESCE(s.sub_category,'')     AS sub_category, "
        "  COALESCE(s.description,'')      AS description, "
        "  COALESCE(s.storage,'')          AS storage, "
        "  COALESCE(s.rack_number,'')      AS rack_no, "
        "  COALESCE(s.bin_number,'')       AS bin_no, "
        "  b.serial, "
        "  b.barcode, "
        "  b.quarter, "
        "  b.year, "
        "  b.created_at "
        "FROM barcode_log_active b "
        "LEFT JOIN sku_catalog_active s ON s.sku = b.sku "
        "ORDER BY COALESCE(s.sku, b.sku), b.year, b.quarter, b.serial");

    if (!q.exec()) {
        setStatus("Failed to query serial numbers.", false);
        return;
    }
    while (q.next()) {
        SerialRow r;
        r.sku         = q.value(0).toString().trimmed().toUpper();
        r.partName    = q.value(1).toString().trimmed();
        r.partNumber  = q.value(2).toString().trimmed();
        r.category    = q.value(3).toString().trimmed();
        r.subCategory = q.value(4).toString().trimmed();
        r.description = q.value(5).toString().trimmed();
        r.storage     = q.value(6).toString().trimmed();
        r.rackNo      = q.value(7).toString().trimmed();
        r.binNo       = q.value(8).toString().trimmed();
        r.serial      = q.value(9).toInt();
        r.barcode     = q.value(10).toString().trimmed();
        r.quarter     = QString("Q%1").arg(q.value(11).toInt());
        r.year        = q.value(12).toString().trimmed();
        r.generatedAt = q.value(13).toString().trimmed();
        rows.append(r);
    }

    if (rows.isEmpty()) {
        setStatus("No serial numbers found to export.", false);
        return;
    }

    // -----------------------------------------------------------------------
    // Build the SpreadsheetML XML document.
    // -----------------------------------------------------------------------
    auto xmlCell = [](const QString &styleId, const QString &type, const QString &value) -> QString {
        const QString escaped = QString(value)
            .replace("&",  "&amp;")
            .replace("<",  "&lt;")
            .replace(">",  "&gt;")
            .replace("\"", "&quot;");
        if (styleId.isEmpty()) {
            return QString("   <Cell><Data ss:Type=\"%1\">%2</Data></Cell>\n").arg(type, escaped);
        }
        return QString("   <Cell ss:StyleID=\"%1\"><Data ss:Type=\"%2\">%3</Data></Cell>\n")
            .arg(styleId, type, escaped);
    };
    auto numCell = [](const QString &styleId, int value) -> QString {
        if (styleId.isEmpty()) {
            return QString("   <Cell><Data ss:Type=\"Number\">%1</Data></Cell>\n").arg(value);
        }
        return QString("   <Cell ss:StyleID=\"%1\"><Data ss:Type=\"Number\">%2</Data></Cell>\n")
            .arg(styleId).arg(value);
    };

    QString xml;
    xml.reserve(512 * 1024);

    // Header
    xml += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<?mso-application progid=\"Excel.Sheet\"?>\n"
           "<Workbook xmlns=\"urn:schemas-microsoft-com:office:spreadsheet\"\n"
           " xmlns:o=\"urn:schemas-microsoft-com:office:office\"\n"
           " xmlns:x=\"urn:schemas-microsoft-com:office:excel\"\n"
           " xmlns:ss=\"urn:schemas-microsoft-com:office:spreadsheet\">\n";

    // Styles
    xml +=
        " <Styles>\n"
        "  <Style ss:ID=\"Default\" ss:Name=\"Normal\"/>\n"
        // Column header row — amber background, white bold text
        "  <Style ss:ID=\"hdr\">\n"
        "   <Font ss:Bold=\"1\" ss:Color=\"#FFFFFF\" ss:Size=\"10\"/>\n"
        "   <Interior ss:Color=\"#C05E02\" ss:Pattern=\"Solid\"/>\n"
        "   <Alignment ss:Horizontal=\"Center\" ss:Vertical=\"Center\" ss:WrapText=\"1\"/>\n"
        "   <Borders><Border ss:Position=\"Bottom\" ss:LineStyle=\"Continuous\" ss:Weight=\"2\" ss:Color=\"#7A3700\"/></Borders>\n"
        "  </Style>\n"
        // SKU group header row — light amber, dark bold text
        "  <Style ss:ID=\"sku\">\n"
        "   <Font ss:Bold=\"1\" ss:Color=\"#1C2128\" ss:Size=\"10\"/>\n"
        "   <Interior ss:Color=\"#FFA657\" ss:Pattern=\"Solid\"/>\n"
        "   <Borders><Border ss:Position=\"Bottom\" ss:LineStyle=\"Continuous\" ss:Weight=\"1\" ss:Color=\"#C05E02\"/></Borders>\n"
        "  </Style>\n"
        // Even serial row — very light steel
        "  <Style ss:ID=\"even\">\n"
        "   <Interior ss:Color=\"#F6F8FA\" ss:Pattern=\"Solid\"/>\n"
        "  </Style>\n"
        // Odd serial row — white
        "  <Style ss:ID=\"odd\">\n"
        "   <Interior ss:Color=\"#FFFFFF\" ss:Pattern=\"Solid\"/>\n"
        "  </Style>\n"
        // Serial number cell — bold
        "  <Style ss:ID=\"even_ser\">\n"
        "   <Font ss:Bold=\"1\"/>\n"
        "   <Interior ss:Color=\"#F6F8FA\" ss:Pattern=\"Solid\"/>\n"
        "   <Alignment ss:Horizontal=\"Center\"/>\n"
        "  </Style>\n"
        "  <Style ss:ID=\"odd_ser\">\n"
        "   <Font ss:Bold=\"1\"/>\n"
        "   <Interior ss:Color=\"#FFFFFF\" ss:Pattern=\"Solid\"/>\n"
        "   <Alignment ss:Horizontal=\"Center\"/>\n"
        "  </Style>\n"
        " </Styles>\n";

    // Worksheet — "All Serials"
    xml += " <Worksheet ss:Name=\"All Serials\">\n";
    xml += "  <Table ss:DefaultRowHeight=\"15\">\n";
    // Column widths (matches header order below)
    xml += "   <Column ss:Width=\"80\"/>\n";   // SKU
    xml += "   <Column ss:Width=\"140\"/>\n";  // Part Name
    xml += "   <Column ss:Width=\"110\"/>\n";  // Part Number
    xml += "   <Column ss:Width=\"80\"/>\n";   // Category
    xml += "   <Column ss:Width=\"100\"/>\n";  // Sub-Category
    xml += "   <Column ss:Width=\"180\"/>\n";  // Description
    xml += "   <Column ss:Width=\"100\"/>\n";  // Storage
    xml += "   <Column ss:Width=\"70\"/>\n";   // Rack No.
    xml += "   <Column ss:Width=\"70\"/>\n";   // Bin No.
    xml += "   <Column ss:Width=\"60\"/>\n";   // Serial #
    xml += "   <Column ss:Width=\"220\"/>\n";  // QR Code
    xml += "   <Column ss:Width=\"60\"/>\n";   // Quarter
    xml += "   <Column ss:Width=\"55\"/>\n";   // Year
    xml += "   <Column ss:Width=\"140\"/>\n";  // Generated At

    // --- Column header row ---
    xml += "  <Row ss:AutoFitHeight=\"1\">\n";
    const QStringList headers = {
        "SKU", "Part Name", "Part Number", "Category", "Sub-Category",
        "Description", "Storage", "Rack No.", "Bin No.",
        "Serial #", "QR Code", "Quarter", "Year", "Generated At"
    };
    for (const QString &h : headers) {
        xml += xmlCell("hdr", "String", h);
    }
    xml += "  </Row>\n";

    // --- Data rows, grouped by SKU ---
    QString lastSku;
    int rowIndex = 0; // used to alternate row colours within a SKU group
    for (const SerialRow &r : rows) {
        // SKU group header whenever SKU changes
        if (r.sku != lastSku) {
            lastSku  = r.sku;
            rowIndex = 0;
            xml += "  <Row ss:AutoFitHeight=\"1\">\n";
            xml += xmlCell("sku", "String", r.sku);
            xml += xmlCell("sku", "String", r.partName);
            xml += xmlCell("sku", "String", r.partNumber);
            xml += xmlCell("sku", "String", r.category);
            xml += xmlCell("sku", "String", r.subCategory);
            xml += xmlCell("sku", "String", r.description);
            xml += xmlCell("sku", "String", r.storage);
            xml += xmlCell("sku", "String", r.rackNo);
            xml += xmlCell("sku", "String", r.binNo);
            xml += xmlCell("sku", "String", "");  // serial placeholder
            xml += xmlCell("sku", "String", "");  // barcode placeholder
            xml += xmlCell("sku", "String", "");
            xml += xmlCell("sku", "String", "");
            xml += xmlCell("sku", "String", "");
            xml += "  </Row>\n";
        }

        // Serial detail row
        const bool even = (rowIndex % 2 == 0);
        const QString rowStyle    = even ? "even"    : "odd";
        const QString serialStyle = even ? "even_ser" : "odd_ser";
        ++rowIndex;

        xml += "  <Row ss:AutoFitHeight=\"1\">\n";
        xml += xmlCell(rowStyle, "String", r.sku);
        xml += xmlCell(rowStyle, "String", r.partName);
        xml += xmlCell(rowStyle, "String", r.partNumber);
        xml += xmlCell(rowStyle, "String", r.category);
        xml += xmlCell(rowStyle, "String", r.subCategory);
        xml += xmlCell(rowStyle, "String", r.description);
        xml += xmlCell(rowStyle, "String", r.storage);
        xml += xmlCell(rowStyle, "String", r.rackNo);
        xml += xmlCell(rowStyle, "String", r.binNo);
        xml += numCell(serialStyle, r.serial);
        xml += xmlCell(rowStyle, "String", r.barcode);
        xml += xmlCell(rowStyle, "String", r.quarter);
        xml += xmlCell(rowStyle, "String", r.year);
        xml += xmlCell(rowStyle, "String", r.generatedAt);
        xml += "  </Row>\n";
    }

    xml += "  </Table>\n";
    // Freeze the header row
    xml +=
        "  <WorksheetOptions xmlns=\"urn:schemas-microsoft-com:office:excel\">\n"
        "   <Selected/>\n"
        "   <FreezePanes/>\n"
        "   <FrozenNoSplit/>\n"
        "   <SplitHorizontal>1</SplitHorizontal>\n"
        "   <TopRowBottomPane>1</TopRowBottomPane>\n"
        "   <ActivePane>2</ActivePane>\n"
        "  </WorksheetOptions>\n";
    xml += " </Worksheet>\n";

    // ---- Summary sheet: one row per SKU with total serial count ----
    xml += " <Worksheet ss:Name=\"Summary\">\n";
    xml += "  <Table>\n";
    xml += "   <Column ss:Width=\"80\"/>\n";
    xml += "   <Column ss:Width=\"140\"/>\n";
    xml += "   <Column ss:Width=\"110\"/>\n";
    xml += "   <Column ss:Width=\"100\"/>\n";
    xml += "   <Column ss:Width=\"100\"/>\n";
    xml += "   <Column ss:Width=\"70\"/>\n";
    xml += "   <Column ss:Width=\"70\"/>\n";
    xml += "   <Column ss:Width=\"80\"/>\n";

    // Header
    xml += "  <Row>\n";
    const QStringList sumHeaders = {
        "SKU", "Part Name", "Part Number", "Storage", "Rack No.", "Bin No.", "Total Serials", "Last Generated"
    };
    for (const QString &h : sumHeaders) {
        xml += xmlCell("hdr", "String", h);
    }
    xml += "  </Row>\n";

    // Aggregate by SKU from already-sorted rows
    struct SkuAgg {
        QString sku, partName, partNumber, storage, rackNo, binNo, lastGenerated;
        int count = 0;
    };
    QList<SkuAgg> aggs;
    for (const SerialRow &r : rows) {
        if (!aggs.isEmpty() && aggs.last().sku == r.sku) {
            aggs.last().count++;
            aggs.last().lastGenerated = r.generatedAt;
        } else {
            SkuAgg a;
            a.sku = r.sku; a.partName = r.partName; a.partNumber = r.partNumber;
            a.storage = r.storage; a.rackNo = r.rackNo; a.binNo = r.binNo;
            a.lastGenerated = r.generatedAt; a.count = 1;
            aggs.append(a);
        }
    }
    int sumRow = 0;
    for (const SkuAgg &a : aggs) {
        const QString s = (sumRow % 2 == 0) ? "even" : "odd";
        ++sumRow;
        xml += "  <Row>\n";
        xml += xmlCell(s, "String", a.sku);
        xml += xmlCell(s, "String", a.partName);
        xml += xmlCell(s, "String", a.partNumber);
        xml += xmlCell(s, "String", a.storage);
        xml += xmlCell(s, "String", a.rackNo);
        xml += xmlCell(s, "String", a.binNo);
        xml += numCell(s, a.count);
        xml += xmlCell(s, "String", a.lastGenerated);
        xml += "  </Row>\n";
    }
    xml += "  </Table>\n";
    xml +=
        "  <WorksheetOptions xmlns=\"urn:schemas-microsoft-com:office:excel\">\n"
        "   <FreezePanes/>\n"
        "   <FrozenNoSplit/>\n"
        "   <SplitHorizontal>1</SplitHorizontal>\n"
        "   <TopRowBottomPane>1</TopRowBottomPane>\n"
        "   <ActivePane>2</ActivePane>\n"
        "  </WorksheetOptions>\n";
    xml += " </Worksheet>\n";

    xml += "</Workbook>\n";

    // Write to file
    QFile file(targetPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        setStatus("Unable to write Excel file.", false);
        return;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << xml;
    file.close();

    const int totalSerials = rows.size();
    const int totalSkus    = aggs.size();
    setStatus(QString("Exported %1 serials across %2 SKUs to %3")
                  .arg(totalSerials).arg(totalSkus)
                  .arg(QDir::toNativeSeparators(targetPath)), true);

    logAction("EXCEL_EXPORT",
              "ALL_SERIALS",
              QString(),
              targetPath,
              QString(),
              "Export",
              "EXPORT",
              true,
              QString(),
              targetPath);
}

void MainWindow::onSaveDbTriggered() {
    if (!requireAccess(m_access.canBackupRestore, "You don't have permission to run database backups.")) {
        return;
    }
    const QString path = defaultBackupPath();
    backupDatabaseTo(path);
}

void MainWindow::onBackupDbClicked() {
    if (!requireAccess(m_access.canBackupRestore, "You don't have permission to run database backups.")) {
        return;
    }
    onExportDbTriggered();
}

void MainWindow::onUninstallTriggered() {
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QFileInfoList entries = appDir.entryInfoList(QStringList() << "unins*.exe",
                                                       QDir::Files | QDir::Readable,
                                                       QDir::Name | QDir::Reversed);
    if (entries.isEmpty()) {
        QMessageBox::information(this,
                                 "Uninstall",
                                 "Uninstaller was not found in this application folder.\n"
                                 "This option is available for installed builds.");
        return;
    }

    const QString uninstallerPath = entries.first().absoluteFilePath();
    const auto reply = QMessageBox::question(
        this,
        "Uninstall",
        "This will close the application and start the uninstaller.\n\nContinue?",
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (reply != QMessageBox::Yes) {
        return;
    }

    if (!QProcess::startDetached(uninstallerPath, QStringList())) {
        QMessageBox::warning(this,
                             "Uninstall",
                             QString("Unable to start uninstaller:\n%1")
                                 .arg(QDir::toNativeSeparators(uninstallerPath)));
        return;
    }

    QCoreApplication::quit();
}

void MainWindow::onExitTriggered() {
    close();
}

void MainWindow::onFullScreenToggled(bool enabled) {
    if (enabled) {
        showFullScreen();
    } else {
        showNormal();
    }
}

void MainWindow::onHelpGuidesTriggered() {
    showHelpGuidesDialog();
}

void MainWindow::onSkuReferenceTriggered() {
    showSkuReferenceDialog();
}

void MainWindow::onSkuMapTriggered() {
    showSkuMapDocument();
}

void MainWindow::onPrintSettingsTriggered() {
    const QString previewPrefix = selectedBarcodePrefix();
    const bool updated = PrintSettingsDialog::edit(
        this,
        m_printSettings,
        previewPrefix,
        [this](const QString &value) { return renderQrCode(value); },
        [this](const QString &prefix) { return stickerLogoForPrefix(prefix); });
    if (updated) {
        m_printSettings.save();
    }
}

void MainWindow::onManageUsersTriggered() {
    showUserAccountsDialog();
}

void MainWindow::onSwitchUserTriggered() {
    promptLogin();
}

void MainWindow::showHelpGuidesDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle("Help");
    sizeDialogToScreen(&dialog, this, 980, 700, 900, 640);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    QLabel *intro = new QLabel("Open product documentation directly inside the application.", &dialog);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    QTabWidget *docsTabs = new QTabWidget(&dialog);
    docsTabs->setObjectName("helpDocsTabWidget");

    QTextBrowser *userGuideBrowser = new QTextBrowser(docsTabs);
    userGuideBrowser->setObjectName("userGuideTextBrowser");
    userGuideBrowser->setReadOnly(true);
    userGuideBrowser->setOpenExternalLinks(true);
    userGuideBrowser->setMarkdown(loadDocumentationMarkdown("Warehouse_SKU_QR_Manager_User_Guide.md"));
    docsTabs->addTab(userGuideBrowser, "User Guide");

    QTextBrowser *technicalGuideBrowser = new QTextBrowser(docsTabs);
    technicalGuideBrowser->setObjectName("technicalGuideTextBrowser");
    technicalGuideBrowser->setReadOnly(true);
    technicalGuideBrowser->setOpenExternalLinks(true);
    technicalGuideBrowser->setMarkdown(loadDocumentationMarkdown("Warehouse_SKU_QR_Manager_Technical_Documentation.md"));
    docsTabs->addTab(technicalGuideBrowser, "Technical Guide");

    layout->addWidget(docsTabs, 1);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    dialog.exec();
}

void MainWindow::showSkuReferenceDialog() {
    const QStringList localPaths = {
        QStringLiteral("D:/Wareehouse Bar Code generator/Assets/SKU Ref 1.png"),
        QStringLiteral("D:/Wareehouse Bar Code generator/Assets/SKU Ref 2.png")
    };
    const QStringList embeddedPaths = {
        QStringLiteral(":/assets/sku_ref_1.png"),
        QStringLiteral(":/assets/sku_ref_2.png")
    };

    auto pages = QSharedPointer<QVector<QPixmap>>::create();
    pages->reserve(localPaths.size());
    for (int i = 0; i < localPaths.size(); ++i) {
        QPixmap pixmap;
        pixmap.load(localPaths.at(i));
        if (pixmap.isNull() && i < embeddedPaths.size()) {
            pixmap.load(embeddedPaths.at(i));
        }
        pages->append(pixmap);
    }

    bool anyPageAvailable = false;
    for (const QPixmap &page : *pages) {
        if (!page.isNull()) {
            anyPageAvailable = true;
            break;
        }
    }
    if (!anyPageAvailable) {
        QMessageBox::warning(this,
                             "SKU Reference",
                             "Unable to load SKU reference images from:\n"
                             "D:\\Wareehouse Bar Code generator\\Assets\\SKU Ref 1.png\n"
                             "D:\\Wareehouse Bar Code generator\\Assets\\SKU Ref 2.png");
        return;
    }

    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::NonModal);
    dialog->setWindowTitle("SKU Reference");
    dialog->setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    dialog->setWindowFlag(Qt::WindowMinimizeButtonHint, true);
    sizeDialogToScreen(dialog, this, 980, 700, 860, 620);

    QVBoxLayout *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    QLabel *intro = new QLabel("SKU Reference image viewer. Use mouse wheel to zoom, drag/scroll to pan.", dialog);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *imageView = new ZoomableImageView(dialog);
    imageView->setFrameShape(QFrame::StyledPanel);
    layout->addWidget(imageView, 1);

    auto *controlsLayout = new QHBoxLayout();
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    controlsLayout->setSpacing(10);

    QPushButton *prevButton = new QPushButton("Previous", dialog);
    QPushButton *nextButton = new QPushButton("Next", dialog);
    QLabel *pageIndicator = new QLabel(dialog);
    pageIndicator->setAlignment(Qt::AlignCenter);
    pageIndicator->setMinimumWidth(160);

    controlsLayout->addWidget(prevButton);
    controlsLayout->addWidget(pageIndicator, 1);
    controlsLayout->addWidget(nextButton);
    layout->addLayout(controlsLayout);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    layout->addWidget(buttons);

    auto currentPageIndex = QSharedPointer<int>::create(0);
    auto updatePage = [imageView, pageIndicator, prevButton, nextButton, pages, currentPageIndex]() {
        const int totalPages = pages->size();
        int index = *currentPageIndex;
        if (index < 0) {
            index = 0;
        }
        if (index >= totalPages) {
            index = totalPages - 1;
        }
        *currentPageIndex = index;

        imageView->setImage(pages->at(index));
        pageIndicator->setText(QString("Page %1 of %2").arg(index + 1).arg(totalPages));
        prevButton->setEnabled(index > 0);
        nextButton->setEnabled(index < totalPages - 1);
    };

    connect(prevButton, &QPushButton::clicked, dialog, [currentPageIndex, updatePage]() {
        *currentPageIndex -= 1;
        updatePage();
    });
    connect(nextButton, &QPushButton::clicked, dialog, [currentPageIndex, updatePage]() {
        *currentPageIndex += 1;
        updatePage();
    });

    updatePage();

    QScreen *screen = this->screen();
    if (!screen) {
        screen = QApplication::primaryScreen();
    }
    if (screen) {
        const QRect available = screen->availableGeometry();
        dialog->move(available.center() - QPoint(dialog->width() / 2, dialog->height() / 2));
    }

    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void MainWindow::showSkuMapDocument() {
    const QString fileName = QStringLiteral("A0_SKU_MAP_Diagram .pdf");
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath("Assets/" + fileName),
        QDir(appDir).filePath("../Assets/" + fileName),
        QDir(appDir).filePath("../../Assets/" + fileName),
        QDir::current().filePath("Assets/" + fileName)
    };

    for (const QString &path : candidates) {
        if (QFile::exists(path)) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()));
            return;
        }
    }

    QString message = QString("SKU MAP file not found: %1\n\nSearched in:\n").arg(fileName);
    for (const QString &path : candidates) {
        message += " - " + QDir::toNativeSeparators(path) + '\n';
    }
    QMessageBox::warning(this, "SKU MAP", message);
}

bool MainWindow::initDb() {
    migrateLegacyDataIfNeeded();
    const QString path = dbPath();
    if (path.isEmpty()) {
        setStatus("No database selected.", false);
        return false;
    }
    QDir dbDir(QFileInfo(path).absoluteDir());
    if (!dbDir.exists()) {
        dbDir.mkpath(".");
    }

    if (QSqlDatabase::contains("sku_connection")) {
        m_db = QSqlDatabase::database("sku_connection");
    } else {
        m_db = QSqlDatabase::addDatabase("QSQLITE", "sku_connection");
        m_db.setDatabaseName(path);
    }

    if (!m_db.open()) {
        return false;
    }

    if (!ensureSchema()) {
        return false;
    }

    if (!ensureDefaultRoles()) {
        return false;
    }

    if (!loadRulesIfEmpty()) {
        return false;
    }

    if (!loadCatalogIfEmpty()) {
        return false;
    }

    return true;
}

bool MainWindow::ensureSchema() {
    QSqlQuery q(m_db);

    // This is both the initial bootstrap and the in-place migration path, so table
    // creation stays additive and later column fixes are handled below.
    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS sku_rules ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  category_digit TEXT NOT NULL,"
            "  category_name TEXT NOT NULL,"
            "  subcategory_digit TEXT NOT NULL,"
            "  subcategory_name TEXT NOT NULL"
            ")")) {
        return false;
    }

    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS sku_catalog ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  si_no INTEGER NOT NULL,"
            "  part_number TEXT,"
            "  sku TEXT UNIQUE,"
            "  part_name TEXT,"
            "  category_code TEXT,"
            "  sub_category TEXT,"
            "  item_serial INTEGER,"
            "  unique_variation INTEGER,"
            "  description TEXT,"
            "  storage TEXT,"
            "  rack_number TEXT,"
            "  bin_number TEXT,"
            "  dimensions TEXT,"
            "  weight_value REAL NOT NULL,"
            "  weight_unit TEXT NOT NULL,"
            "  product_family TEXT,"
            "  image_path TEXT,"
            "  image_blob BLOB,"
            "  comments TEXT,"
            "  created_at TEXT,"
            "  is_deleted INTEGER DEFAULT 0,"
            "  deleted_at TEXT,"
            "  deleted_by TEXT,"
            "  delete_reason TEXT,"
            "  deleted_snapshot TEXT"
            ")")) {
        return false;
    }

    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS app_meta ("
            "  key TEXT PRIMARY KEY,"
            "  value TEXT"
            ")")) {
        return false;
    }

    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS app_roles ("
            "  role_key TEXT PRIMARY KEY,"
            "  display_name TEXT NOT NULL,"
            "  base_role TEXT NOT NULL,"
            "  can_add INTEGER,"
            "  can_edit INTEGER,"
            "  can_delete INTEGER,"
            "  can_serial_edit INTEGER,"
            "  can_serial_delete INTEGER,"
            "  can_print INTEGER,"
            "  can_manage_users INTEGER,"
            "  can_backup_restore INTEGER,"
            "  can_export INTEGER,"
            "  created_at TEXT NOT NULL"
            ")")) {
        return false;
    }

    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS app_users ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  username TEXT NOT NULL UNIQUE,"
            "  user_id TEXT,"
            "  full_name TEXT,"
            "  email TEXT,"
            "  password_hash TEXT NOT NULL,"
            "  password_salt TEXT NOT NULL,"
            "  role TEXT NOT NULL,"
            "  perm_add INTEGER,"
            "  perm_edit INTEGER,"
            "  perm_delete INTEGER,"
            "  perm_serial_edit INTEGER,"
            "  perm_serial_delete INTEGER,"
            "  perm_print INTEGER,"
            "  perm_manage_users INTEGER,"
            "  perm_backup_restore INTEGER,"
            "  perm_export INTEGER,"
            "  created_at TEXT NOT NULL"
            ")")) {
        return false;
    }

    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS app_audit_log ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  timestamp TEXT NOT NULL,"
            "  user TEXT NOT NULL,"
            "  user_id TEXT,"
            "  role TEXT,"
            "  machine_id TEXT,"
            "  module_screen TEXT,"
            "  action_type TEXT,"
            "  action TEXT NOT NULL,"
            "  entity TEXT NOT NULL,"
            "  record_id TEXT,"
            "  old_value TEXT,"
            "  new_value TEXT,"
            "  comment TEXT,"
            "  result TEXT,"
            "  error_message TEXT"
            ")")) {
        return false;
    }

    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS barcode_serials ("
            "  sku TEXT NOT NULL,"
            "  year INTEGER NOT NULL,"
            "  quarter INTEGER NOT NULL,"
            "  last_serial INTEGER NOT NULL,"
            "  PRIMARY KEY (sku, year, quarter)"
            ")")) {
        return false;
    }

    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS barcode_log ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  sku TEXT NOT NULL,"
            "  year INTEGER NOT NULL,"
            "  quarter INTEGER NOT NULL,"
            "  serial INTEGER NOT NULL,"
            "  barcode TEXT NOT NULL UNIQUE,"
            "  created_at TEXT,"
            "  is_deleted INTEGER DEFAULT 0,"
            "  deleted_at TEXT,"
            "  deleted_by TEXT,"
            "  delete_reason TEXT,"
            "  deleted_snapshot TEXT"
            ")")) {
        return false;
    }

    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS app_backup_history ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  timestamp TEXT NOT NULL,"
            "  trigger_type TEXT NOT NULL,"
            "  backup_path TEXT NOT NULL,"
            "  checksum TEXT,"
            "  file_size INTEGER,"
            "  metadata TEXT,"
            "  result TEXT NOT NULL,"
            "  error_message TEXT"
            ")")) {
        return false;
    }

    q.exec("CREATE INDEX IF NOT EXISTS idx_sku_catalog_sku ON sku_catalog(sku)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_sku_catalog_part_number ON sku_catalog(part_number)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_sku_catalog_part_name ON sku_catalog(part_name)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_sku_catalog_category ON sku_catalog(category_code, sub_category)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_app_users_username ON app_users(username)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_app_roles_key ON app_roles(role_key)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_sku_catalog_active ON sku_catalog(is_deleted, sku)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_barcode_log_active ON barcode_log(is_deleted, sku, year, quarter)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_audit_timestamp ON app_audit_log(timestamp)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_backup_timestamp ON app_backup_history(timestamp)");

    auto ensureColumn = [&](const QString &table, const QString &column, const QString &type) {
        QSqlQuery info(m_db);
        info.exec(QString("PRAGMA table_info(%1)").arg(table));
        while (info.next()) {
            if (info.value(1).toString() == column) {
                return true;
            }
        }
        QSqlQuery alter(m_db);
        return alter.exec(QString("ALTER TABLE %1 ADD COLUMN %2 %3").arg(table, column, type));
    };

    if (!ensureColumn("app_users", "user_id", "TEXT")) {
        return false;
    }
    if (!ensureColumn("app_users", "full_name", "TEXT")) {
        return false;
    }
    if (!ensureColumn("app_users", "email", "TEXT")) {
        return false;
    }
    if (!ensureColumn("app_users", "perm_add", "INTEGER")) {
        return false;
    }
    if (!ensureColumn("app_users", "perm_edit", "INTEGER")) {
        return false;
    }
    if (!ensureColumn("app_users", "perm_delete", "INTEGER")) {
        return false;
    }
    if (!ensureColumn("app_users", "perm_serial_edit", "INTEGER")) {
        return false;
    }
    if (!ensureColumn("app_users", "perm_serial_delete", "INTEGER")) {
        return false;
    }
    if (!ensureColumn("app_users", "perm_print", "INTEGER")) {
        return false;
    }
    if (!ensureColumn("app_users", "perm_manage_users", "INTEGER")) {
        return false;
    }
    if (!ensureColumn("app_users", "perm_backup_restore", "INTEGER")) {
        return false;
    }
    if (!ensureColumn("app_users", "perm_export", "INTEGER")) {
        return false;
    }

    if (!ensureColumn("app_roles", "can_add", "INTEGER")) {
        return false;
    }
    if (!ensureColumn("app_roles", "can_edit", "INTEGER")) {
        return false;
    }
    if (!ensureColumn("app_roles", "can_delete", "INTEGER")) {
        return false;
    }
    if (!ensureColumn("app_roles", "can_serial_edit", "INTEGER")) {
        return false;
    }
    if (!ensureColumn("app_roles", "can_serial_delete", "INTEGER")) {
        return false;
    }
    if (!ensureColumn("app_roles", "can_print", "INTEGER")) {
        return false;
    }
    if (!ensureColumn("app_roles", "can_manage_users", "INTEGER")) {
        return false;
    }
    if (!ensureColumn("app_roles", "can_backup_restore", "INTEGER")) {
        return false;
    }
    if (!ensureColumn("app_roles", "can_export", "INTEGER")) {
        return false;
    }

    if (!ensureColumn("sku_catalog", "weight_value", "REAL")) {
        return false;
    }
    if (!ensureColumn("sku_catalog", "weight_unit", "TEXT")) {
        return false;
    }
    if (!ensureColumn("sku_catalog", "image_blob", "BLOB")) {
        return false;
    }
    if (!ensureColumn("sku_catalog", "rack_number", "TEXT")) {
        return false;
    }
    if (!ensureColumn("sku_catalog", "bin_number", "TEXT")) {
        return false;
    }
    if (!ensureColumn("sku_catalog", "is_deleted", "INTEGER DEFAULT 0")) {
        return false;
    }
    if (!ensureColumn("sku_catalog", "deleted_at", "TEXT")) {
        return false;
    }
    if (!ensureColumn("sku_catalog", "deleted_by", "TEXT")) {
        return false;
    }
    if (!ensureColumn("sku_catalog", "delete_reason", "TEXT")) {
        return false;
    }
    if (!ensureColumn("sku_catalog", "deleted_snapshot", "TEXT")) {
        return false;
    }

    if (!ensureColumn("barcode_log", "is_deleted", "INTEGER DEFAULT 0")) {
        return false;
    }
    if (!ensureColumn("barcode_log", "deleted_at", "TEXT")) {
        return false;
    }
    if (!ensureColumn("barcode_log", "deleted_by", "TEXT")) {
        return false;
    }
    if (!ensureColumn("barcode_log", "delete_reason", "TEXT")) {
        return false;
    }
    if (!ensureColumn("barcode_log", "deleted_snapshot", "TEXT")) {
        return false;
    }

    if (!ensureColumn("app_audit_log", "user_id", "TEXT")) {
        return false;
    }
    if (!ensureColumn("app_audit_log", "role", "TEXT")) {
        return false;
    }
    if (!ensureColumn("app_audit_log", "machine_id", "TEXT")) {
        return false;
    }
    if (!ensureColumn("app_audit_log", "module_screen", "TEXT")) {
        return false;
    }
    if (!ensureColumn("app_audit_log", "action_type", "TEXT")) {
        return false;
    }
    if (!ensureColumn("app_audit_log", "record_id", "TEXT")) {
        return false;
    }
    if (!ensureColumn("app_audit_log", "result", "TEXT")) {
        return false;
    }
    if (!ensureColumn("app_audit_log", "error_message", "TEXT")) {
        return false;
    }

    q.exec("UPDATE sku_catalog SET is_deleted = COALESCE(is_deleted, 0)");
    q.exec("UPDATE barcode_log SET is_deleted = COALESCE(is_deleted, 0)");

    QSqlQuery migrateWeight(m_db);
    migrateWeight.exec("UPDATE sku_catalog SET weight_value = CAST(weight AS REAL) "
                       "WHERE (weight_value IS NULL OR weight_value = 0) AND weight IS NOT NULL");
    migrateWeight.exec("UPDATE sku_catalog SET weight_unit = 'kg' WHERE weight_unit IS NULL OR weight_unit = ''");

    q.exec("DROP VIEW IF EXISTS sku_catalog_active");
    q.exec("DROP VIEW IF EXISTS barcode_log_active");
    q.exec("CREATE VIEW IF NOT EXISTS sku_catalog_active AS "
           "SELECT * FROM sku_catalog WHERE COALESCE(is_deleted, 0) = 0");
    q.exec("CREATE VIEW IF NOT EXISTS barcode_log_active AS "
           "SELECT * FROM barcode_log WHERE COALESCE(is_deleted, 0) = 0");

    return true;
}

bool MainWindow::loadRulesIfEmpty() {
    const QString csvPath = findRulesCsvPath();
    if (csvPath.isEmpty()) {
        setStatus("Could not find sku_rules.csv. Place it under Assets/.", false);
        return false;
    }

    QFile file(csvPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream in(&file);
    bool firstLine = true;
    QString lastCategoryDigit;
    QString lastCategoryName;
    struct RuleRow {
        QString categoryDigit;
        QString categoryName;
        QString subDigit;
        QString subName;
    };
    QList<RuleRow> rules;

    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.trimmed().isEmpty()) {
            continue;
        }
        if (firstLine) {
            firstLine = false;
            continue;
        }
        const QStringList fields = parseCsvLine(line);
        if (fields.size() < 4) {
            continue;
        }

        QString categoryDigit = normalizedText(fields.at(0));
        QString categoryName = normalizedText(fields.at(1));
        if (categoryDigit.isEmpty()) {
            categoryDigit = lastCategoryDigit;
        }
        if (categoryName.isEmpty()) {
            categoryName = lastCategoryName;
        }

        const QString subDigit = normalizedText(fields.at(2));
        const QString subName = normalizedText(fields.at(3));

        if (categoryDigit.isEmpty() || subDigit.isEmpty()) {
            continue;
        }

        rules.append({categoryDigit, categoryName, subDigit, subName});
        lastCategoryDigit = categoryDigit;
        lastCategoryName = categoryName;
    }

    if (rules.isEmpty()) {
        setStatus("sku_rules.csv did not contain any valid rule rows.", false);
        return false;
    }

    if (!m_db.transaction()) {
        return false;
    }
    QSqlQuery clear(m_db);
    if (!clear.exec("DELETE FROM sku_rules")) {
        m_db.rollback();
        return false;
    }

    QSqlQuery insert(m_db);
    insert.prepare(
        "INSERT INTO sku_rules (category_digit, category_name, subcategory_digit, subcategory_name) "
        "VALUES (?, ?, ?, ?)");

    for (const RuleRow &rule : rules) {
        insert.addBindValue(rule.categoryDigit);
        insert.addBindValue(rule.categoryName);
        insert.addBindValue(rule.subDigit);
        insert.addBindValue(rule.subName);
        if (!insert.exec()) {
            m_db.rollback();
            return false;
        }
    }

    return m_db.commit();
}

QString MainWindow::findRulesCsvPath() const {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath("Assets/sku_rules.csv"),
        QDir(appDir).filePath("../Assets/sku_rules.csv"),
        QDir(appDir).filePath("../../Assets/sku_rules.csv"),
        QDir::current().filePath("Assets/sku_rules.csv"),
        QString(":/assets/sku_rules.csv")
    };
    for (const auto &path : candidates) {
        if (QFile::exists(path)) {
            return path;
        }
    }
    return QString();
}

QString MainWindow::findCatalogCsvPath() const {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath("Assets/EXISTING sku DATA.csv"),
        QDir(appDir).filePath("../Assets/EXISTING sku DATA.csv"),
        QDir(appDir).filePath("../../Assets/EXISTING sku DATA.csv"),
        QDir::current().filePath("Assets/EXISTING sku DATA.csv")
    };
    for (const auto &path : candidates) {
        if (QFile::exists(path)) {
            return path;
        }
    }
    return QString();
}

QString MainWindow::catalogImportFlagPath() const {
    return QDir(dataDirPath()).filePath("catalog_imported.flag");
}

bool MainWindow::loadCatalogIfEmpty() {
    const QString flagPath = catalogImportFlagPath();
    if (m_customDbPath.isEmpty() && QFile::exists(flagPath)) {
        return true;
    }

    const QString csvPath = findCatalogCsvPath();
    if (csvPath.isEmpty()) {
        return true;
    }

    QSqlQuery meta(m_db);
    meta.prepare("SELECT value FROM app_meta WHERE key = 'catalog_imported'");
    if (meta.exec() && meta.next()) {
        return true;
    }

    QFile file(csvPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream in(&file);
    bool firstLine = true;

    m_db.transaction();
    QSqlQuery insert(m_db);
    insert.prepare(
        "INSERT OR IGNORE INTO sku_catalog ("
        "si_no, part_number, sku, part_name, category_code, sub_category, item_serial, unique_variation, "
        "description, storage, rack_number, bin_number, dimensions, weight_value, weight_unit, "
        "product_family, image_path, comments, created_at"
        ") VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");

    int inserted = 0;
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.trimmed().isEmpty()) {
            continue;
        }
        if (firstLine) {
            firstLine = false;
            continue;
        }

        const QStringList fields = parseCsvLine(line);
        if (fields.size() < 4) {
            continue;
        }

        const QString siNo = normalizedText(fields.value(0));
        const QString partNumber = normalizedText(fields.value(1));
        const QString sku = normalizedText(fields.value(2));
        const QString partName = normalizedText(fields.value(3));
        const QString categoryCode = normalizedText(fields.value(6));
        const QString subCategory = normalizedText(fields.value(7));
        const QString itemSerial = normalizedText(fields.value(8));
        const QString variation = normalizedText(fields.value(9));
        const QString description = normalizedText(fields.value(10));
        const QString storage = normalizedText(fields.value(11));
        const QString rackNumber = QString();
        const QString binNumber = QString();
        const QString rawDimensions = normalizedText(fields.value(12));
        QString dimensions = rawDimensions;
        QString normalizedDimensions;
        if (tryNormalizeDimensionsCm(rawDimensions, &normalizedDimensions)) {
            dimensions = normalizedDimensions;
        }
        const QString weightText = normalizedText(fields.value(13));
        const QString imagePath = normalizedText(fields.value(15));
        const QString createdAt = normalizedText(fields.value(17));
        const QString comments = normalizedText(fields.value(18));
        const QString productFamily = normalizedText(fields.value(19));

        if (sku.isEmpty()) {
            continue;
        }

        insert.addBindValue(siNo.toInt());
        insert.addBindValue(partNumber);
        insert.addBindValue(sku);
        insert.addBindValue(partName);
        insert.addBindValue(categoryCode);
        insert.addBindValue(subCategory);
        insert.addBindValue(itemSerial.toInt());
        insert.addBindValue(variation.toInt());
        insert.addBindValue(description);
        insert.addBindValue(storage);
        insert.addBindValue(rackNumber);
        insert.addBindValue(binNumber);
        insert.addBindValue(dimensions);
        bool weightOk = false;
        const double weightValue = weightText.toDouble(&weightOk);
        insert.addBindValue(weightOk ? weightValue : 0.0);
        insert.addBindValue("kg");
        insert.addBindValue(productFamily);
        insert.addBindValue(imagePath);
        insert.addBindValue(comments);
        insert.addBindValue(createdAt);

        if (!insert.exec()) {
            m_db.rollback();
            return false;
        }
        const int rows = insert.numRowsAffected();
        if (rows > 0) {
            inserted += rows;
        }
    }

    m_db.commit();
    QSqlQuery setMeta(m_db);
    setMeta.prepare("INSERT OR REPLACE INTO app_meta (key, value) VALUES ('catalog_imported', ?)");
    setMeta.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    setMeta.exec();

    if (inserted > 0) {
        setStatus(QString("Imported %1 SKU record(s) from existing CSV.").arg(inserted), true);
        QJsonObject importDetails;
        importDetails.insert("source_csv", csvPath);
        importDetails.insert("inserted_rows", inserted);
        logAction("CATALOG_IMPORT",
                  "sku_catalog",
                  QString(),
                  QString::fromUtf8(QJsonDocument(importDetails).toJson(QJsonDocument::Compact)),
                  QString(),
                  "Data Import",
                  "IMPORT",
                  true,
                  QString(),
                  QString::number(inserted));
    }

    QFile flag(flagPath);
    if (flag.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&flag);
        out << QDateTime::currentDateTime().toString(Qt::ISODate);
    }
    return true;
}

void MainWindow::loadCategories() {
    if (!m_categoryCombo) {
        appendRunLog("loadCategories: m_categoryCombo is null");
        return;
    }
    m_categoryCombo->clear();
    QSqlQuery q(m_db);
    if (!q.exec("SELECT category_digit, category_name, MIN(id) AS first_id "
                "FROM sku_rules "
                "GROUP BY category_digit, category_name "
                "ORDER BY first_id")) {
        return;
    }
    while (q.next()) {
        const QString categoryDigit = q.value(0).toString();
        const QString categoryName = q.value(1).toString();
        m_categoryCombo->addItem(formatRuleDisplayText(categoryDigit, categoryName), categoryDigit);
        m_categoryCombo->setItemData(m_categoryCombo->count() - 1, categoryName, kRuleNameRole);
    }
}

void MainWindow::loadSubCategories(const QString &categoryDigit) {
    if (!m_subCategoryCombo) {
        appendRunLog("loadSubCategories: m_subCategoryCombo is null");
        return;
    }
    m_subCategoryCombo->clear();
    QSqlQuery q(m_db);
    q.prepare("SELECT subcategory_digit, subcategory_name FROM sku_rules "
              "WHERE category_digit = ? ORDER BY id");
    q.addBindValue(categoryDigit);
    if (!q.exec()) {
        return;
    }
    while (q.next()) {
        const QString subCategoryDigit = q.value(0).toString();
        const QString subCategoryName = q.value(1).toString();
        m_subCategoryCombo->addItem(formatRuleDisplayText(subCategoryDigit, subCategoryName), subCategoryDigit);
        m_subCategoryCombo->setItemData(m_subCategoryCombo->count() - 1, subCategoryName, kRuleNameRole);
    }
}

int MainWindow::firstAvailableItemSerial(const QString &categoryName, const QString &subCategoryName) const {
    const QString trimmedCategory = categoryName.trimmed();
    const QString trimmedSubCategory = subCategoryName.trimmed();
    if (trimmedCategory.isEmpty() || trimmedSubCategory.isEmpty()) {
        return 1;
    }

    QSqlQuery q(m_db);
    q.prepare("SELECT item_serial FROM sku_catalog_active "
              "WHERE category_code = ? AND sub_category = ? AND item_serial > 0 "
              "ORDER BY item_serial ASC");
    q.addBindValue(trimmedCategory);
    q.addBindValue(trimmedSubCategory);

    int candidate = 1;
    if (q.exec()) {
        while (q.next()) {
            const int serial = q.value(0).toInt();
            if (serial < candidate) {
                continue;
            }
            if (serial > candidate) {
                break;
            }
            ++candidate;
        }
    }
    return candidate;
}

void MainWindow::updateSerialsAndSku(bool resetVariation) {
    const QString categoryDigit = m_categoryCombo->currentData().toString();
    const QString subCategoryDigit = m_subCategoryCombo->currentData().toString();
    const QString categoryText = comboRuleName(m_categoryCombo);
    const QString subCategoryText = comboRuleName(m_subCategoryCombo);

    if (categoryDigit.isEmpty() || subCategoryDigit.isEmpty()) {
        return;
    }

    const int newSerial = firstAvailableItemSerial(categoryText, subCategoryText);

    m_itemSerialSpin->blockSignals(true);
    m_variationSpin->blockSignals(true);
    m_itemSerialSpin->setValue(newSerial);
    if (resetVariation) {
        m_variationSpin->setValue(1);
    }
    m_itemSerialSpin->blockSignals(false);
    m_variationSpin->blockSignals(false);

    updateSkuPreview();
}

void MainWindow::updateSkuPreview() {
    const QString categoryDigit = m_categoryCombo->currentData().toString();
    const QString subCategoryDigit = m_subCategoryCombo->currentData().toString();
    const QString sku = computeSku(categoryDigit, subCategoryDigit, m_itemSerialSpin->value(), m_variationSpin->value());
    m_skuField->setText(sku);
    int barcodeIndex = m_barcodeSkuCombo->findData(sku);
    if (barcodeIndex < 0) {
        barcodeIndex = m_barcodeSkuCombo->findText(sku, Qt::MatchStartsWith);
    }
    if (barcodeIndex >= 0) {
        m_barcodeSkuCombo->setCurrentIndex(barcodeIndex);
    }
    updateNextBarcodeSerial();
}

QString MainWindow::currentSkuValue() const {
    const QString manual = normalizedText(m_skuField->text());
    if (!manual.isEmpty()) {
        return manual;
    }
    const QString categoryDigit = m_categoryCombo->currentData().toString();
    const QString subCategoryDigit = m_subCategoryCombo->currentData().toString();
    return computeSku(categoryDigit, subCategoryDigit, m_itemSerialSpin->value(), m_variationSpin->value());
}

void MainWindow::updateNextBarcodeSerial() {
    const int quarter = selectedBarcodeQuarter();
    const int year = selectedBarcodeYear();

    const QString sku = extractSkuFromDisplay(m_barcodeSkuCombo->currentText());
    if (sku.isEmpty()) {
        m_barcodeNextSerialField->setText("1");
        updateBarcodeSkuDetails();
        return;
    }
    const int nextSerial = fetchNextBarcodeSerial(sku, year, quarter);
    m_barcodeNextSerialField->setText(QString::number(nextSerial));
    updateBarcodeSkuDetails();
}

void MainWindow::onBarcodePeriodChanged() {
    updateNextBarcodeSerial();
}

QString MainWindow::selectedBarcodePrefix() const {
    if (!m_barcodePrefixCombo) {
        return "SK";
    }
    QString prefix = m_barcodePrefixCombo->currentData().toString().trimmed().toUpper();
    if (prefix.isEmpty()) {
        prefix = m_barcodePrefixCombo->currentText().trimmed().left(2).toUpper();
    }
    return normalizeBarcodePrefix(prefix);
}

QString MainWindow::barcodePrefixFromValue(const QString &barcodeValue) const {
    return normalizeBarcodePrefix(barcodeValue);
}

QString MainWindow::buildBarcodeValue(const QString &sku, int serial, int year, int quarter, const QString &prefix) const {
    // Canonical QR format: prefix + normalized SKU + quarter + two-digit year + fixed-width serial.
    const QString normalizedPrefix = normalizeBarcodePrefix(prefix);
    return QString("%1%2%3%4%5")
        .arg(normalizedPrefix)
        .arg(sku.trimmed().toUpper().remove(' '))
        .arg(quarter)
        .arg(year % 100, 2, 10, QLatin1Char('0'))
        .arg(serial, 5, 10, QLatin1Char('0'));
}

QString MainWindow::stickerWebsiteForPrefix(const QString &prefix) const {
    const QString normalizedPrefix = normalizeBarcodePrefix(prefix);
    if (normalizedPrefix == "SD") {
        return "www.skylarkdrones.com";
    }
    if (normalizedPrefix == "SK") {
        return "www.skykart.in";
    }
    return "Skylark Drones Manufacturing\nPrivate Limited";
}

QImage MainWindow::stickerLogoForPrefix(const QString &prefix) const {
    const QString normalizedPrefix = normalizeBarcodePrefix(prefix);
    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList logoNames;
    if (normalizedPrefix == "SD") {
        logoNames << "SD logo .png" << "SD logo.png"
                  << "SD sticker logo .png" << "SD sticker logo.png";
    } else if (normalizedPrefix == "SM") {
        logoNames << "SDMPL Logo.png" << "SDMPL logo.png";
    } else {
        logoNames << "SK Logo.png" << "SK logo.png"
                  << "SK sticker logo.png";
    }

    QStringList candidates;
    if (normalizedPrefix == "SD") {
        candidates << ":/assets/sd_sticker_logo.png";
    } else if (normalizedPrefix == "SM") {
        candidates << ":/assets/sm_sticker_logo.png";
    } else {
        candidates << ":/assets/sk_sticker_logo.png";
    }
    candidates << ":/assets/sticker_logo.png";
    for (const QString &name : logoNames) {
        candidates << QDir(appDir).filePath("Assets/" + name)
                   << QDir(appDir).filePath("../Assets/" + name)
                   << QDir(appDir).filePath("../../Assets/" + name)
                   << QDir::current().filePath("Assets/" + name);
    }

    QImage logo;
    for (const QString &candidate : candidates) {
        if (!candidate.startsWith(":/") && !QFile::exists(candidate)) {
            continue;
        }
        if (logo.load(candidate) && !logo.isNull()) {
            return logo;
        }
    }

    return QImage();
}

int MainWindow::fetchNextBarcodeSerial(const QString &sku, int year, int quarter) const {
    const QList<int> serials = fetchNextBarcodeSerials(sku, year, quarter, 1);
    return serials.isEmpty() ? 1 : serials.first();
}

QList<int> MainWindow::fetchNextBarcodeSerials(const QString &sku, int year, int quarter, int quantity) const {
    QList<int> nextSerials;
    if (quantity <= 0 || sku.trimmed().isEmpty()) {
        return nextSerials;
    }

    QSqlQuery q(m_db);
    q.prepare("SELECT serial FROM barcode_log_active "
              "WHERE sku = ? AND year = ? AND quarter = ? AND serial > 0 "
              "ORDER BY serial ASC");
    q.addBindValue(sku);
    q.addBindValue(year);
    q.addBindValue(quarter);
    QList<int> existingSerials;
    if (q.exec()) {
        while (q.next()) {
            if (!q.isNull(0)) {
                existingSerials.append(q.value(0).toInt());
            }
        }
    }

    int candidate = 1;
    int index = 0;
    while (nextSerials.size() < quantity) {
        while (index < existingSerials.size() && existingSerials.at(index) < candidate) {
            ++index;
        }

        bool alreadyUsed = false;
        while (index < existingSerials.size() && existingSerials.at(index) == candidate) {
            alreadyUsed = true;
            ++index;
        }

        if (alreadyUsed) {
            ++candidate;
            continue;
        }

        nextSerials.append(candidate);
        ++candidate;
    }

    return nextSerials;
}

int MainWindow::currentQuarter() const {
    const int month = QDate::currentDate().month();
    if (month >= 4 && month <= 6) {
        return 1;
    }
    if (month >= 7 && month <= 9) {
        return 2;
    }
    if (month >= 10 && month <= 12) {
        return 3;
    }
    return 4;
}

int MainWindow::currentYear() const {
    const QDate today = QDate::currentDate();
    const int year = today.year();
    const int month = today.month();
    return (month >= 4) ? year : (year - 1);
}

int MainWindow::selectedBarcodeQuarter() const {
    if (m_barcodeQuarterCombo) {
        const int quarter = m_barcodeQuarterCombo->currentData().toInt();
        if (quarter >= 1 && quarter <= 4) {
            return quarter;
        }
    }
    return currentQuarter();
}

int MainWindow::selectedBarcodeYear() const {
    if (m_barcodeYearCombo) {
        const int year = m_barcodeYearCombo->currentData().toInt();
        if (year > 0) {
            return year;
        }
    }
    return currentYear();
}

bool MainWindow::skuExists(const QString &sku) const {
    QSqlQuery q(m_db);
    q.prepare("SELECT COUNT(*) FROM sku_catalog_active WHERE sku = ?");
    q.addBindValue(sku);
    if (q.exec() && q.next()) {
        return q.value(0).toInt() > 0;
    }
    return false;
}

void MainWindow::loadSkuList(const QString &filter, bool preserveText) {
    if (!m_barcodeSkuCombo) {
        appendRunLog("loadSkuList: m_barcodeSkuCombo is null");
        return;
    }

    const QString trimmed = filter.trimmed();
    QString currentText;
    int cursorPos = -1;
    QLineEdit *edit = m_barcodeSkuCombo->lineEdit();
    if (preserveText && edit) {
        currentText = edit->text();
        cursorPos = edit->cursorPosition();
    }

    QSignalBlocker blocker(m_barcodeSkuCombo);
    const bool editSignalsBlocked = edit ? edit->blockSignals(true) : false;
    m_barcodeSkuCombo->clear();

    QSqlQuery q(m_db);
    if (trimmed.isEmpty()) {
        q.prepare("SELECT sku, part_number, part_name FROM sku_catalog_active ORDER BY sku");
    } else {
        q.prepare(
            "SELECT sku, part_number, part_name FROM sku_catalog_active "
            "WHERE sku LIKE ? OR part_number LIKE ? OR part_name LIKE ? "
            "ORDER BY sku");
        const QString like = "%" + trimmed + "%";
        q.addBindValue(like);
        q.addBindValue(like);
        q.addBindValue(like);
    }

    if (q.exec()) {
        while (q.next()) {
            const QString sku = q.value(0).toString().trimmed().toUpper();
            const QString partName = q.value(2).toString().trimmed();
            const QString display = partName.isEmpty()
                                        ? QString("%1 | -").arg(sku)
                                        : QString("%1 | %2").arg(sku, partName);
            m_barcodeSkuCombo->addItem(display, sku);
        }
    }

    if (preserveText) {
        m_barcodeSkuCombo->setCurrentIndex(-1);
        if (edit) {
            edit->setText(currentText);
            if (cursorPos >= 0) {
                edit->setCursorPosition(cursorPos);
            }
        }
    } else {
        if (m_barcodeSkuCombo->count() > 0) {
            m_barcodeSkuCombo->setCurrentIndex(0);
        } else {
            m_barcodeSkuCombo->setCurrentIndex(-1);
        }
        updateNextBarcodeSerial();
        updateBarcodeSkuDetails();
    }

    if (edit) {
        edit->blockSignals(editSignalsBlocked);
    }
}

void MainWindow::populateBarcodeModel(const QList<QStringList> &rows) {
    m_barcodeModel->clear();
    const QStringList headers = { "QR Code", "SKU", "Serial", "Quarter", "Year", "Created At" };
    m_barcodeModel->setColumnCount(headers.size());
    m_barcodeModel->setHorizontalHeaderLabels(headers);

    for (const auto &row : rows) {
        QList<QStandardItem *> items;
        items.reserve(headers.size());
        for (int i = 0; i < headers.size(); ++i) {
            items.append(new QStandardItem(row.value(i)));
        }
        m_barcodeModel->appendRow(items);
    }

    m_barcodeTableView->resizeColumnsToContents();
    if (!rows.isEmpty()) {
        m_barcodeTableView->selectRow(0);
    } else {
        m_barcodePreview->setText("No QR Code");
        m_barcodeValueField->clear();
        m_barcodeImage = QImage();
    }
}

void MainWindow::updateDashboardMetrics() {
    if (!m_totalSkusValueLabel || !m_totalBarcodesValueLabel || !m_quarterBarcodesValueLabel) {
        appendRunLog("updateDashboardMetrics: metric labels missing");
        return;
    }
    QSqlQuery q(m_db);

    int totalSkus = 0;
    if (q.exec("SELECT COUNT(*) FROM sku_catalog_active") && q.next()) {
        totalSkus = q.value(0).toInt();
    }

    int totalBarcodes = 0;
    if (q.exec("SELECT COUNT(*) FROM barcode_log_active") && q.next()) {
        totalBarcodes = q.value(0).toInt();
    }

    const int quarter = currentQuarter();
    const int year = currentYear();
    int quarterCount = 0;
    q.prepare("SELECT COUNT(*) FROM barcode_log_active WHERE year = ? AND quarter = ?");
    q.addBindValue(year);
    q.addBindValue(quarter);
    if (q.exec() && q.next()) {
        quarterCount = q.value(0).toInt();
    }

    m_totalSkusValueLabel->setText(QString::number(totalSkus));
    m_totalBarcodesValueLabel->setText(QString::number(totalBarcodes));
    m_quarterBarcodesValueLabel->setText(QString::number(quarterCount));

    int sdSerials = 0;
    int skSerials = 0;
    int smSerials = 0;
    QSqlQuery prefixQuery(m_db);
    if (prefixQuery.exec("SELECT SUBSTR(UPPER(COALESCE(barcode, '')), 1, 2) AS prefix, COUNT(*) "
                         "FROM barcode_log_active GROUP BY prefix")) {
        while (prefixQuery.next()) {
            const QString prefix = prefixQuery.value(0).toString().trimmed().toUpper();
            const int count = prefixQuery.value(1).toInt();
            if (prefix == "SD") {
                sdSerials = count;
            } else if (prefix == "SK") {
                skSerials = count;
            } else if (prefix == "SM") {
                smSerials = count;
            }
        }
    }

    if (m_sdSerialsValueLabel) {
        m_sdSerialsValueLabel->setText(QString::number(sdSerials));
    }
    if (m_skSerialsValueLabel) {
        m_skSerialsValueLabel->setText(QString::number(skSerials));
    }
    if (m_smSerialsValueLabel) {
        m_smSerialsValueLabel->setText(QString::number(smSerials));
    }

    scheduleLatestSkuCardsRefresh();
}

void MainWindow::scheduleLatestSkuCardsRefresh(int delayMs) {
    if (!m_latestSkuRefreshTimer) {
        m_latestSkuRefreshTimer = new QTimer(this);
        m_latestSkuRefreshTimer->setSingleShot(true);
        connect(m_latestSkuRefreshTimer, &QTimer::timeout, this, &MainWindow::refreshLatestSkuCards);
    }

    const int safeDelay = qMax(0, delayMs);
    m_latestSkuRefreshTimer->start(safeDelay);
}

void MainWindow::updateBarcodeSkuDetails() {
    auto updateSkuDetailsClickBehavior = [this](const QString &skuValue) {
        if (!m_barcodeSkuDetailsGroupBox) {
            return;
        }

        const bool hasSku = !skuValue.trimmed().isEmpty();
        const QVariant skuProperty = hasSku ? QVariant(skuValue) : QVariant();
        const Qt::CursorShape cursor = hasSku ? Qt::PointingHandCursor : Qt::ArrowCursor;

        auto applyToWidget = [&](QWidget *widget) {
            if (!widget) {
                return;
            }
            widget->setProperty("sku", skuProperty);
            widget->setCursor(cursor);
        };

        applyToWidget(m_barcodeSkuDetailsGroupBox);
        const QList<QWidget *> clickTargets = m_barcodeSkuDetailsGroupBox->findChildren<QWidget *>();
        for (QWidget *target : clickTargets) {
            applyToWidget(target);
        }
    };

    const QString sku = extractSkuFromDisplay(m_barcodeSkuCombo->currentText());
    if (sku.isEmpty()) {
        m_barcodePartNameValueLabel->setText("-");
        m_barcodePartNumberValueLabel->setText("-");
        if (m_barcodeQuantityWordsValueLabel) {
            m_barcodeQuantityWordsValueLabel->setText("-");
        }
        loadBarcodeSkuImage(QByteArray(), QString());
        updateSkuDetailsClickBehavior(QString());
        return;
    }

    QSqlQuery q(m_db);
    q.prepare("SELECT part_name, part_number, image_blob, image_path FROM sku_catalog_active WHERE sku = ?");
    q.addBindValue(sku);
    if (q.exec() && q.next()) {
        m_barcodePartNameValueLabel->setText(q.value(0).toString());
        m_barcodePartNumberValueLabel->setText(q.value(1).toString());
        loadBarcodeSkuImage(q.value(2).toByteArray(), q.value(3).toString());
        updateSkuDetailsClickBehavior(sku);
    } else {
        m_barcodePartNameValueLabel->setText("-");
        m_barcodePartNumberValueLabel->setText("-");
        loadBarcodeSkuImage(QByteArray(), QString());
        updateSkuDetailsClickBehavior(QString());
    }

    updateQuantityWordsLabels(sku);
}

void MainWindow::loadBarcodeSkuImage(const QByteArray &data, const QString &legacyPath) {
    loadImageLabelFromData(m_barcodeSkuImageLabel, data, legacyPath, AppGlobals::noImageText());
}

void MainWindow::refreshLatestSkuCards() {
    if (!m_latestSkuGridLayout || !m_latestSkuContainer) {
        appendRunLog("refreshLatestSkuCards: grid/layout null");
        return;
    }
    if (m_refreshingLatestSkuCards) {
        return;
    }
    m_refreshingLatestSkuCards = true;
    m_latestSkuContainer->setUpdatesEnabled(false);

    int availableWidth = m_latestSkuContainer->width();
    int availableHeight = m_latestSkuContainer->height();
    if (m_latestSkuScrollArea && m_latestSkuScrollArea->viewport()) {
        availableWidth = m_latestSkuScrollArea->viewport()->width();
        availableHeight = m_latestSkuScrollArea->viewport()->height();
    }
    availableWidth = qMax(320, availableWidth);
    availableHeight = qMax(220, availableHeight);

    const int outerMargin = qBound(8, availableWidth / 65, 20);
    const int spacing = qBound(8, availableWidth / 75, 22);
    m_latestSkuGridLayout->setContentsMargins(outerMargin, outerMargin, outerMargin, outerMargin);
    m_latestSkuGridLayout->setHorizontalSpacing(spacing);
    m_latestSkuGridLayout->setVerticalSpacing(spacing);
    m_latestSkuGridLayout->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    while (QLayoutItem *item = m_latestSkuGridLayout->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            delete w;
        }
        delete item;
    }

    const int usableWidth = qMax(1, availableWidth - outerMargin * 2);
    const int usableHeight = qMax(1, availableHeight - outerMargin * 2);
    const int minCardWidthPx = 250;
    const int maxCardWidthPx = 420;
    int columns = qMax(1, (usableWidth + spacing) / (minCardWidthPx + spacing));
    int cardWidthPx = qMax(minCardWidthPx, (usableWidth - (columns - 1) * spacing) / columns);
    if (cardWidthPx > maxCardWidthPx) {
        const int preferredColumns = qMax(1, (usableWidth + spacing) / (maxCardWidthPx + spacing));
        columns = qMax(columns, preferredColumns);
        cardWidthPx = qMax(minCardWidthPx, (usableWidth - (columns - 1) * spacing) / columns);
    }
    cardWidthPx = qBound(minCardWidthPx, cardWidthPx, maxCardWidthPx);
    const int cardHeightPx = qBound(150, static_cast<int>(cardWidthPx * 0.58), 230);
    const int rows = qMax(1, (usableHeight + spacing) / (cardHeightPx + spacing));
    const int cardLimit = qMax(6, columns * rows);

    const int year = selectedBarcodeYear();
    const int quarter = selectedBarcodeQuarter();
    QSqlQuery q(m_db);
    q.prepare(
        "SELECT s.sku, s.part_name, s.image_blob, s.image_path, s.created_at, "
        "COALESCE(t.total_count, 0) AS total_serials, "
        "COALESCE(qtr.qtr_count, 0) AS quarter_incoming "
        "FROM sku_catalog_active s "
        "LEFT JOIN (SELECT sku, COUNT(*) AS total_count FROM barcode_log_active GROUP BY sku) t "
        "  ON t.sku = s.sku "
        "LEFT JOIN (SELECT sku, COUNT(*) AS qtr_count FROM barcode_log_active WHERE year = ? AND quarter = ? GROUP BY sku) qtr "
        "  ON qtr.sku = s.sku "
        "ORDER BY datetime(s.created_at) DESC, s.id DESC LIMIT ?");
    q.addBindValue(year);
    q.addBindValue(quarter);
    q.addBindValue(cardLimit);
    if (!q.exec()) {
        appendRunLog(QString("refreshLatestSkuCards: query failed: %1").arg(q.lastError().text()));
    }

    const qreal skuSizePt = qBound(8.0, cardWidthPx / 34.0, 13.0);
    const qreal nameSizePt = qBound(9.0, cardWidthPx / 23.0, 18.0);
    const qreal metaSizePt = qBound(8.0, cardWidthPx / 37.0, 12.0);
    const qreal dateSizePt = qBound(7.0, cardWidthPx / 40.0, 11.0);

    int index = 0;
    while (q.next()) {
        const QString sku = q.value(0).toString();
        const QString partName = q.value(1).toString();
        const QByteArray imageData = q.value(2).toByteArray();
        const QString imagePath = q.value(3).toString();
        const QString createdAt = q.value(4).toString();
        const int totalSerials = q.value(5).toInt();
        const int quarterIncoming = q.value(6).toInt();

        auto *card = new QFrame(m_latestSkuContainer);
        card->setObjectName("skuCard");
        card->setFixedSize(cardWidthPx, cardHeightPx);
        card->setAttribute(Qt::WA_StyledBackground, true);

        auto *shadow = new QGraphicsDropShadowEffect(card);
        shadow->setBlurRadius(qBound(10, cardWidthPx / 18, 18));
        shadow->setOffset(0, 6);
        shadow->setColor(QColor(0, 0, 0, 140));
        card->setGraphicsEffect(shadow);

        const int contentMargin = qBound(8, cardWidthPx / 30, 16);
        const int contentSpacing = qBound(6, cardWidthPx / 40, 14);
        auto *cardLayout = new QHBoxLayout(card);
        cardLayout->setContentsMargins(contentMargin, contentMargin, contentMargin, contentMargin);
        cardLayout->setSpacing(contentSpacing);

        const int imageSizePx = qBound(64, static_cast<int>(cardHeightPx * 0.52), 120);
        auto *imageLabel = new QLabel(card);
        imageLabel->setObjectName("skuCardImage");
        imageLabel->setFixedSize(imageSizePx, imageSizePx);
        imageLabel->setAlignment(Qt::AlignCenter);
        loadImageLabelFromData(imageLabel, imageData, imagePath, AppGlobals::noImageText());

        auto *skuLabel = new QLabel(QString("SKU: %1").arg(sku), card);
        skuLabel->setObjectName("skuCardSku");
        QFont skuFont = skuLabel->font();
        skuFont.setPointSizeF(skuSizePt);
        skuFont.setBold(true);
        skuLabel->setFont(skuFont);

        auto *nameLabel = new QLabel(partName, card);
        nameLabel->setWordWrap(true);
        nameLabel->setObjectName("skuCardName");
        QFont nameFont = nameLabel->font();
        nameFont.setPointSizeF(nameSizePt);
        nameFont.setBold(true);
        nameLabel->setFont(nameFont);
        nameLabel->setMaximumHeight(qBound(42, static_cast<int>(cardHeightPx * 0.35), 92));

        auto *incomingLabel = new QLabel(
            QString("Incoming (Q%1 %2): %3").arg(quarter).arg(year).arg(quarterIncoming), card);
        incomingLabel->setObjectName("skuCardMeta");
        QFont incomingFont = incomingLabel->font();
        incomingFont.setPointSizeF(metaSizePt);
        incomingLabel->setFont(incomingFont);

        auto *totalLabel = new QLabel(QString("Total Serials: %1").arg(totalSerials), card);
        totalLabel->setObjectName("skuCardMeta");
        QFont totalFont = totalLabel->font();
        totalFont.setPointSizeF(metaSizePt);
        totalLabel->setFont(totalFont);

        auto *dateLabel = new QLabel(createdAt.isEmpty() ? AppGlobals::noDateText()
                                                         : QString("Date: %1").arg(createdAt), card);
        dateLabel->setObjectName("skuCardDate");
        QFont dateFont = dateLabel->font();
        dateFont.setPointSizeF(dateSizePt);
        dateLabel->setFont(dateFont);

        auto *textLayout = new QVBoxLayout();
        textLayout->setContentsMargins(0, 0, 0, 0);
        textLayout->setSpacing(qBound(2, cardHeightPx / 60, 8));
        textLayout->addWidget(skuLabel);
        textLayout->addWidget(nameLabel);
        textLayout->addWidget(incomingLabel);
        textLayout->addWidget(totalLabel);
        textLayout->addWidget(dateLabel);
        textLayout->addStretch(1);

        cardLayout->addWidget(imageLabel, 0, Qt::AlignVCenter);
        cardLayout->addLayout(textLayout, 1);

        auto registerClickTarget = [&](QWidget *widget) {
            widget->setProperty("sku", sku);
            widget->setCursor(Qt::PointingHandCursor);
            widget->installEventFilter(this);
        };

        registerClickTarget(card);
        registerClickTarget(imageLabel);
        registerClickTarget(skuLabel);
        registerClickTarget(nameLabel);
        registerClickTarget(incomingLabel);
        registerClickTarget(totalLabel);
        registerClickTarget(dateLabel);

        const int row = index / columns;
        const int col = index % columns;
        m_latestSkuGridLayout->addWidget(card, row, col);
        index++;
    }

    if (index == 0) {
        auto *emptyLabel = new QLabel("No SKUs yet.", m_latestSkuContainer);
        emptyLabel->setAlignment(Qt::AlignCenter);
        emptyLabel->setStyleSheet("QLabel { color: #a7b3c6; }");
        m_latestSkuGridLayout->addWidget(emptyLabel, 0, 0);
    }

    m_latestSkuContainer->setUpdatesEnabled(true);
    m_latestSkuContainer->update();
    m_refreshingLatestSkuCards = false;
}

QImage MainWindow::renderQrCode(const QString &value) const {
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        return QImage();
    }

    const QByteArray payload = trimmed.toUtf8();
    try {
        const qrcodegen::QrCode qr =
            qrcodegen::QrCode::encodeText(payload.constData(), qrcodegen::QrCode::Ecc::MEDIUM);

        const int size = qr.getSize();
        const int border = 4;
        const int scale = 4;
        const int imageSize = (size + border * 2) * scale;

        QImage image(imageSize, imageSize, QImage::Format_ARGB32);
        image.fill(Qt::white);

        QPainter painter(&image);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::black);

        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                if (qr.getModule(x, y)) {
                    painter.drawRect((x + border) * scale, (y + border) * scale, scale, scale);
                }
            }
        }
        painter.end();
        return image;
    } catch (const std::exception &) {
        return QImage();
    }
}

QString MainWindow::computeSku(const QString &categoryDigit,
                               const QString &subCategoryDigit,
                               int itemSerial,
                               int variation) const {
    if (categoryDigit.isEmpty() || subCategoryDigit.isEmpty()) {
        return QString();
    }
    const QString itemSerialPadded = QString("%1").arg(itemSerial, 3, 10, QLatin1Char('0'));
    const QString variationCode = getVariationCode(variation);
    return categoryDigit + subCategoryDigit + itemSerialPadded + variationCode;
}

QString MainWindow::getVariationCode(int num) const {
    if (num >= 1 && num <= 9) {
        return QString::number(num);
    }
    if (num >= 10 && num <= 35) {
        const QChar letter('A' + (num - 10));
        return QString(letter);
    }
    return "X";
}

void MainWindow::setStatus(const QString &message, bool ok) {
    const QString text = message.trimmed();
    if (m_statusLabel) {
        m_statusLabel->setText(text);
    }
    const QString color = ok ? "#3fb950" : "#f85149";
    if (m_statusLabel) {
        m_statusLabel->setStyleSheet(QString("color: %1;").arg(color));
    }
    if (m_statusBarMessageLabel) {
        m_statusBarMessageLabel->setText(text);
        if (text.isEmpty()) {
            m_statusBarMessageLabel->setStyleSheet("QLabel { padding-left: 4px; }");
        } else {
            m_statusBarMessageLabel->setStyleSheet(QString("QLabel { color: %1; padding-left: 4px; font-weight: 600; }").arg(color));
        }
    }
    showToast(text, ok);
    updateNoDbBanner();
}

void MainWindow::showToast(const QString &message, bool ok) {
    // Transient pop-up confirmation near the bottom of the window: green for
    // success, red for failures. Auto-dismisses so it never blocks the operator.
    const QString text = message.trimmed();
    if (text.isEmpty() || !isVisible()) {
        return;
    }
    if (!m_toastLabel) {
        m_toastLabel = new QLabel(this);
        m_toastLabel->setWordWrap(true);
        m_toastLabel->setAlignment(Qt::AlignCenter);
        m_toastLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_toastLabel->hide();
    }
    if (!m_toastTimer) {
        m_toastTimer = new QTimer(this);
        m_toastTimer->setSingleShot(true);
        connect(m_toastTimer, &QTimer::timeout, this, [this]() {
            if (m_toastLabel) {
                m_toastLabel->hide();
            }
        });
    }

    m_toastLabel->setObjectName(ok ? "toastSuccessLabel" : "toastErrorLabel");
    m_toastLabel->setText(QString("%1  %2").arg(ok ? QStringLiteral("✓") : QStringLiteral("⚠"), text));
    // Re-polish so the object-name-based stylesheet rule is re-applied.
    m_toastLabel->style()->unpolish(m_toastLabel);
    m_toastLabel->style()->polish(m_toastLabel);

    m_toastLabel->setMaximumWidth(qMax(280, width() / 2));
    m_toastLabel->adjustSize();
    const int x = (width() - m_toastLabel->width()) / 2;
    const int y = height() - m_toastLabel->height() - 56;
    m_toastLabel->move(qMax(0, x), qMax(0, y));
    m_toastLabel->raise();
    m_toastLabel->show();
    m_toastTimer->start(ok ? 2600 : 4500);
}

void MainWindow::updateNoDbBanner() {
    if (!m_noDbBannerLabel) {
        return;
    }
    if (m_db.isOpen()) {
        const QString activeDbPath = QDir::toNativeSeparators(m_db.databaseName().trimmed());
        const QString dbText = activeDbPath.isEmpty() ? QStringLiteral("Connected")
                                                      : QString("Connected: %1").arg(activeDbPath);
        m_noDbBannerLabel->setText(QString("Database status: %1").arg(dbText));
        m_noDbBannerLabel->setStyleSheet(
            "QLabel#noDbBannerLabel { background-color: #0d2118; color: #3fb950; border: 1px solid #196c2e; border-radius: 8px; padding: 7px 12px; font-weight: 600; }");
    } else {
        m_noDbBannerLabel->setText("Database status: Not connected. Use File \342\206\222 Load DB... to open or create a database.");
        m_noDbBannerLabel->setStyleSheet(
            "QLabel#noDbBannerLabel { background-color: #2d1f00; color: #ffa657; border: 1px solid #7d4e00; border-radius: 8px; padding: 7px 12px; font-weight: 600; }");
    }
    m_noDbBannerLabel->setVisible(true);
}

void MainWindow::populateResultsModel(const QList<QStringList> &rows) {
    m_resultsModel->clear();
    const QStringList headers = {
        "ID",
        "SI.NO",
        "Part Number",
        "SKU",
        "Part Name",
        "Category Code",
        "Sub Category",
        "Item Serial",
        "Unique Variation",
        "Description",
        "Storage",
        "Rack Number",
        "Bin Number",
        "Dimensions",
        "Weight (kg)",
        "Product Family",
        "Image",
        "Comments",
        "Created At"
    };

    m_resultsModel->setColumnCount(headers.size());
    m_resultsModel->setHorizontalHeaderLabels(headers);

    for (const auto &row : rows) {
        QList<QStandardItem *> items;
        items.reserve(headers.size());
        for (int i = 0; i < headers.size(); ++i) {
            auto *item = new QStandardItem(row.value(i));
            items.append(item);
        }
        m_resultsModel->appendRow(items);
    }

    m_resultsView->resizeColumnsToContents();
    m_resultsView->setColumnHidden(0, true);

    // Cap long-text columns so they don't dominate the view.
    // Users can still drag column borders to resize freely.
    static const int kMaxWideCol  = 220; // Description, Comments
    static const int kMaxNameCol  = 190; // Part Name
    for (int col : {4, 9, 17}) {
        const int limit = (col == 4) ? kMaxNameCol : kMaxWideCol;
        if (m_resultsView->columnWidth(col) > limit)
            m_resultsView->setColumnWidth(col, limit);
    }

    if (m_searchNotFoundLabel) {
        const bool noResults = rows.isEmpty();
        m_searchNotFoundLabel->setVisible(noResults);
        if (noResults) {
            m_searchNotFoundLabel->setGeometry(m_resultsView->viewport()->rect());
        }
    }
}

void MainWindow::showResultsContextMenu(const QPoint &pos) {
    const QModelIndex index = m_resultsView->indexAt(pos);
    if (!index.isValid()) {
        return;
    }

    QMenu menu(this);
    QAction *moreInfo   = menu.addAction(QIcon::fromTheme("dialog-information"), "More Information...");
    menu.addSeparator();
    QAction *copyCell   = menu.addAction("Copy Cell");
    QAction *copyRow    = menu.addAction("Copy Row");
    QAction *copyColumn = menu.addAction("Copy Column");
    QAction *selectedAction = menu.exec(m_resultsView->viewport()->mapToGlobal(pos));
    if (!selectedAction) {
        return;
    }

    if (selectedAction == moreInfo) {
        const auto *skuItem = m_resultsModel->item(index.row(), 3); // SKU column
        if (skuItem) {
            showSkuDetailsDialog(skuItem->text());
        }
        return;
    }

    QString text;
    if (selectedAction == copyCell) {
        text = m_resultsModel->item(index.row(), index.column())->text();
    } else if (selectedAction == copyRow) {
        QStringList cells;
        for (int col = 0; col < m_resultsModel->columnCount(); ++col) {
            if (m_resultsView->isColumnHidden(col)) {
                continue;
            }
            cells << m_resultsModel->item(index.row(), col)->text();
        }
        text = cells.join('\t');
    } else if (selectedAction == copyColumn) {
        QStringList cells;
        for (int row = 0; row < m_resultsModel->rowCount(); ++row) {
            cells << m_resultsModel->item(row, index.column())->text();
        }
        text = cells.join('\n');
    }

    if (!text.isEmpty()) {
        QApplication::clipboard()->setText(text);
    }
}

void MainWindow::searchRecords() {
    if (!m_resultsModel || !m_resultsView) {
        appendRunLog("searchRecords: results model/view null");
        return;
    }
    const QString sku = normalizedText(m_searchSku->text()).toLower();
    const QString partNumber = normalizedText(m_searchPartNumber->text()).toLower();
    const QString partName = normalizedText(m_searchPartName->text()).toLower();

    QStringList clauses;
    QList<QVariant> binds;

    if (!sku.isEmpty()) {
        clauses << "LOWER(sku) LIKE ?";
        binds << ("%" + sku + "%");
    }
    if (!partNumber.isEmpty()) {
        clauses << "LOWER(part_number) LIKE ?";
        binds << ("%" + partNumber + "%");
    }
    if (!partName.isEmpty()) {
        clauses << "LOWER(part_name) LIKE ?";
        binds << ("%" + partName + "%");
    }

    if (clauses.isEmpty()) {
        const QString sql =
        "SELECT id, si_no, part_number, sku, part_name, category_code, sub_category, item_serial, unique_variation, "
        "description, storage, rack_number, bin_number, dimensions, "
        "CASE WHEN weight_unit IS NULL OR weight_unit = '' "
        "THEN CAST(weight_value AS TEXT) "
        "ELSE CAST(weight_value AS TEXT) || ' ' || weight_unit END AS weight_display, "
        "product_family, "
        "CASE WHEN image_blob IS NOT NULL AND length(image_blob) > 0 THEN 'Stored' "
        "WHEN image_path IS NOT NULL AND image_path <> '' THEN 'Path' ELSE '' END AS image_status, "
        "comments, created_at "
        "FROM sku_catalog_active ORDER BY si_no DESC";

        QSqlQuery q(m_db);
        if (!q.exec(sql)) {
            setStatus("Search failed.", false);
            return;
        }

        QList<QStringList> results;
        while (q.next()) {
            QStringList row;
            for (int i = 0; i < 19; ++i) {
                row << q.value(i).toString();
            }
            results.append(row);
        }

        populateResultsModel(results);
        if (results.isEmpty()) {
            setStatus("No records in database.", false);
            m_selectedId = 0;
            loadImageForSelectedId(0);
        } else {
            setStatus(QString("Showing all records (%1).").arg(results.size()), true);
            m_resultsView->selectRow(0);
        }
        return;
    }

    const QString sql =
        "SELECT id, si_no, part_number, sku, part_name, category_code, sub_category, item_serial, unique_variation, "
        "description, storage, rack_number, bin_number, dimensions, "
        "CASE WHEN weight_unit IS NULL OR weight_unit = '' "
        "THEN CAST(weight_value AS TEXT) "
        "ELSE CAST(weight_value AS TEXT) || ' ' || weight_unit END AS weight_display, "
        "product_family, "
        "CASE WHEN image_blob IS NOT NULL AND length(image_blob) > 0 THEN 'Stored' "
        "WHEN image_path IS NOT NULL AND image_path <> '' THEN 'Path' ELSE '' END AS image_status, "
        "comments, created_at "
        "FROM sku_catalog_active WHERE " + clauses.join(" OR ") + " ORDER BY si_no DESC";

    QSqlQuery q(m_db);
    q.prepare(sql);
    for (const auto &bind : binds) {
        q.addBindValue(bind);
    }

    if (!q.exec()) {
        setStatus("Search failed.", false);
        return;
    }

    QList<QStringList> results;
    while (q.next()) {
        QStringList row;
        for (int i = 0; i < 19; ++i) {
            row << q.value(i).toString();
        }
        results.append(row);
    }

    populateResultsModel(results);
    if (results.isEmpty()) {
        setStatus("No matching record found.", false);
        m_selectedId = 0;
        loadImageForSelectedId(0);
        if (m_searchQuantityWordsValueLabel) {
            m_searchQuantityWordsValueLabel->setText("-");
        }
    } else {
        setStatus(QString("Found %1 matching record(s).").arg(results.size()), true);
        m_resultsView->selectRow(0);
    }
}

void MainWindow::clearSearch() {
    m_searchSku->clear();
    m_searchPartNumber->clear();
    m_searchPartName->clear();
    m_resultsModel->clear();
    setStatus(QString(), true);
    m_selectedId = 0;
    loadImageForSelectedId(0);
    if (m_searchQuantityWordsValueLabel) {
        m_searchQuantityWordsValueLabel->setText("-");
    }
    searchRecords();
}

void MainWindow::fillFormFromSearch() {
    if (m_resultsModel->rowCount() == 0) {
        setStatus("No record available to fill the form.", false);
        return;
    }

    int row = 0;
    const QModelIndex currentIndex = m_resultsView->selectionModel()->currentIndex();
    if (currentIndex.isValid()) {
        row = currentIndex.row();
    }
    m_selectedId = m_resultsModel->item(row, 0)->text().toInt();
    const QString partNumber = m_resultsModel->item(row, 2)->text();
    const QString sku = m_resultsModel->item(row, 3)->text();
    const QString partName = m_resultsModel->item(row, 4)->text();
    const QString categoryCode = m_resultsModel->item(row, 5)->text();
    const QString subCategory = m_resultsModel->item(row, 6)->text();
    const int itemSerial = m_resultsModel->item(row, 7)->text().toInt();
    const int variation = m_resultsModel->item(row, 8)->text().toInt();
    const QString description = m_resultsModel->item(row, 9)->text();
    const QString storage = m_resultsModel->item(row, 10)->text();
    const QString rackNumber = m_resultsModel->item(row, 11)->text();
    const QString binNumber = m_resultsModel->item(row, 12)->text();
    const QString dimensions = m_resultsModel->item(row, 13)->text();
    const QString weightDisplay = m_resultsModel->item(row, 14)->text();
    const QString productFamily = m_resultsModel->item(row, 15)->text();
    const QString comments = m_resultsModel->item(row, 17)->text();

    m_partNameField->setText(partName);
    m_partNumberField->setText(partNumber);

    const int catIndex = findRuleComboIndex(m_categoryCombo, categoryCode);
    if (catIndex >= 0) {
        m_categoryCombo->setCurrentIndex(catIndex);
    }
    const int subIndex = findRuleComboIndex(m_subCategoryCombo, subCategory);
    if (subIndex >= 0) {
        m_subCategoryCombo->setCurrentIndex(subIndex);
    }

    m_itemSerialSpin->blockSignals(true);
    m_variationSpin->blockSignals(true);
    m_itemSerialSpin->setValue(itemSerial);
    m_variationSpin->setValue(variation);
    m_itemSerialSpin->blockSignals(false);
    m_variationSpin->blockSignals(false);

    m_skuField->setText(sku);
    QString normalizedDimensionsForDisplay;
    if (tryNormalizeDimensionsCm(dimensions, &normalizedDimensionsForDisplay)) {
        m_dimensionsField->setText(normalizedDimensionsForDisplay);
    } else {
        m_dimensionsField->setText(dimensions);
    }
    QString weightValueText = weightDisplay;
    if (weightValueText.contains(' ')) {
        weightValueText = weightValueText.section(' ', 0, 0);
    }
    m_weightField->setText(weightValueText);
    m_descriptionEdit->setPlainText(description);
    m_storageField->setText(storage);
    if (m_rackNumberField) {
        m_rackNumberField->setText(rackNumber);
    }
    if (m_binNumberField) {
        m_binNumberField->setText(binNumber);
    }
    m_productFamilyField->setText(productFamily);
    m_commentsEdit->setPlainText(comments);
    loadImageForSelectedId(m_selectedId);
    updateNextBarcodeSerial();
    updateQuantityWordsLabels(sku);

    setStatus("Form filled from search. Verify and update if needed.", true);
}

void MainWindow::saveForm() {
    if (!requireAccess(m_access.canAdd, "You don't have permission to add data.")) {
        return;
    }

    if (normalizedText(m_partNameField->text()).isEmpty()) {
        setStatus("Part Name is required.", false);
        return;
    }

    QString normalizedDimensions;
    if (!tryNormalizeDimensionsCm(m_dimensionsField->text(), &normalizedDimensions)) {
        setStatus("Product dimensions must be in format: L cm x W cm x H cm.", false);
        return;
    }
    m_dimensionsField->setText(normalizedDimensions);

    bool weightOk = false;
    const double weightValue = m_weightField->text().trimmed().toDouble(&weightOk);
    if (!weightOk || weightValue <= 0.0) {
        setStatus("Product weight (kg) is required.", false);
        return;
    }

    const QString categoryDigit = m_categoryCombo->currentData().toString();
    const QString subCategoryDigit = m_subCategoryCombo->currentData().toString();
    if (categoryDigit.isEmpty() || subCategoryDigit.isEmpty()) {
        setStatus("Select category and sub category first.", false);
        return;
    }

    const QString sku = currentSkuValue();
    if (sku.isEmpty()) {
        setStatus("Unable to generate SKU.", false);
        return;
    }
    m_skuField->setText(sku);

    QSqlQuery dup(m_db);
    dup.prepare("SELECT COUNT(*) FROM sku_catalog_active WHERE sku = ?");
    dup.addBindValue(sku);
    if (!dup.exec() || !dup.next()) {
        setStatus("Failed to check SKU duplicates.", false);
        return;
    }
    if (dup.value(0).toInt() > 0) {
        setStatus("SKU already exists. Entry not saved.", false);
        return;
    }

    int newSiNo = 1;
    QSqlQuery maxSi(m_db);
    if (maxSi.exec("SELECT MAX(si_no) FROM sku_catalog") && maxSi.next()) {
        if (!maxSi.isNull(0)) {
            newSiNo = maxSi.value(0).toInt() + 1;
        }
    }

    QSqlQuery insert(m_db);
    insert.prepare(
        "INSERT INTO sku_catalog ("
        "si_no, part_number, sku, part_name, category_code, sub_category, item_serial, unique_variation, "
        "description, storage, rack_number, bin_number, dimensions, weight_value, weight_unit, "
        "product_family, image_blob, image_path, comments, created_at"
        ") VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");

    QByteArray imageData = m_imageBytes;
    QString imagePathForSave;
    if (imageData.isEmpty() && !m_legacyImagePath.isEmpty()) {
        imageData = loadImageBytesFromPath(m_legacyImagePath);
        if (imageData.isEmpty()) {
            imagePathForSave = m_legacyImagePath;
        }
    }

    insert.addBindValue(newSiNo);
    insert.addBindValue(normalizedText(m_partNumberField->text()));
    insert.addBindValue(sku);
    insert.addBindValue(normalizedText(m_partNameField->text()));
    insert.addBindValue(comboRuleName(m_categoryCombo));
    insert.addBindValue(comboRuleName(m_subCategoryCombo));
    insert.addBindValue(m_itemSerialSpin->value());
    insert.addBindValue(m_variationSpin->value());
    insert.addBindValue(m_descriptionEdit->toPlainText().trimmed());
    insert.addBindValue(normalizedText(m_storageField->text()));
    insert.addBindValue(normalizedText(m_rackNumberField ? m_rackNumberField->text() : QString()));
    insert.addBindValue(normalizedText(m_binNumberField ? m_binNumberField->text() : QString()));
    insert.addBindValue(normalizedDimensions);
    insert.addBindValue(weightValue);
    insert.addBindValue("kg");
    insert.addBindValue(normalizedText(m_productFamilyField->text()));
    insert.addBindValue(imageData);
    insert.addBindValue(imagePathForSave);
    insert.addBindValue(m_commentsEdit->toPlainText().trimmed());
    const QString createdAt = QDateTime::currentDateTime().toString(Qt::ISODate);
    insert.addBindValue(createdAt);

    if (!insert.exec()) {
        setStatus("Failed to save entry.", false);
        logAction("SKU_CREATE",
                  sku,
                  QString(),
                  QString(),
                  "Failed to save entry.",
                  "SKU Master",
                  "CREATE",
                  false,
                  insert.lastError().text(),
                  sku);
        return;
    }

    QJsonObject newValue;
    newValue.insert("sku", sku);
    newValue.insert("part_number", normalizedText(m_partNumberField->text()));
    newValue.insert("part_name", normalizedText(m_partNameField->text()));
    newValue.insert("category", comboRuleName(m_categoryCombo));
    newValue.insert("sub_category", comboRuleName(m_subCategoryCombo));
    newValue.insert("item_serial", m_itemSerialSpin->value());
    newValue.insert("variation", m_variationSpin->value());
    newValue.insert("description", m_descriptionEdit->toPlainText().trimmed());
    newValue.insert("storage", normalizedText(m_storageField->text()));
    newValue.insert("rack_number", normalizedText(m_rackNumberField ? m_rackNumberField->text() : QString()));
    newValue.insert("bin_number", normalizedText(m_binNumberField ? m_binNumberField->text() : QString()));
    newValue.insert("dimensions", normalizedDimensions);
    newValue.insert("weight_value", weightValue);
    newValue.insert("weight_unit", "kg");
    newValue.insert("product_family", normalizedText(m_productFamilyField->text()));
    const bool imagePresent = !imageData.isEmpty() || !imagePathForSave.isEmpty();
    newValue.insert("image_present", imagePresent);
    newValue.insert("image_storage", imageData.isEmpty() ? (imagePathForSave.isEmpty() ? "none" : "path") : "blob");
    newValue.insert("image_bytes", static_cast<int>(imageData.size()));
    newValue.insert("comments", m_commentsEdit->toPlainText().trimmed());
    newValue.insert("created_at", createdAt);
    logAction("SKU_CREATE",
              sku,
              QString(),
              QString::fromUtf8(QJsonDocument(newValue).toJson(QJsonDocument::Compact)),
              QString(),
              "SKU Master",
              "CREATE",
              true,
              QString(),
              sku);

    m_skuField->setText(sku);
    m_selectedId = 0;
    setStatus("Entry saved successfully with auto SKU and SI.NO.", true);

    m_searchSku->setText(sku);
    searchRecords();
    loadSkuList();
    loadHistorySkuList();
    updateDashboardMetrics();
    int barcodeIndex = m_barcodeSkuCombo->findData(sku);
    if (barcodeIndex < 0) {
        barcodeIndex = m_barcodeSkuCombo->findText(sku, Qt::MatchStartsWith);
    }
    if (barcodeIndex >= 0) {
        m_barcodeSkuCombo->setCurrentIndex(barcodeIndex);
    }
}

void MainWindow::generateBarcodes() {
    if (!requireAccess(m_access.canAdd, "You don't have permission to generate QR codes.")) {
        return;
    }

    const QString sku = extractSkuFromDisplay(m_barcodeSkuCombo->currentText());
    if (sku.isEmpty()) {
        setStatus("Select an SKU to generate QR codes.", false);
        return;
    }
    if (!skuExists(sku)) {
        setStatus("Selected SKU does not exist in catalog.", false);
        return;
    }

    const QString qtyText = m_barcodeQuantityField ? m_barcodeQuantityField->text().trimmed() : QString();
    bool qtyOk = false;
    const int quantity = qtyText.toInt(&qtyOk);
    if (!qtyOk || quantity <= 0) {
        setStatus("Quantity must be a positive integer.", false);
        return;
    }
    const int year = selectedBarcodeYear();
    const int quarter = selectedBarcodeQuarter();
    const QString barcodePrefix = selectedBarcodePrefix();
    const QList<int> serials = fetchNextBarcodeSerials(sku, year, quarter, quantity);
    if (serials.size() != quantity) {
        setStatus("Unable to allocate serial numbers.", false);
        return;
    }

    QList<QStringList> rows;
    rows.reserve(quantity);
    m_lastGeneratedBarcodes.clear();

    // Reserve and persist the full batch in one transaction so serial allocation stays
    // contiguous even if one insert in the middle fails.
    m_db.transaction();
    QSqlQuery log(m_db);
    log.prepare("INSERT INTO barcode_log (sku, year, quarter, serial, barcode, created_at) VALUES (?,?,?,?,?,?)");

    for (int serial : serials) {
        const QString barcodeValue = buildBarcodeValue(sku, serial, year, quarter, barcodePrefix);
        const QString createdAt = QDateTime::currentDateTime().toString(Qt::ISODate);

        log.addBindValue(sku);
        log.addBindValue(year);
        log.addBindValue(quarter);
        log.addBindValue(serial);
        log.addBindValue(barcodeValue);
        log.addBindValue(createdAt);

        if (!log.exec()) {
            m_db.rollback();
            setStatus("Failed to save QR code batch.", false);
            logAction("BARCODE_GENERATE",
                      sku,
                      QString(),
                      QString(),
                      "Failed to save QR code batch.",
                      "Barcode",
                      "CREATE",
                      false,
                      log.lastError().text(),
                      sku);
            return;
        }

        rows.append({barcodeValue, sku, QString::number(serial), QString("Q%1").arg(quarter), QString::number(year), createdAt});
        m_lastGeneratedBarcodes.append(barcodeValue);
    }

    m_db.commit();

    refreshBarcodeSerialTracker(sku, year, quarter);

    populateBarcodeModel(rows);

    const QString lastBarcode = rows.isEmpty() ? QString() : rows.last().at(0);
    if (!lastBarcode.isEmpty()) {
        m_barcodeImage = renderQrCode(lastBarcode);
        if (!m_barcodeImage.isNull()) {
            m_barcodePreview->setText(QString());
            m_barcodePreview->setPixmap(QPixmap::fromImage(m_barcodeImage)
                                            .scaled(m_barcodePreview->size(), Qt::KeepAspectRatio, Qt::FastTransformation));
        }
        m_barcodeValueField->setText(lastBarcode);
    }

    updateNextBarcodeSerial();
    setStatus(QString("Generated %1 QR code(s) for %2.").arg(quantity).arg(sku), true);
    updateDashboardMetrics();
    updateQuantityWordsLabels(sku);
    loadHistoryForSelectedSku();

    QJsonObject logDetails;
    logDetails.insert("quantity", quantity);
    logDetails.insert("year", year);
    logDetails.insert("quarter", quarter);
    logDetails.insert("prefix", barcodePrefix);
    if (!serials.isEmpty()) {
        logDetails.insert("serial_start", serials.first());
        logDetails.insert("serial_end", serials.last());
    }
    QJsonArray serialArray;
    for (int serial : serials) {
        serialArray.append(serial);
    }
    logDetails.insert("serials", serialArray);
    logAction("BARCODE_GENERATE",
              sku,
              QString(),
              QString::fromUtf8(QJsonDocument(logDetails).toJson(QJsonDocument::Compact)),
              QString(),
              "Barcode",
              "CREATE",
              true,
              QString(),
              sku);
}


void MainWindow::deleteSelectedBarcodes() {
    if (!requireAccess(m_access.canSerialDelete, "You don't have permission to delete serial numbers.")) {
        return;
    }

    const QModelIndexList rows = m_barcodeTableView->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        setStatus("Select serial rows to delete.", false);
        return;
    }

    const auto reply = QMessageBox::question(this, "Delete Serials",
                                             "Delete selected serial numbers?",
                                             QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) {
        return;
    }

    QString comment;
    if (!promptAdminAuthorization("Delete Serial Numbers", &comment, true)) {
        return;
    }

    QSet<QString> barcodes;
    QSet<QString> affectedKeys;
    QSet<QString> affectedSkus;
    for (const QModelIndex &rowIndex : rows) {
        const QString barcode = m_barcodeModel->item(rowIndex.row(), 0)->text();
        barcodes.insert(barcode);
    }

    m_db.transaction();
    QSqlQuery selectMeta(m_db);
    selectMeta.prepare("SELECT sku, year, quarter, serial, created_at FROM barcode_log_active WHERE barcode = ?");
    QSqlQuery del(m_db);
    del.prepare(
        "UPDATE barcode_log SET "
        "is_deleted = 1, deleted_at = ?, deleted_by = ?, delete_reason = ?, deleted_snapshot = ? "
        "WHERE barcode = ? AND COALESCE(is_deleted, 0) = 0");

    for (const QString &barcode : barcodes) {
        selectMeta.addBindValue(barcode);
        QString sku;
        int year = 0;
        int quarter = 0;
        int serial = 0;
        QString createdAt;
        if (selectMeta.exec() && selectMeta.next()) {
            sku = selectMeta.value(0).toString();
            year = selectMeta.value(1).toInt();
            quarter = selectMeta.value(2).toInt();
            serial = selectMeta.value(3).toInt();
            createdAt = selectMeta.value(4).toString();
            affectedKeys.insert(QString("%1|%2|%3").arg(sku).arg(year).arg(quarter));
            affectedSkus.insert(sku);
        }
        selectMeta.finish();

        QJsonObject oldValue;
        oldValue.insert("sku", sku);
        oldValue.insert("barcode", barcode);
        oldValue.insert("serial", serial);
        oldValue.insert("quarter", quarter);
        oldValue.insert("year", year);
        oldValue.insert("created_at", createdAt);
        const QString oldValueJson = QString::fromUtf8(QJsonDocument(oldValue).toJson(QJsonDocument::Compact));

        del.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
        del.addBindValue(m_currentUsername);
        del.addBindValue(comment);
        del.addBindValue(oldValueJson);
        del.addBindValue(barcode);
        if (!del.exec()) {
            m_db.rollback();
            setStatus("Failed to delete serials.", false);
            logAction("SERIAL_DELETE",
                      barcode,
                      QString(),
                      QString(),
                      "Failed to delete serials.",
                      "Barcode",
                      "DELETE",
                      false,
                      del.lastError().text(),
                      barcode);
            return;
        }
        del.finish();

        if (!sku.isEmpty()) {
            logAction("SERIAL_DELETE",
                      sku,
                      oldValueJson,
                      QString(),
                      comment,
                      "Barcode",
                      "DELETE",
                      true,
                      QString(),
                      barcode);
        }
    }

    for (const QString &key : affectedKeys) {
        const QStringList parts = key.split('|');
        if (parts.size() != 3) {
            continue;
        }
        const QString sku = parts.at(0);
        const int year = parts.at(1).toInt();
        const int quarter = parts.at(2).toInt();
        refreshBarcodeSerialTracker(sku, year, quarter);
    }

    m_db.commit();

    // Remove rows from model
    QList<int> rowNumbers;
    for (const QModelIndex &rowIndex : rows) {
        rowNumbers.append(rowIndex.row());
    }
    std::sort(rowNumbers.begin(), rowNumbers.end(), std::greater<int>());
    for (int row : rowNumbers) {
        m_barcodeModel->removeRow(row);
    }
    for (const QString &barcode : barcodes) {
        m_lastGeneratedBarcodes.removeAll(barcode);
    }

    setStatus("Selected serials deleted.", true);
    updateDashboardMetrics();
    updateNextBarcodeSerial();
    const QString currentSku = extractSkuFromDisplay(m_barcodeSkuCombo->currentText());
    if (!currentSku.isEmpty() && affectedSkus.contains(currentSku)) {
        updateQuantityWordsLabels(currentSku);
    }
    loadHistoryForSelectedSku();
}

void MainWindow::clearBarcodeFields() {
    if (m_barcodeSkuCombo) {
        QSignalBlocker blocker(m_barcodeSkuCombo);
        m_barcodeSkuCombo->setCurrentIndex(-1);
        if (m_barcodeSkuCombo->lineEdit()) {
            m_barcodeSkuCombo->lineEdit()->clear();
        }
    }
    if (m_barcodeQuantityField) {
        m_barcodeQuantityField->setText("1");
    }
    if (m_barcodeNextSerialField) {
        m_barcodeNextSerialField->setText("1");
    }
    if (m_barcodeValueField) {
        m_barcodeValueField->clear();
    }
    if (m_barcodePreview) {
        m_barcodePreview->setText("No QR Code");
        m_barcodePreview->setPixmap(QPixmap());
    }
    m_barcodeImage = QImage();
    m_lastGeneratedBarcodes.clear();

    if (m_barcodePartNameValueLabel) {
        m_barcodePartNameValueLabel->setText("-");
    }
    if (m_barcodePartNumberValueLabel) {
        m_barcodePartNumberValueLabel->setText("-");
    }
    if (m_barcodeQuantityWordsValueLabel) {
        m_barcodeQuantityWordsValueLabel->setText("-");
    }
    loadBarcodeSkuImage(QByteArray(), QString());
    populateBarcodeModel({});
    updateNextBarcodeSerial();
    setStatus("QR code fields cleared.", true);
}

void MainWindow::onTabChanged(int index) {
    if (!m_mainTabs) {
        return;
    }
    QWidget *current = m_mainTabs->widget(index);
    if (current && current->objectName() == "barcodeTab") {
        clearBarcodeFields();
    }
}

void MainWindow::exportBarcodesToPdf(const QStringList &barcodes, const QString &selectedSkuHint) {
    QStringList toPrint;
    toPrint.reserve(barcodes.size());
    QSet<QString> seen;
    for (const QString &barcodeValue : barcodes) {
        const QString barcode = barcodeValue.trimmed();
        if (barcode.isEmpty() || seen.contains(barcode)) {
            continue;
        }
        seen.insert(barcode);
        toPrint.append(barcode);
    }

    if (toPrint.isEmpty()) {
        setStatus("No QR codes to export.", false);
        return;
    }

    QString safeSku = selectedSkuHint.trimmed();
    safeSku.replace(QRegularExpression("[<>:\"/\\\\|?*\\x00-\\x1F]"), "_");
    safeSku.replace(QRegularExpression("\\s+"), " ");
    if (safeSku.isEmpty()) {
        safeSku = "SKU";
    }

    const QString dateLabel = QDate::currentDate().toString("yyyy-MM-dd");
    QDir exportDir(dataDirPath());
    if (!exportDir.exists()) {
        exportDir.mkpath(".");
    }
    const QString defaultName = exportDir.filePath(QString("%1  %2.pdf").arg(safeSku, dateLabel));
    QString chosen = QFileDialog::getSaveFileName(
        this,
        "Export QR Codes (PDF)",
        defaultName,
        "PDF Files (*.pdf)");
    if (chosen.isEmpty()) {
        return;
    }
    if (!chosen.endsWith(".pdf", Qt::CaseInsensitive)) {
        chosen += ".pdf";
    }

    const qreal labelWidthMm = m_printSettings.labelWidthMm;
    const qreal labelHeightMm = m_printSettings.labelHeightMm;

    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(chosen);

    QPageLayout pageLayout(
        QPageSize(QSizeF(labelWidthMm, labelHeightMm), QPageSize::Millimeter, "Sticker"),
        QPageLayout::Portrait,
        QMarginsF(0, 0, 0, 0),
        QPageLayout::Millimeter);
    pageLayout.setMode(QPageLayout::FullPageMode);
    printer.setPageLayout(pageLayout);

    QPainter painter(&printer);
    if (!painter.isActive()) {
        setStatus("Unable to create QR code PDF.", false);
        return;
    }

    auto mmToPx = [&](qreal mm) {
        return mm * printer.resolution() / 25.4;
    };

    struct PrintItem {
        QString barcode;
        QString sku;
        QString partName;
        QString rackNumber;
        QString binNumber;
        int serial = 0;
    };

    QVector<PrintItem> items;
    items.reserve(toPrint.size());
    for (const QString &barcode : toPrint) {
        QSqlQuery q(m_db);
        q.prepare("SELECT b.sku, b.serial, s.part_name, s.rack_number, s.bin_number "
                  "FROM barcode_log_active b LEFT JOIN sku_catalog_active s ON s.sku = b.sku "
                  "WHERE b.barcode = ?");
        q.addBindValue(barcode);
        if (q.exec() && q.next()) {
            PrintItem item;
            item.barcode = barcode;
            item.sku = q.value(0).toString();
            item.serial = q.value(1).toInt();
            item.partName = q.value(2).toString();
            item.rackNumber = q.value(3).toString();
            item.binNumber = q.value(4).toString();
            items.append(item);
        }
    }

    if (items.isEmpty()) {
        setStatus("No QR code data found for export.", false);
        return;
    }

    // ── geometry ──────────────────────────────────────────────────────────
    const qreal edgeMarginMm  = qMax<qreal>(0.0, m_printSettings.edgeMarginMm);
    const qreal innerMarginMm = qMax<qreal>(0.0, m_printSettings.innerMarginMm);
    const qreal contentLeftMm   = edgeMarginMm;
    const qreal contentTopMm    = edgeMarginMm;
    const qreal contentRightMm  = qMax(contentLeftMm + 1.0, labelWidthMm - edgeMarginMm);
    const qreal contentBottomMm = qMax(contentTopMm  + 1.0, labelHeightMm - edgeMarginMm);
    const qreal contentWidthMm  = contentRightMm - contentLeftMm;

    const qreal qrWidthMm  = m_printSettings.barcodeWidthMm;
    const qreal qrHeightMm = m_printSettings.barcodeHeightMm;
    const qreal logoSizeMm = qMax<qreal>(0.0, m_printSettings.logoSizeMm);

    // ── font metrics ──────────────────────────────────────────────────────
    const qreal ptToMm          = 25.4 / 72.0;
    const qreal partLineHeightMm= qMax<qreal>(1.0, m_printSettings.partNameFontSizePt * ptToMm * 1.35);
    const qreal detailLineHeightMm = qMax<qreal>(1.0, m_printSettings.detailFontSizePt * ptToMm * 1.35);
    // Part-name header: 2 wrapped lines
    const qreal partNameHeightMm = partLineHeightMm * 2.0;

    int maxSiteLineRows = 1;
    for (int idx = 0; idx < items.size(); ++idx) {
        const int rows = qMax(1, stickerWebsiteForPrefix(barcodePrefixFromValue(items.at(idx).barcode)).count('\n') + 1);
        maxSiteLineRows = qMax(maxSiteLineRows, rows);
    }
    Q_UNUSED(maxSiteLineRows)

    // ── default element positions ─────────────────────────────────────────
    // Part Name: full-width header, no left indent, top of content area.
    const qreal defPartNameXmm  = contentLeftMm;
    const qreal defPartNameYmm  = contentTopMm;

    // QR Code: right side, directly below the part-name header.
    const qreal defQrXmm = qMax(contentLeftMm, contentRightMm - qrWidthMm);
    const qreal defQrYmm = contentTopMm + partNameHeightMm;

    // Info Block: left side, below header; right edge stops before QR code.
    const qreal resolvedQrXmm   = (m_printSettings.qrPosXmm >= 0.0) ? m_printSettings.qrPosXmm : defQrXmm;
    const qreal infoXdefaultMm  = contentLeftMm + innerMarginMm;
    const qreal infoWidthMm     = qMax(1.0, resolvedQrXmm - infoXdefaultMm - edgeMarginMm);
    const qreal defInfoXmm      = infoXdefaultMm;
    const qreal defInfoYmm      = contentTopMm + partNameHeightMm;

    // Logo: bottom-left, below the info block / website area.
    const qreal defLogoXmm = contentLeftMm;
    const qreal defLogoYmm = qMax(contentTopMm, contentBottomMm - logoSizeMm);

    // ── resolve stored vs. default (< 0 means use default) ────────────────
    const qreal partNameXmm  = (m_printSettings.partNamePosXmm  >= 0.0) ? m_printSettings.partNamePosXmm  : defPartNameXmm;
    const qreal partNameYmm  = (m_printSettings.partNamePosYmm  >= 0.0) ? m_printSettings.partNamePosYmm  : defPartNameYmm;
    const qreal qrXmm        = (m_printSettings.qrPosXmm        >= 0.0) ? m_printSettings.qrPosXmm        : defQrXmm;
    const qreal qrYmm        = (m_printSettings.qrPosYmm        >= 0.0) ? m_printSettings.qrPosYmm        : defQrYmm;
    const qreal infoXmm      = (m_printSettings.infoBlockPosXmm >= 0.0) ? m_printSettings.infoBlockPosXmm : defInfoXmm;
    const qreal infoYmm      = (m_printSettings.infoBlockPosYmm >= 0.0) ? m_printSettings.infoBlockPosYmm : defInfoYmm;
    const qreal logoXmm_raw  = (m_printSettings.logoPosXmm      >= 0.0) ? m_printSettings.logoPosXmm      : defLogoXmm;
    const qreal logoYmm_raw  = (m_printSettings.logoPosYmm      >= 0.0) ? m_printSettings.logoPosYmm      : defLogoYmm;
    // clamp logo within content area
    const qreal logoXmm = qMax(contentLeftMm, qMin(logoXmm_raw, qMax(contentLeftMm, contentRightMm  - logoSizeMm)));
    const qreal logoYmm = qMax(contentTopMm,  qMin(logoYmm_raw, qMax(contentTopMm,  contentBottomMm - logoSizeMm)));

    // ── fonts ─────────────────────────────────────────────────────────────
    const QString fontFamily = m_printSettings.fontFamily.trimmed().isEmpty()
                                   ? QStringLiteral("Britannic Bold")
                                   : m_printSettings.fontFamily.trimmed();
    QFont partFont(fontFamily);
    partFont.setBold(true);
    partFont.setPointSizeF(qMax<qreal>(1.0, m_printSettings.partNameFontSizePt));
    QFont detailFont(fontFamily);
    detailFont.setBold(false);
    detailFont.setPointSizeF(qMax<qreal>(1.0, m_printSettings.detailFontSizePt));

    // ── per-page rendering ────────────────────────────────────────────────
    for (int i = 0; i < items.size(); ++i) {
        if (i > 0) printer.newPage();

        const PrintItem &item       = items.at(i);
        const QString barcodePrefix = barcodePrefixFromValue(item.barcode);
        const QString siteLine      = stickerWebsiteForPrefix(barcodePrefix);
        const int     siteLineRows  = qMax(1, siteLine.count('\n') + 1);
        const QString skuLine       = QString("SKU: %1").arg(item.sku.isEmpty()        ? "-" : item.sku);
        const QString rackLine      = QString("Rack: %1").arg(item.rackNumber.isEmpty() ? "-" : item.rackNumber);
        const QString binLine       = QString("Bin: %1").arg(item.binNumber.isEmpty()   ? "-" : item.binNumber);
        const QString codeLine      = item.barcode.isEmpty() ? "-" : item.barcode;
        const QImage  logoImage     = stickerLogoForPrefix(barcodePrefix);

        painter.setPen(Qt::black);

        // Helper: draw a single text line/block inside an (xMm, yMm, wMm, hMm) rect.
        auto drawText = [&](const QString &text,
                            qreal xMm, qreal yMm, qreal wMm, qreal hMm,
                            const QFont &font, bool wrap, bool shrinkToFit) {
            const qreal xPx = mmToPx(xMm);
            const qreal wPx = mmToPx(wMm);
            const qreal hPx = mmToPx(hMm);
            QFont use = font;
            if (shrinkToFit || wrap) {
                qreal pt = qMax(1.0, use.pointSizeF());
                while (pt > 1.0) {
                    QFontMetricsF fm(use);
                    bool shrink = false;
                    if (shrinkToFit && fm.horizontalAdvance(text) > wPx) shrink = true;
                    if (!shrink && wrap) {
                        if (fm.boundingRect(QRectF(0,0,wPx,hPx), Qt::AlignLeft|Qt::TextWordWrap, text).height() > hPx)
                            shrink = true;
                    }
                    if (!shrink) break;
                    pt -= 0.25;
                    use.setPointSizeF(pt);
                }
            }
            painter.setFont(use);
            QFontMetrics fm(use);
            QRectF r(xPx, mmToPx(yMm), wPx, hPx);
            if (wrap) {
                painter.drawText(r, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, text);
            } else {
                painter.drawText(r, Qt::AlignLeft | Qt::AlignVCenter,
                                 fm.elidedText(text, Qt::ElideRight, qMax(1, (int)wPx)));
            }
        };

        // 1. Part Name — full-width header, no left indent.
        drawText(item.partName.isEmpty() ? "-" : item.partName,
                 partNameXmm, partNameYmm, contentWidthMm, partNameHeightMm,
                 partFont, true, false);

        // 2. Info block: SKU, Rack, Bin, Serial, Website — left column below header.
        drawText(skuLine,   infoXmm, infoYmm,                              infoWidthMm, detailLineHeightMm, detailFont, false, false);
        drawText(rackLine,  infoXmm, infoYmm + detailLineHeightMm,         infoWidthMm, detailLineHeightMm, detailFont, false, false);
        drawText(binLine,   infoXmm, infoYmm + detailLineHeightMm * 2,     infoWidthMm, detailLineHeightMm, detailFont, false, false);
        drawText(codeLine,  infoXmm, infoYmm + detailLineHeightMm * 3,     infoWidthMm, detailLineHeightMm, detailFont, false, true);
        drawText(siteLine,  infoXmm, infoYmm + detailLineHeightMm * 4,
                 infoWidthMm, detailLineHeightMm * siteLineRows,
                 detailFont, siteLineRows > 1, siteLineRows == 1);

        // 3. QR Code — right column.
        {
            const QRectF qrRect(mmToPx(qrXmm), mmToPx(qrYmm), mmToPx(qrWidthMm), mmToPx(qrHeightMm));
            const QImage qrImage = renderQrCode(item.barcode);
            if (!qrImage.isNull()) {
                QImage scaled = qrImage.scaled(qrRect.size().toSize(), Qt::KeepAspectRatio, Qt::FastTransformation);
                painter.drawImage(QPointF(qrRect.left() + (qrRect.width()  - scaled.width())  / 2.0,
                                          qrRect.top()  + (qrRect.height() - scaled.height()) / 2.0), scaled);
            }
        }

        // 4. Logo — bottom-left by default.
        if (!logoImage.isNull() && logoSizeMm > 0.0) {
            const QRectF logoRect(mmToPx(logoXmm), mmToPx(logoYmm), mmToPx(logoSizeMm), mmToPx(logoSizeMm));
            QImage logoScaled = logoImage.scaled(logoRect.size().toSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            painter.drawImage(QPointF(logoRect.left() + (logoRect.width()  - logoScaled.width())  / 2.0,
                                       logoRect.top()  + (logoRect.height() - logoScaled.height()) / 2.0), logoScaled);
        }
    }

    painter.end();
    setStatus(QString("QR codes exported to %1").arg(QDir::toNativeSeparators(chosen)), true);
    logAction("PDF_EXPORT",
              "QR_LABELS",
              QString(),
              chosen,
              QString(),
              "Export",
              "EXPORT",
              true,
              QString(),
              chosen);
}

void MainWindow::printBarcodes() {
    if (!requireAccess(m_access.canPrint, "You don't have permission to export PDF labels.")) {
        return;
    }

    // Export the full batch of last-generated barcodes so that generating N codes
    // and clicking "Export QR Code" always produces a single PDF with all N labels.
    QStringList toPrint = m_lastGeneratedBarcodes;
    if (toPrint.isEmpty()) {
        const QString current = m_barcodeValueField ? m_barcodeValueField->text().trimmed() : QString();
        if (!current.isEmpty()) {
            toPrint << current;
        }
    }
    if (toPrint.isEmpty()) {
        setStatus("No QR codes to export. Generate QR codes first.", false);
        return;
    }

    QString selectedSku;
    if (m_barcodeSkuCombo) {
        selectedSku = extractSkuFromDisplay(m_barcodeSkuCombo->currentText());
    }
    exportBarcodesToPdf(toPrint, selectedSku);
}

void MainWindow::onBarcodeSkuInputChanged(const QString &text) {
    loadSkuList(text, true);
}

void MainWindow::onBarcodeSkuActivated(int index) {
    if (!m_barcodeSkuCombo) {
        return;
    }
    QString sku;
    if (index >= 0) {
        sku = m_barcodeSkuCombo->itemData(index).toString();
    }
    if (sku.isEmpty()) {
        sku = extractSkuFromDisplay(m_barcodeSkuCombo->currentText());
    }
    if (sku.isEmpty()) {
        return;
    }

    resetBarcodeFieldsForSkuChange();
    updateNextBarcodeSerial();
}

void MainWindow::onBarcodeTableSelectionChanged(const QModelIndex &current, const QModelIndex &previous) {
    Q_UNUSED(previous);
    if (!current.isValid()) {
        return;
    }

    const QString barcodeValue = m_barcodeModel->item(current.row(), 0)->text();
    if (barcodeValue.isEmpty()) {
        return;
    }

    const QImage image = renderQrCode(barcodeValue);
    if (!image.isNull()) {
        m_barcodeImage = image;
        m_barcodePreview->setText(QString());
        m_barcodePreview->setPixmap(QPixmap::fromImage(image)
                                        .scaled(m_barcodePreview->size(), Qt::KeepAspectRatio, Qt::FastTransformation));
        m_barcodeValueField->setText(barcodeValue);
    }
}


void MainWindow::updateSelected() {
    if (!requireAccess(m_access.canEdit, "You don't have permission to update records.")) {
        return;
    }

    if (m_selectedId <= 0) {
        setStatus("Select a record from search results to update.", false);
        return;
    }

    if (normalizedText(m_partNameField->text()).isEmpty()) {
        setStatus("Part Name is required.", false);
        return;
    }

    QString normalizedDimensions;
    if (!tryNormalizeDimensionsCm(m_dimensionsField->text(), &normalizedDimensions)) {
        setStatus("Product dimensions must be in format: L cm x W cm x H cm.", false);
        return;
    }
    m_dimensionsField->setText(normalizedDimensions);

    bool weightOk = false;
    const double weightValue = m_weightField->text().trimmed().toDouble(&weightOk);
    if (!weightOk || weightValue <= 0.0) {
        setStatus("Product weight (kg) is required.", false);
        return;
    }

    const QString categoryDigit = m_categoryCombo->currentData().toString();
    const QString subCategoryDigit = m_subCategoryCombo->currentData().toString();
    if (categoryDigit.isEmpty() || subCategoryDigit.isEmpty()) {
        setStatus("Select category and sub category first.", false);
        return;
    }

    const QString sku = currentSkuValue();
    if (sku.isEmpty()) {
        setStatus("Unable to generate SKU.", false);
        return;
    }
    m_skuField->setText(sku);

    QSqlQuery dup(m_db);
    dup.prepare("SELECT COUNT(*) FROM sku_catalog_active WHERE sku = ? AND id != ?");
    dup.addBindValue(sku);
    dup.addBindValue(m_selectedId);
    if (!dup.exec() || !dup.next()) {
        setStatus("Failed to check SKU duplicates.", false);
        return;
    }
    if (dup.value(0).toInt() > 0) {
        setStatus("SKU already exists. Update not saved.", false);
        return;
    }

    QString oldValueJson;
    QSqlQuery oldFetch(m_db);
    oldFetch.prepare(
        "SELECT sku, part_number, part_name, category_code, sub_category, item_serial, unique_variation, "
        "description, storage, rack_number, bin_number, dimensions, weight_value, weight_unit, "
        "product_family, image_blob, image_path, comments "
        "FROM sku_catalog WHERE id = ? AND COALESCE(is_deleted, 0) = 0");
    oldFetch.addBindValue(m_selectedId);
    if (oldFetch.exec() && oldFetch.next()) {
        QJsonObject oldValue;
        oldValue.insert("sku", oldFetch.value(0).toString());
        oldValue.insert("part_number", oldFetch.value(1).toString());
        oldValue.insert("part_name", oldFetch.value(2).toString());
        oldValue.insert("category", oldFetch.value(3).toString());
        oldValue.insert("sub_category", oldFetch.value(4).toString());
        oldValue.insert("item_serial", oldFetch.value(5).toInt());
        oldValue.insert("variation", oldFetch.value(6).toInt());
        oldValue.insert("description", oldFetch.value(7).toString());
        oldValue.insert("storage", oldFetch.value(8).toString());
        oldValue.insert("rack_number", oldFetch.value(9).toString());
        oldValue.insert("bin_number", oldFetch.value(10).toString());
        oldValue.insert("dimensions", oldFetch.value(11).toString());
        oldValue.insert("weight_value", oldFetch.value(12).toDouble());
        oldValue.insert("weight_unit", oldFetch.value(13).toString());
        oldValue.insert("product_family", oldFetch.value(14).toString());
        const QByteArray oldImage = oldFetch.value(15).toByteArray();
        const QString oldPath = oldFetch.value(16).toString();
        const bool oldImagePresent = !oldImage.isEmpty() || !oldPath.isEmpty();
        oldValue.insert("image_present", oldImagePresent);
        oldValue.insert("image_storage", oldImage.isEmpty() ? (oldPath.isEmpty() ? "none" : "path") : "blob");
        oldValue.insert("image_bytes", static_cast<int>(oldImage.size()));
        oldValue.insert("comments", oldFetch.value(17).toString());
        oldValueJson = QString::fromUtf8(QJsonDocument(oldValue).toJson(QJsonDocument::Compact));
    }

    QSqlQuery update(m_db);
    update.prepare(
        "UPDATE sku_catalog SET "
        "part_number = ?, sku = ?, part_name = ?, category_code = ?, sub_category = ?, "
        "item_serial = ?, unique_variation = ?, description = ?, storage = ?, rack_number = ?, bin_number = ?, "
        "dimensions = ?, weight_value = ?, weight_unit = ?, product_family = ?, image_blob = ?, image_path = ?, "
        "comments = ? "
        "WHERE id = ? AND COALESCE(is_deleted, 0) = 0");

    QByteArray imageData = m_imageBytes;
    QString imagePathForSave;
    if (imageData.isEmpty() && !m_legacyImagePath.isEmpty()) {
        imageData = loadImageBytesFromPath(m_legacyImagePath);
        if (imageData.isEmpty()) {
            imagePathForSave = m_legacyImagePath;
        }
    }

    update.addBindValue(normalizedText(m_partNumberField->text()));
    update.addBindValue(sku);
    update.addBindValue(normalizedText(m_partNameField->text()));
    update.addBindValue(comboRuleName(m_categoryCombo));
    update.addBindValue(comboRuleName(m_subCategoryCombo));
    update.addBindValue(m_itemSerialSpin->value());
    update.addBindValue(m_variationSpin->value());
    update.addBindValue(m_descriptionEdit->toPlainText().trimmed());
    update.addBindValue(normalizedText(m_storageField->text()));
    update.addBindValue(normalizedText(m_rackNumberField ? m_rackNumberField->text() : QString()));
    update.addBindValue(normalizedText(m_binNumberField ? m_binNumberField->text() : QString()));
    update.addBindValue(normalizedDimensions);
    update.addBindValue(weightValue);
    update.addBindValue("kg");
    update.addBindValue(normalizedText(m_productFamilyField->text()));
    update.addBindValue(imageData);
    update.addBindValue(imagePathForSave);
    update.addBindValue(m_commentsEdit->toPlainText().trimmed());
    update.addBindValue(m_selectedId);

    if (!update.exec()) {
        setStatus("Failed to update entry.", false);
        logAction("SKU_UPDATE",
                  sku,
                  oldValueJson,
                  QString(),
                  "Failed to update entry.",
                  "SKU Master",
                  "EDIT",
                  false,
                  update.lastError().text(),
                  sku);
        return;
    }

    QJsonObject newValue;
    newValue.insert("sku", sku);
    newValue.insert("part_number", normalizedText(m_partNumberField->text()));
    newValue.insert("part_name", normalizedText(m_partNameField->text()));
    newValue.insert("category", comboRuleName(m_categoryCombo));
    newValue.insert("sub_category", comboRuleName(m_subCategoryCombo));
    newValue.insert("item_serial", m_itemSerialSpin->value());
    newValue.insert("variation", m_variationSpin->value());
    newValue.insert("description", m_descriptionEdit->toPlainText().trimmed());
    newValue.insert("storage", normalizedText(m_storageField->text()));
    newValue.insert("rack_number", normalizedText(m_rackNumberField ? m_rackNumberField->text() : QString()));
    newValue.insert("bin_number", normalizedText(m_binNumberField ? m_binNumberField->text() : QString()));
    newValue.insert("dimensions", normalizedDimensions);
    newValue.insert("weight_value", weightValue);
    newValue.insert("weight_unit", "kg");
    newValue.insert("product_family", normalizedText(m_productFamilyField->text()));
    const bool imagePresent = !imageData.isEmpty() || !imagePathForSave.isEmpty();
    newValue.insert("image_present", imagePresent);
    newValue.insert("image_storage", imageData.isEmpty() ? (imagePathForSave.isEmpty() ? "none" : "path") : "blob");
    newValue.insert("image_bytes", static_cast<int>(imageData.size()));
    newValue.insert("comments", m_commentsEdit->toPlainText().trimmed());
    logAction("SKU_UPDATE",
              sku,
              oldValueJson,
              QString::fromUtf8(QJsonDocument(newValue).toJson(QJsonDocument::Compact)),
              QString(),
              "SKU Master",
              "EDIT",
              true,
              QString(),
              sku);

    setStatus("Entry updated successfully.", true);
    m_searchSku->setText(sku);
    searchRecords();
    loadSkuList();
    loadHistorySkuList();
    updateDashboardMetrics();
    int barcodeIndex = m_barcodeSkuCombo->findData(sku);
    if (barcodeIndex < 0) {
        barcodeIndex = m_barcodeSkuCombo->findText(sku, Qt::MatchStartsWith);
    }
    if (barcodeIndex >= 0) {
        m_barcodeSkuCombo->setCurrentIndex(barcodeIndex);
    }
}

void MainWindow::deleteSelectedSku() {
    if (!requireAccess(m_access.canDelete, "You don't have permission to delete records.")) {
        return;
    }

    if (m_selectedId <= 0) {
        setStatus("Select a record to delete.", false);
        return;
    }

    QSqlQuery fetch(m_db);
    fetch.prepare("SELECT sku FROM sku_catalog WHERE id = ? AND COALESCE(is_deleted, 0) = 0");
    fetch.addBindValue(m_selectedId);
    if (!fetch.exec() || !fetch.next()) {
        setStatus("Failed to load SKU for delete.", false);
        return;
    }
    const QString sku = fetch.value(0).toString();

    const auto reply = QMessageBox::question(this, "Delete SKU",
                                             QString("Delete SKU %1 and all related serials?").arg(sku),
                                             QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) {
        return;
    }

    QString comment;
    if (!promptAdminAuthorization("Delete SKU", &comment, true)) {
        return;
    }

    QString oldValueJson;
    QSqlQuery oldFetch(m_db);
    oldFetch.prepare(
        "SELECT sku, part_number, part_name, category_code, sub_category, item_serial, unique_variation, "
        "description, storage, rack_number, bin_number, dimensions, weight_value, weight_unit, "
        "product_family, image_blob, image_path, comments "
        "FROM sku_catalog WHERE id = ? AND COALESCE(is_deleted, 0) = 0");
    oldFetch.addBindValue(m_selectedId);
    if (oldFetch.exec() && oldFetch.next()) {
        QJsonObject oldValue;
        oldValue.insert("sku", oldFetch.value(0).toString());
        oldValue.insert("part_number", oldFetch.value(1).toString());
        oldValue.insert("part_name", oldFetch.value(2).toString());
        oldValue.insert("category", oldFetch.value(3).toString());
        oldValue.insert("sub_category", oldFetch.value(4).toString());
        oldValue.insert("item_serial", oldFetch.value(5).toInt());
        oldValue.insert("variation", oldFetch.value(6).toInt());
        oldValue.insert("description", oldFetch.value(7).toString());
        oldValue.insert("storage", oldFetch.value(8).toString());
        oldValue.insert("rack_number", oldFetch.value(9).toString());
        oldValue.insert("bin_number", oldFetch.value(10).toString());
        oldValue.insert("dimensions", oldFetch.value(11).toString());
        oldValue.insert("weight_value", oldFetch.value(12).toDouble());
        oldValue.insert("weight_unit", oldFetch.value(13).toString());
        oldValue.insert("product_family", oldFetch.value(14).toString());
        const QByteArray oldImage = oldFetch.value(15).toByteArray();
        const QString oldPath = oldFetch.value(16).toString();
        const bool oldImagePresent = !oldImage.isEmpty() || !oldPath.isEmpty();
        oldValue.insert("image_present", oldImagePresent);
        oldValue.insert("image_storage", oldImage.isEmpty() ? (oldPath.isEmpty() ? "none" : "path") : "blob");
        oldValue.insert("image_bytes", static_cast<int>(oldImage.size()));
        oldValue.insert("comments", oldFetch.value(17).toString());
        oldValueJson = QString::fromUtf8(QJsonDocument(oldValue).toJson(QJsonDocument::Compact));
    }

    m_db.transaction();
    QSqlQuery delSku(m_db);
    delSku.prepare(
        "UPDATE sku_catalog SET "
        "is_deleted = 1, deleted_at = ?, deleted_by = ?, delete_reason = ?, deleted_snapshot = ? "
        "WHERE id = ? AND COALESCE(is_deleted, 0) = 0");
    delSku.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    delSku.addBindValue(m_currentUsername);
    delSku.addBindValue(comment);
    delSku.addBindValue(oldValueJson);
    delSku.addBindValue(m_selectedId);
    if (!delSku.exec()) {
        m_db.rollback();
        setStatus("Failed to delete SKU.", false);
        logAction("SKU_DELETE",
                  sku,
                  oldValueJson,
                  QString(),
                  "Failed to delete SKU.",
                  "SKU Master",
                  "DELETE",
                  false,
                  delSku.lastError().text(),
                  sku);
        return;
    }

    QSqlQuery delSerials(m_db);
    const QString serialSnapshot = QString("{\"sku\":\"%1\",\"deleted_by_sku\":true}").arg(sku);
    delSerials.prepare(
        "UPDATE barcode_log SET "
        "is_deleted = 1, deleted_at = ?, deleted_by = ?, delete_reason = ?, deleted_snapshot = ? "
        "WHERE sku = ? AND COALESCE(is_deleted, 0) = 0");
    delSerials.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    delSerials.addBindValue(m_currentUsername);
    delSerials.addBindValue(comment);
    delSerials.addBindValue(serialSnapshot);
    delSerials.addBindValue(sku);
    delSerials.exec();

    QSqlQuery delTrack(m_db);
    delTrack.prepare("DELETE FROM barcode_serials WHERE sku = ?");
    delTrack.addBindValue(sku);
    delTrack.exec();

    m_db.commit();

    logAction("SKU_DELETE",
              sku,
              oldValueJson,
              QString(),
              comment,
              "SKU Master",
              "DELETE",
              true,
              QString(),
              sku);

    setStatus(QString("Deleted SKU %1.").arg(sku), true);
    m_selectedId = 0;
    clearFormFields();
    searchRecords();
    loadSkuList();
    loadHistorySkuList();
    updateDashboardMetrics();
}

void MainWindow::clearFormFields() {
    m_partNameField->clear();
    m_partNumberField->clear();
    m_dimensionsField->clear();
    m_weightField->clear();
    m_descriptionEdit->clear();
    m_storageField->clear();
    if (m_rackNumberField) {
        m_rackNumberField->clear();
    }
    if (m_binNumberField) {
        m_binNumberField->clear();
    }
    m_productFamilyField->clear();
    m_imagePathField->clear();
    m_commentsEdit->clear();
    m_imageBytes.clear();
    m_legacyImagePath.clear();
    loadImagePreview(QByteArray());
    m_selectedId = 0;
    m_barcodeValueField->clear();
    m_barcodePreview->setText("No QR Code");
    m_barcodeImage = QImage();
    m_lastGeneratedBarcodes.clear();
    m_barcodeNextSerialField->setText("1");
    if (m_searchQuantityWordsValueLabel) {
        m_searchQuantityWordsValueLabel->setText("-");
    }

    if (m_categoryCombo->count() > 0) {
        m_categoryCombo->setCurrentIndex(0);
    }
    updateSerialsAndSku(true);
    setStatus("Form cleared.", true);
    updateNextBarcodeSerial();
}

void MainWindow::browseImage() {
    const QString file = QFileDialog::getOpenFileName(
        this,
        "Select Image",
        QString(),
        "Images (*.png *.jpg *.jpeg *.bmp *.gif)");

    if (file.isEmpty()) {
        return;
    }

    QFile imageFile(file);
    if (!imageFile.open(QIODevice::ReadOnly)) {
        setStatus("Failed to read image.", false);
        return;
    }

    const QByteArray data = imageFile.readAll();
    imageFile.close();

    QPixmap testPixmap;
    if (data.isEmpty() || !testPixmap.loadFromData(data)) {
        setStatus("Selected file is not a supported image.", false);
        return;
    }

    m_imageBytes = data;
    m_legacyImagePath.clear();
    loadImagePreview(m_imageBytes);
    updateImagePathFieldDisplay(QFileInfo(file).fileName());
    setStatus("Image stored in database.", true);
}

void MainWindow::loadImagePreview(const QByteArray &data, const QString &legacyPath) {
    loadImageLabelFromData(m_imagePreview, data, legacyPath, AppGlobals::noImageText());
}

void MainWindow::loadImageLabel(QLabel *label, const QString &path, const QString &fallbackText) {
    if (!label) {
        return;
    }

    const QString resolvedPath = resolveImagePath(path);
    if (resolvedPath.isEmpty() || !QFile::exists(resolvedPath)) {
        label->setPixmap(QPixmap());
        label->setText(fallbackText);
        return;
    }

    QPixmap pixmap(resolvedPath);
    if (pixmap.isNull()) {
        label->setPixmap(QPixmap());
        label->setText(fallbackText);
        return;
    }

    label->setText(QString());
    label->setPixmap(pixmap.scaled(label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void MainWindow::loadImageLabelFromData(QLabel *label,
                                        const QByteArray &data,
                                        const QString &legacyPath,
                                        const QString &fallbackText) {
    if (!label) {
        return;
    }

    QPixmap pixmap;
    if (!data.isEmpty()) {
        pixmap.loadFromData(data);
    }

    if (pixmap.isNull()) {
        if (!legacyPath.isEmpty()) {
            loadImageLabel(label, legacyPath, fallbackText);
            return;
        }
        label->setPixmap(QPixmap());
        label->setText(fallbackText);
        return;
    }

    label->setText(QString());
    label->setPixmap(pixmap.scaled(label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

QString MainWindow::resolveImagePath(const QString &path) const {
    if (path.isEmpty()) {
        return QString();
    }
    if (!QDir::isRelativePath(path)) {
        return path;
    }

    const QString dataRootCandidate = QDir(dataRootPath()).filePath(path);
    if (QFile::exists(dataRootCandidate)) {
        return dataRootCandidate;
    }
    const QString dataDirCandidate = QDir(dataDirPath()).filePath(path);
    if (QFile::exists(dataDirCandidate)) {
        return dataDirCandidate;
    }
    return QDir(QCoreApplication::applicationDirPath()).filePath(path);
}

QByteArray MainWindow::loadImageBytesFromPath(const QString &path) const {
    const QString resolvedPath = resolveImagePath(path);
    if (resolvedPath.isEmpty() || !QFile::exists(resolvedPath)) {
        return {};
    }
    QFile file(resolvedPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

void MainWindow::updateImagePathFieldDisplay(const QString &fileNameHint) {
    if (!m_imagePathField) {
        return;
    }
    if (!fileNameHint.isEmpty()) {
        m_imagePathField->setText(fileNameHint);
        return;
    }
    if (!m_imageBytes.isEmpty()) {
        m_imagePathField->setText("Stored in DB");
        return;
    }
    if (!m_legacyImagePath.isEmpty()) {
        m_imagePathField->setText(QFileInfo(m_legacyImagePath).fileName());
        return;
    }
    m_imagePathField->clear();
}

void MainWindow::loadImageForSelectedId(int id) {
    m_imageBytes.clear();
    m_legacyImagePath.clear();
    if (id <= 0) {
        loadImagePreview(QByteArray());
        updateImagePathFieldDisplay();
        return;
    }

    QSqlQuery q(m_db);
    q.prepare("SELECT image_blob, image_path FROM sku_catalog WHERE id = ? AND COALESCE(is_deleted, 0) = 0");
    q.addBindValue(id);
    if (q.exec() && q.next()) {
        m_imageBytes = q.value(0).toByteArray();
        m_legacyImagePath = q.value(1).toString();
    }

    loadImagePreview(m_imageBytes, m_legacyImagePath);
    updateImagePathFieldDisplay();
}

void MainWindow::refreshBarcodeSerialTracker(const QString &sku, int year, int quarter) {
    if (sku.trimmed().isEmpty()) {
        return;
    }

    QSqlQuery maxq(m_db);
    maxq.prepare("SELECT MAX(serial) FROM barcode_log_active WHERE sku = ? AND year = ? AND quarter = ?");
    maxq.addBindValue(sku);
    maxq.addBindValue(year);
    maxq.addBindValue(quarter);

    int maxSerial = 0;
    if (maxq.exec() && maxq.next() && !maxq.isNull(0)) {
        maxSerial = maxq.value(0).toInt();
    }

    if (maxSerial > 0) {
        QSqlQuery up(m_db);
        up.prepare("INSERT OR REPLACE INTO barcode_serials (sku, year, quarter, last_serial) VALUES (?,?,?,?)");
        up.addBindValue(sku);
        up.addBindValue(year);
        up.addBindValue(quarter);
        up.addBindValue(maxSerial);
        up.exec();
    } else {
        QSqlQuery delTrack(m_db);
        delTrack.prepare("DELETE FROM barcode_serials WHERE sku = ? AND year = ? AND quarter = ?");
        delTrack.addBindValue(sku);
        delTrack.addBindValue(year);
        delTrack.addBindValue(quarter);
        delTrack.exec();
    }
}

void MainWindow::onCategoryChanged() {
    const QString categoryDigit = m_categoryCombo->currentData().toString();
    loadSubCategories(categoryDigit);
    updateSerialsAndSku(true);
}

void MainWindow::onSubCategoryChanged() {
    updateSerialsAndSku(true);
}

void MainWindow::onSerialOrVariationChanged() {
    updateSkuPreview();
}

void MainWindow::onSearchSelectionChanged(const QModelIndex &current, const QModelIndex &previous) {
    Q_UNUSED(previous);
    if (!current.isValid()) {
        return;
    }
    const int row = current.row();
    const auto idItem = m_resultsModel->item(row, 0);
    if (idItem) {
        m_selectedId = idItem->text().toInt();
    }
    loadImageForSelectedId(m_selectedId);
    const auto skuItem = m_resultsModel->item(row, 3);
    if (skuItem) {
        const QString sku = skuItem->text().trimmed().toUpper();
        int idx = m_barcodeSkuCombo->findData(sku);
        if (idx < 0) {
            idx = m_barcodeSkuCombo->findText(sku, Qt::MatchStartsWith);
        }
        if (idx >= 0) {
            m_barcodeSkuCombo->setCurrentIndex(idx);
            resetBarcodeFieldsForSkuChange();
        }
        updateQuantityWordsLabels(sku);
    }
    updateNextBarcodeSerial();
}

QString MainWindow::dataRootPath() const {
    return AppSettings::dataRootPath();
}

QString MainWindow::dataDirPath() const {
    return AppSettings::dataDirPath();
}

void MainWindow::migrateLegacyDataIfNeeded() {
    const QString legacyBase = AppSettings::legacyDataDirPath();
    const QString legacyDb = QDir(legacyBase).filePath(AppGlobals::dbFileName());
    const QString legacyFlag = QDir(legacyBase).filePath("catalog_imported.flag");
    const QString legacyImages = QDir(legacyBase).filePath(AppGlobals::imagesFolderName());

    const QString newBase = dataDirPath();
    const QString newDb = QDir(newBase).filePath(AppGlobals::dbFileName());
    const QString newFlag = QDir(newBase).filePath("catalog_imported.flag");
    const QString newImages = QDir(newBase).filePath(AppGlobals::imagesFolderName());

    if (!QFile::exists(newDb) && QFile::exists(legacyDb)) {
        QDir().mkpath(newBase);
        QFile::copy(legacyDb, newDb);
    }

    if (!QFile::exists(newFlag) && QFile::exists(legacyFlag)) {
        QDir().mkpath(newBase);
        QFile::copy(legacyFlag, newFlag);
    }

    QDir legacyImageDir(legacyImages);
    if (legacyImageDir.exists()) {
        QDir newImageDir(newImages);
        if (!newImageDir.exists()) {
            QDir().mkpath(newImages);
        }
        const QStringList files = legacyImageDir.entryList(QDir::Files);
        for (const QString &file : files) {
            const QString source = legacyImageDir.filePath(file);
            const QString target = newImageDir.filePath(file);
            if (!QFile::exists(target)) {
                QFile::copy(source, target);
            }
        }
    }
}

QString MainWindow::dbPath() const {
    return m_customDbPath.trimmed();
}

QString MainWindow::imagesDirPath() const {
    return AppSettings::imagesDirPath();
}
