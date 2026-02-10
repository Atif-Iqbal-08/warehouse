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
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QAction>
#include <QActionGroup>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPushButton>
#include <QMessageBox>
#include <QTabWidget>
#include <QTime>
#include <QPrintDialog>
#include <QPrinter>
#include <QPageLayout>
#include <QPageSize>
#include <QRandomGenerator>
#include <QRegularExpression>
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
#include <QTableView>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QUuid>
#include <QApplication>
#include <QGridLayout>
#include <QGraphicsDropShadowEffect>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QTimer>
#include <QMouseEvent>
#include <QSettings>
#include <exception>

namespace {
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

QString escapeCsvField(const QString &value) {
    QString field = value;
    const bool needsQuotes = field.contains('"') || field.contains(',') || field.contains('\n') || field.contains('\r');
    field.replace('"', "\"\"");
    if (needsQuotes) {
        field = "\"" + field + "\"";
    }
    return field;
}

void appendRunLog(const QString &message) {
    const QString logPath = QDir(AppSettings::dataDirPath()).filePath("app_run.log");
    QFile file(logPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << QDateTime::currentDateTime().toString(Qt::ISODate) << " | " << message << '\n';
}
} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setupUi();
    appendRunLog("MainWindow setupUi complete");

    if (!selectDatabaseOnStartup()) {
        setStatus("Database selection canceled.", false);
        appendRunLog("Database selection canceled");
        QTimer::singleShot(0, this, &QWidget::close);
        return;
    }

    if (!m_db.isOpen()) {
        if (!initDb()) {
            setStatus("Database initialization failed.", false);
            appendRunLog("initDb failed");
            return;
        }
    }
    appendRunLog("initDb ok");

    m_currentUsername = "local";
    m_currentRoleKey = AppGlobals::roleKeyFull();
    m_currentBaseRole = UserRole::FullAccess;
    m_access = accessPolicyForBaseRole(UserRole::FullAccess);

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
        policy.canManageUsers = false;
        break;
    case UserRole::AddOnly:
        policy.canAdd = true;
        policy.canEdit = true;
        policy.canDelete = false;
        policy.canManageUsers = false;
        break;
    case UserRole::FullAccess:
        policy.canAdd = true;
        policy.canEdit = true;
        policy.canDelete = true;
        policy.canManageUsers = true;
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
    m_access = accessPolicyForBaseRole(baseRole);

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
        m_deleteBarcodeButton->setEnabled(m_access.canDelete);
    }
    if (m_historyEditButton) {
        m_historyEditButton->setEnabled(m_access.canEdit);
    }
    if (m_historyDeleteButton) {
        m_historyDeleteButton->setEnabled(m_access.canDelete);
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
}

bool MainWindow::requireAccess(bool allowed, const QString &message) {
    Q_UNUSED(allowed);
    Q_UNUSED(message);
    return true;
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
    if (value == AppGlobals::roleKeyView() || value == "view_only" || value == "viewonly") {
        return UserRole::ViewOnly;
    }
    if (value == AppGlobals::roleKeyAdd() || value == "add_only" || value == "addonly") {
        return UserRole::AddOnly;
    }
    if (value == AppGlobals::roleKeyFull() || value == "full_access" || value == "fullaccess" || value == "admin") {
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
        "INSERT OR IGNORE INTO app_roles (role_key, display_name, base_role, created_at) "
        "VALUES (?, ?, ?, ?)");

    struct RoleSeed {
        QString key;
        QString name;
        UserRole baseRole;
    };

    const RoleSeed seeds[] = {
        { AppGlobals::roleKeyView(), AppGlobals::roleNameView(), UserRole::ViewOnly },
        { AppGlobals::roleKeyAdd(), AppGlobals::roleNameAdd(), UserRole::AddOnly },
        { AppGlobals::roleKeyFull(), AppGlobals::roleNameFull(), UserRole::FullAccess }
    };

    for (const auto &seed : seeds) {
        insert.addBindValue(seed.key);
        insert.addBindValue(seed.name);
        insert.addBindValue(baseRoleToStorage(seed.baseRole));
        insert.addBindValue(now);
        if (!insert.exec()) {
            return false;
        }
        insert.finish();
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

    QSqlQuery q(m_db);
    if (!q.exec("SELECT COUNT(*) FROM app_users")) {
        return false;
    }
    if (q.next() && q.value(0).toInt() > 0) {
        return true;
    }

    QMessageBox::information(this,
                             "User Setup",
                             "No user accounts found. Create an admin account to continue.");
    return promptCreateUser(AppGlobals::roleKeyFull(), false);
}

bool MainWindow::promptLogin() {
    QDialog dialog(this);
    dialog.setWindowTitle("User Login");
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
            logAction("LOGIN_FAILED", username, QString(), QString(), "Invalid credentials");
            appendRunLog(QString("Login failed for %1").arg(username));
            return;
        }

        m_currentUsername = username;
        applyAccessControl(roleKey);
        updateWindowTitleWithUser();
        setStatus(QString("Logged in as %1 (%2).").arg(m_currentUsername, roleDisplayName(m_currentRoleKey)), true);
        logAction("LOGIN_SUCCESS", m_currentUsername, QString(), QString(), QString());
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
    q.prepare("SELECT username, full_name, user_id, email, role, created_at FROM app_users ORDER BY username");
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

        table->setItem(row, 0, new QTableWidgetItem(username));
        table->setItem(row, 1, new QTableWidgetItem(fullName));
        table->setItem(row, 2, new QTableWidgetItem(userId));
        table->setItem(row, 3, new QTableWidgetItem(email));
        table->setItem(row, 4, new QTableWidgetItem(roleDisplayName(roleValue)));
        table->setItem(row, 5, new QTableWidgetItem(createdAt));
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
    QVBoxLayout *layout = new QVBoxLayout(&dialog);

    // Roles section
    QGroupBox *rolesBox = new QGroupBox("Roles", &dialog);
    QVBoxLayout *rolesLayout = new QVBoxLayout(rolesBox);
    QLabel *rolesInfo = new QLabel("Add roles and choose access level.", rolesBox);
    rolesLayout->addWidget(rolesInfo);

    QTableWidget *rolesTable = new QTableWidget(rolesBox);
    rolesTable->setColumnCount(3);
    rolesTable->setHorizontalHeaderLabels({ "Role Name", "Access Level", "Created" });
    rolesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    rolesTable->setSelectionMode(QAbstractItemView::SingleSelection);
    rolesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
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
    roleForm->addRow("Role Name", roleNameField);
    roleForm->addRow("Access Level", roleAccessCombo);
    rolesLayout->addLayout(roleForm);

    QLabel *rolesStatus = new QLabel(rolesBox);
    rolesStatus->setStyleSheet("color: #c62828;");
    rolesLayout->addWidget(rolesStatus);

    QPushButton *addRoleButton = new QPushButton("Add Role", rolesBox);
    rolesLayout->addWidget(addRoleButton);

    layout->addWidget(rolesBox);

    // Users section
    QGroupBox *usersBox = new QGroupBox("Users", &dialog);
    QVBoxLayout *usersLayout = new QVBoxLayout(usersBox);
    QLabel *usersInfo = new QLabel("Add user accounts and assign roles.", usersBox);
    usersLayout->addWidget(usersInfo);

    QTableWidget *usersTable = new QTableWidget(usersBox);
    usersTable->setColumnCount(6);
    usersTable->setHorizontalHeaderLabels({ "Username", "Name", "User ID", "Email", "Role", "Created" });
    usersTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    usersTable->setSelectionMode(QAbstractItemView::SingleSelection);
    usersTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
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

    userForm->addRow("Name", nameField);
    userForm->addRow("User ID", userIdField);
    userForm->addRow("Email", emailField);
    userForm->addRow("Username", usernameField);
    userForm->addRow("Password", passwordField);
    userForm->addRow("Confirm Password", confirmField);
    userForm->addRow("Role", roleCombo);
    usersLayout->addLayout(userForm);

    QLabel *usersStatus = new QLabel(usersBox);
    usersStatus->setStyleSheet("color: #c62828;");
    usersLayout->addWidget(usersStatus);

    QPushButton *addUserButton = new QPushButton("Add User", usersBox);
    usersLayout->addWidget(addUserButton);

    layout->addWidget(usersBox);

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

        QSqlQuery insert(m_db);
        insert.prepare("INSERT INTO app_roles (role_key, display_name, base_role, created_at) VALUES (?, ?, ?, ?)");
        insert.addBindValue(roleKey);
        insert.addBindValue(roleName);
        insert.addBindValue(baseRole);
        insert.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));

        if (!insert.exec()) {
            rolesStatus->setStyleSheet("color: #c62828;");
            rolesStatus->setText("Failed to add role. Role name may already exist.");
            return;
        }

        rolesStatus->setStyleSheet("color: #1b5e20;");
        rolesStatus->setText("Role added.");
        roleNameField->clear();
        roleAccessCombo->setCurrentIndex(0);
        loadRolesTable();
        loadRolesIntoCombo(roleCombo);
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

        usersStatus->setStyleSheet("color: #1b5e20;");
        usersStatus->setText("User account created.");
        nameField->clear();
        userIdField->clear();
        emailField->clear();
        usernameField->clear();
        passwordField->clear();
        confirmField->clear();
        roleCombo->setCurrentIndex(0);
        loadUsersIntoTable(usersTable);
    });

    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    dialog.exec();
}

void MainWindow::showUserAccountsDialog() {
    showSettingsDialog();
}

void MainWindow::showSkuDetailsDialog(const QString &sku) {
    const QString trimmedSku = sku.trimmed().toUpper();
    if (trimmedSku.isEmpty()) {
        return;
    }

    QSqlQuery q(m_db);
    q.prepare(
        "SELECT sku, part_name, part_number, category_code, sub_category, "
        "item_serial, unique_variation, description, storage, rack_number, bin_number, "
        "dimensions, weight_value, weight_unit, product_family, image_blob, image_path, comments, created_at "
        "FROM sku_catalog WHERE sku = ?");
    q.addBindValue(trimmedSku);
    if (!q.exec() || !q.next()) {
        setStatus("Unable to load SKU details.", false);
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QString("SKU Details - %1").arg(trimmedSku));
    dialog.setMinimumSize(640, 420);

    auto *rootLayout = new QHBoxLayout(&dialog);
    rootLayout->setContentsMargins(16, 16, 16, 16);
    rootLayout->setSpacing(16);

    auto *imageLabel = new QLabel(&dialog);
    imageLabel->setFixedSize(200, 200);
    imageLabel->setAlignment(Qt::AlignCenter);
    imageLabel->setFrameShape(QFrame::StyledPanel);
    loadImageLabelFromData(imageLabel, q.value(15).toByteArray(), q.value(16).toString(), AppGlobals::noImageText());

    auto makeValueLabel = [&dialog](const QString &text) {
        auto *label = new QLabel(text, &dialog);
        label->setWordWrap(true);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        return label;
    };

    auto *detailsLayout = new QVBoxLayout();
    detailsLayout->setSpacing(10);

    auto *formLayout = new QFormLayout();
    formLayout->setLabelAlignment(Qt::AlignLeft);
    formLayout->setFormAlignment(Qt::AlignTop);
    formLayout->setHorizontalSpacing(16);
    formLayout->setVerticalSpacing(6);

    formLayout->addRow("SKU", makeValueLabel(q.value(0).toString()));
    formLayout->addRow("Part Name", makeValueLabel(q.value(1).toString()));
    formLayout->addRow("Part Number", makeValueLabel(q.value(2).toString()));
    formLayout->addRow("Category", makeValueLabel(q.value(3).toString()));
    formLayout->addRow("Sub Category", makeValueLabel(q.value(4).toString()));
    formLayout->addRow("Item Serial", makeValueLabel(q.value(5).toString()));
    formLayout->addRow("Variation", makeValueLabel(q.value(6).toString()));
    formLayout->addRow("Dimensions", makeValueLabel(q.value(11).toString()));
    const QString weightDisplay = QString("%1 %2").arg(q.value(12).toString(), q.value(13).toString());
    formLayout->addRow("Weight", makeValueLabel(weightDisplay.trimmed()));
    formLayout->addRow("Storage", makeValueLabel(q.value(8).toString()));
    formLayout->addRow("Rack Number", makeValueLabel(q.value(9).toString()));
    formLayout->addRow("Bin Number", makeValueLabel(q.value(10).toString()));
    formLayout->addRow("Product Family", makeValueLabel(q.value(14).toString()));
    formLayout->addRow("Description", makeValueLabel(q.value(7).toString()));
    formLayout->addRow("Comments", makeValueLabel(q.value(17).toString()));
    formLayout->addRow("Created At", makeValueLabel(q.value(18).toString()));

    detailsLayout->addLayout(formLayout);
    detailsLayout->addStretch(1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    detailsLayout->addWidget(buttons);

    rootLayout->addWidget(imageLabel);
    rootLayout->addLayout(detailsLayout, 1);

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

    UserRole baseRole = UserRole::ViewOnly;
    if (!fetchRoleInfo(roleKey, &baseRole, nullptr)) {
        baseRole = baseRoleFromStorage(roleKey);
    }

    return baseRole == UserRole::FullAccess;
}

bool MainWindow::promptAdminAuthorization(const QString &action, QString *commentOut, bool requireComment) {
    Q_UNUSED(action);
    Q_UNUSED(requireComment);
    if (commentOut) {
        commentOut->clear();
    }
    return true;
}

bool MainWindow::logAction(const QString &action,
                           const QString &entity,
                           const QString &oldValue,
                           const QString &newValue,
                           const QString &comment) {
    QSqlQuery q(m_db);
    q.prepare(
        "INSERT INTO app_audit_log (timestamp, user, action, entity, old_value, new_value, comment) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)");
    q.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    q.addBindValue(m_currentUsername.isEmpty() ? QString("unknown") : m_currentUsername);
    q.addBindValue(action);
    q.addBindValue(entity);
    q.addBindValue(oldValue);
    q.addBindValue(newValue);
    q.addBindValue(comment);
    return q.exec();
}

int MainWindow::totalQuantityForSku(const QString &sku) const {
    if (sku.trimmed().isEmpty()) {
        return 0;
    }
    QSqlQuery q(m_db);
    q.prepare("SELECT COUNT(*) FROM barcode_log WHERE sku = ?");
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

    const QString darkTheme =
        "QMainWindow { background-color: #0b0f14; color: #f7f9fc; }"
        "QWidget { color: #f7f9fc; }"
        "QGroupBox { background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #131c26, stop:1 #0f151e); "
        "  border: 1px solid #1f2a3a; border-radius: 12px; margin-top: 18px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; color: #ff8f1f; font-weight: 700; }"
        "QLineEdit, QPlainTextEdit, QSpinBox, QComboBox {"
        "  background-color: #0e1621; border: 1px solid #2a3a4f; border-radius: 8px; padding: 7px; color: #f7f9fc;"
        "  selection-background-color: #ff8f1f; selection-color: #0b0f14; }"
        "QLineEdit:focus, QPlainTextEdit:focus, QSpinBox:focus, QComboBox:focus { border: 1px solid #22c8ff; }"
        "QComboBox QAbstractItemView { background-color: #0e1621; selection-background-color: #22c8ff; }"
        "QTableView { background-color: #0e1621; gridline-color: #1f2a3a; selection-background-color: #233349; selection-color: #ffffff; }"
        "QTableView::item { padding: 5px; }"
        "QHeaderView::section { background-color: #17212e; color: #f7f9fc; border: 1px solid #243246; padding: 7px; font-weight: 600; }"
        "QTableCornerButton::section { background-color: #17212e; border: 1px solid #243246; }"
        "QPushButton { background-color: #1b2736; border: 1px solid #31445c; border-radius: 8px; padding: 8px 14px; font-weight: 600; }"
        "QPushButton:hover { background-color: #243449; border-color: #3e5874; }"
        "QPushButton:pressed { background-color: #18212d; }"
        "QPushButton:disabled { background-color: #1a2430; color: #7f8a97; border-color: #2a3a4f; }"
        "QPushButton#dashboardSearchButton, QPushButton#searchButton, QPushButton#generateBarcodeButton, QPushButton#saveButton {"
        "  background-color: #ff8f1f; color: #0b0f14; border: 1px solid #ffb765; }"
        "QPushButton#dashboardSearchButton:hover, QPushButton#searchButton:hover, QPushButton#generateBarcodeButton:hover, QPushButton#saveButton:hover {"
        "  background-color: #ffa648; border-color: #ffd19a; }"
        "QTabWidget::pane { border: 1px solid #1f2a3a; }"
        "QTabBar::tab { background: #111720; color: #cfd7e3; padding: 8px 14px; border-top-left-radius: 8px; border-top-right-radius: 8px; margin-right: 6px; border: 1px solid #1f2a3a; }"
        "QTabBar::tab:selected { background: #ff8f1f; color: #0b0f14; }"
        "QTabBar::tab:hover { background: #1b2634; color: #ffffff; }"
        "QScrollArea { background-color: transparent; border: none; }"
        "QScrollArea#latestSkuScrollArea { background-color: #0c1118; border: 1px solid #1f2a3a; border-radius: 12px; }"
        "QWidget#latestSkuContainer { background-color: #0c1118; }"
        "QScrollBar:vertical { background: #10161f; width: 12px; margin: 0px; }"
        "QScrollBar::handle:vertical { background: #2a3a4f; border-radius: 6px; min-height: 22px; }"
        "QScrollBar::handle:vertical:hover { background: #3b5270; }"
        "QScrollBar:horizontal { background: #10161f; height: 12px; margin: 0px; }"
        "QScrollBar::handle:horizontal { background: #2a3a4f; border-radius: 6px; min-width: 22px; }"
        "QScrollBar::handle:horizontal:hover { background: #3b5270; }"
        "QLabel#companyLogoLabel { background-color: #131a23; border: 1px solid #2a3647; border-radius: 10px; padding: 6px; }"
        "QLabel#softwareNameLabel { color: #ff8f1f; font-size: 18px; font-weight: 700; }"
        "QLabel#companyNameLabel { color: #f7f9fc; font-weight: 600; }"
        "QLabel#authorLabel { color: #22c8ff; font-style: italic; }"
        "QLabel#totalSkusValueLabel { color: #ff8f1f; font-size: 20px; font-weight: 700; }"
        "QLabel#totalBarcodesValueLabel { color: #22c8ff; font-size: 20px; font-weight: 700; }"
        "QLabel#quarterBarcodesValueLabel { color: #f7f9fc; font-size: 20px; font-weight: 700; }"
        "QFrame#skuCard { background-color: #121a24; border: 1px solid #243246; border-radius: 12px; }"
        "QFrame#skuCard:hover { border-color: #ff8f1f; }"
        "QLabel#skuCardImage { background-color: #0e141d; border: 1px solid #2a3647; border-radius: 8px; }"
        "QLabel#skuCardSku { color: #ffb765; font-weight: 700; font-size: 12px; }"
        "QLabel#skuCardName { color: #f7f9fc; font-weight: 700; font-size: 18px; }"
        "QLabel#skuCardMeta { color: #cfd7e3; font-weight: 600; font-size: 12px; }"
        "QLabel#skuCardDate { color: #9fb0c6; font-size: 11px; }";
    m_darkStyleSheet = darkTheme;

    m_lightStyleSheet =
        "QMainWindow { background-color: #f6f7fb; color: #1b1f24; }"
        "QWidget { color: #1b1f24; }"
        "QGroupBox { background-color: #ffffff; border: 1px solid #d7dce3; border-radius: 10px; margin-top: 18px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; color: #2b5fab; font-weight: 700; }"
        "QLineEdit, QPlainTextEdit, QSpinBox, QComboBox {"
        "  background-color: #ffffff; border: 1px solid #c5ccd6; border-radius: 6px; padding: 6px; }"
        "QLineEdit:focus, QPlainTextEdit:focus, QSpinBox:focus, QComboBox:focus { border: 1px solid #2b5fab; }"
        "QComboBox QAbstractItemView { background-color: #ffffff; selection-background-color: #dbe9ff; }"
        "QTableView { background-color: #ffffff; gridline-color: #d7dce3; selection-background-color: #dbe9ff; selection-color: #1b1f24; }"
        "QHeaderView::section { background-color: #eef2f7; color: #1b1f24; border: 1px solid #d7dce3; padding: 6px; font-weight: 600; }"
        "QPushButton { background-color: #f0f3f7; border: 1px solid #c5ccd6; border-radius: 6px; padding: 6px 12px; font-weight: 600; }"
        "QPushButton:hover { background-color: #e4e9f1; }"
        "QPushButton:pressed { background-color: #d9e0ea; }"
        "QPushButton#dashboardSearchButton, QPushButton#searchButton, QPushButton#generateBarcodeButton, QPushButton#saveButton {"
        "  background-color: #2b5fab; color: #ffffff; border: 1px solid #3b6fc2; }"
        "QPushButton#dashboardSearchButton:hover, QPushButton#searchButton:hover, QPushButton#generateBarcodeButton:hover, QPushButton#saveButton:hover {"
        "  background-color: #3b6fc2; }"
        "QTabWidget::pane { border: 1px solid #d7dce3; }"
        "QTabBar::tab { background: #e9edf3; color: #1b1f24; padding: 8px 14px; border-top-left-radius: 8px; border-top-right-radius: 8px; margin-right: 6px; border: 1px solid #d7dce3; }"
        "QTabBar::tab:selected { background: #2b5fab; color: #ffffff; }"
        "QTabBar::tab:hover { background: #dfe6ef; }"
        "QScrollBar:vertical { background: #f0f2f6; width: 12px; margin: 0px; }"
        "QScrollBar::handle:vertical { background: #c9d1dc; border-radius: 6px; min-height: 22px; }"
        "QScrollBar::handle:vertical:hover { background: #b2bccb; }"
        "QScrollBar:horizontal { background: #f0f2f6; height: 12px; margin: 0px; }"
        "QScrollBar::handle:horizontal { background: #c9d1dc; border-radius: 6px; min-width: 22px; }"
        "QScrollBar::handle:horizontal:hover { background: #b2bccb; }"
        "QFrame#skuCard { background-color: #ffffff; border: 1px solid #d7dce3; border-radius: 12px; }"
        "QFrame#skuCard:hover { border-color: #2b5fab; }"
        "QLabel#skuCardImage { background-color: #f4f6fb; border: 1px solid #d7dce3; border-radius: 8px; }"
        "QLabel#skuCardSku { color: #2b5fab; font-weight: 700; font-size: 12px; }"
        "QLabel#skuCardName { color: #1b1f24; font-weight: 700; font-size: 18px; }"
        "QLabel#skuCardMeta { color: #4a5568; font-weight: 600; font-size: 12px; }"
        "QLabel#skuCardDate { color: #6b7280; font-size: 11px; }";

    m_mainTabs = ui->mainTabWidget;
    m_backupDbButton = ui->backupDbButton;
    m_totalSkusValueLabel = ui->totalSkusValueLabel;
    m_totalBarcodesValueLabel = ui->totalBarcodesValueLabel;
    m_quarterBarcodesValueLabel = ui->quarterBarcodesValueLabel;
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
    m_resultsView->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_resultsView->horizontalHeader()->setStretchLastSection(true);
    m_resultsView->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_resultsView->setContextMenuPolicy(Qt::CustomContextMenu);

    m_statusLabel = ui->statusLabel;
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setMinimumHeight(40);

    m_imagePreview = ui->searchImagePreviewLabel;
    m_imagePreview->setFixedSize(220, 220);
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

    m_skuField->setReadOnly(false);
    m_skuField->setAlignment(Qt::AlignCenter);
    m_skuField->setStyleSheet("QLineEdit { font-size: 18px; font-weight: 700; color: #ff9800; border: 1px solid #2a3544; }");
    m_itemSerialSpin->setRange(1, 999);
    m_variationSpin->setRange(1, 35);
    m_descriptionEdit->setFixedHeight(60);
    m_commentsEdit->setFixedHeight(60);
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
    m_barcodePreview->setFixedSize(160, 160);
    m_barcodePreview->setFrameShape(QFrame::StyledPanel);
    m_barcodePreview->setAlignment(Qt::AlignCenter);
    m_barcodeSkuImageLabel->setFixedSize(140, 140);
    m_barcodeSkuImageLabel->setFrameShape(QFrame::StyledPanel);
    m_barcodeSkuImageLabel->setAlignment(Qt::AlignCenter);
    m_historySkuImageLabel->setFixedSize(180, 180);
    m_historySkuImageLabel->setFrameShape(QFrame::StyledPanel);
    m_historySkuImageLabel->setAlignment(Qt::AlignCenter);

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
    connect(m_mainTabs, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);

    createMenusAndToolbars();
    m_printSettings.load();
    applyTheme(Theme::Dark);
}

void MainWindow::createMenusAndToolbars() {
    QMenu *fileMenu = menuBar()->addMenu("&File");
    m_actionLoadDb = fileMenu->addAction("Load DB...");
    m_actionExportDb = fileMenu->addAction("Export DB...");
    m_actionSaveDb = fileMenu->addAction("Save DB");
    fileMenu->addSeparator();
    m_actionExportSkuCsv = fileMenu->addAction("Export SKU Master (CSV)...");
    m_actionExportBarcodeSummaryCsv = fileMenu->addAction("Export QR Summary (CSV)...");
    fileMenu->addSeparator();
    m_actionExit = fileMenu->addAction("Exit");

    QMenu *viewMenu = menuBar()->addMenu("&View");
    m_actionFullScreen = viewMenu->addAction("Full Screen");
    m_actionFullScreen->setCheckable(true);

    QMenu *themeMenu = viewMenu->addMenu("Theme");
    QActionGroup *themeGroup = new QActionGroup(this);
    themeGroup->setExclusive(true);
    m_actionThemeDark = themeMenu->addAction("Dark");
    m_actionThemeDark->setCheckable(true);
    themeGroup->addAction(m_actionThemeDark);
    m_actionThemeLight = themeMenu->addAction("Light");
    m_actionThemeLight->setCheckable(true);
    themeGroup->addAction(m_actionThemeLight);
    m_actionThemeSystem = themeMenu->addAction("System Default");
    m_actionThemeSystem->setCheckable(true);
    themeGroup->addAction(m_actionThemeSystem);

    QMenu *optionsMenu = menuBar()->addMenu("&Options");
    m_actionPrintSettings = optionsMenu->addAction("QR Code / Sticker Settings...");

    connect(m_actionLoadDb, &QAction::triggered, this, &MainWindow::onLoadDbTriggered);
    connect(m_actionExportDb, &QAction::triggered, this, &MainWindow::onExportDbTriggered);
    connect(m_actionSaveDb, &QAction::triggered, this, &MainWindow::onSaveDbTriggered);
    if (m_actionExportSkuCsv) {
        connect(m_actionExportSkuCsv, &QAction::triggered, this, &MainWindow::exportSkuMasterCsv);
    }
    if (m_actionExportBarcodeSummaryCsv) {
        connect(m_actionExportBarcodeSummaryCsv, &QAction::triggered, this, &MainWindow::exportBarcodeSummaryCsv);
    }
    connect(m_actionExit, &QAction::triggered, this, &MainWindow::onExitTriggered);
    connect(m_actionFullScreen, &QAction::toggled, this, &MainWindow::onFullScreenToggled);
    connect(m_actionThemeDark, &QAction::triggered, this, &MainWindow::onThemeDark);
    connect(m_actionThemeLight, &QAction::triggered, this, &MainWindow::onThemeLight);
    connect(m_actionThemeSystem, &QAction::triggered, this, &MainWindow::onThemeSystem);
    connect(m_actionPrintSettings, &QAction::triggered, this, &MainWindow::onPrintSettingsTriggered);
}

void MainWindow::applyTheme(Theme theme) {
    m_currentTheme = theme;
    if (theme == Theme::Dark) {
        qApp->setStyleSheet(m_darkStyleSheet);
    } else if (theme == Theme::Light) {
        qApp->setStyleSheet(m_lightStyleSheet);
    } else {
        qApp->setStyleSheet(QString());
    }

    if (m_resultsView) {
        if (theme == Theme::Dark) {
            m_resultsView->setStyleSheet(
                "QTableView::item:selected { background: #324a6d; color: #ffffff; }"
                "QTableView { selection-background-color: #324a6d; selection-color: #ffffff; }");
        } else {
            m_resultsView->setStyleSheet(QString());
        }
    }

    if (m_actionThemeDark) {
        m_actionThemeDark->setChecked(theme == Theme::Dark);
    }
    if (m_actionThemeLight) {
        m_actionThemeLight->setChecked(theme == Theme::Light);
    }
    if (m_actionThemeSystem) {
        m_actionThemeSystem->setChecked(theme == Theme::System);
    }
}

bool MainWindow::selectDatabaseOnStartup() {
    QSettings settings;
    const QString savedPath = settings.value("db/path").toString().trimmed();
    if (!savedPath.isEmpty()) {
        if (openDatabaseAt(savedPath)) {
            return true;
        }
        settings.remove("db/path");
    }

    const QString startDir = dataDirPath();
    while (true) {
        QMessageBox prompt(this);
        prompt.setWindowTitle("Select Database Folder");
        prompt.setText("Choose the folder where the database should be stored.");
        QPushButton *chooseButton = prompt.addButton("Choose Folder", QMessageBox::AcceptRole);
        QPushButton *exitButton = prompt.addButton("Exit", QMessageBox::RejectRole);
        prompt.exec();

        QAbstractButton *clicked = prompt.clickedButton();
        if (!clicked || clicked == exitButton) {
            return false;
        }

        if (clicked == chooseButton) {
            const QString folder = QFileDialog::getExistingDirectory(
                this,
                "Select Database Folder",
                startDir);
            if (folder.isEmpty()) {
                continue;
            }

            const QString file = QDir(folder).filePath(AppGlobals::dbFileName());
            if (!openDatabaseAt(file)) {
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
    if (trimmed.isEmpty()) {
        return false;
    }

    if (m_db.isOpen()) {
        m_db.close();
    }

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
        setStatus(message, false);
        return false;
    }

    m_customDbPath = trimmed;
    QSettings settings;
    settings.setValue("db/path", trimmed);

    if (!ensureSchema()) {
        setStatus("Database schema check failed.", false);
        return false;
    }

    if (!ensureDefaultRoles()) {
        setStatus("Role setup failed.", false);
        return false;
    }
    if (!loadRulesIfEmpty()) {
        return false;
    }
    if (!loadCatalogIfEmpty()) {
        return false;
    }

    loadCategories();
    onCategoryChanged();
    searchRecords();
    loadSkuList();
    loadHistorySkuList();
    updateDashboardMetrics();

    setStatus(QString("Loaded database: %1").arg(QDir::toNativeSeparators(trimmed)), true);
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
        return false;
    }
    if (!QFile::exists(sourcePath)) {
        setStatus("Database file not found.", false);
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
        return false;
    }

    setStatus(QString("Database backup saved: %1").arg(QDir::toNativeSeparators(target)), true);
    return true;
}

QString MainWindow::defaultBackupPath() const {
    const QString backupDir = QDir(dataRootPath()).filePath("backups");
    QDir dir(backupDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    return dir.filePath(QString("sku_backup_%1.db").arg(stamp));
}

void MainWindow::onLoadDbTriggered() {
    const QString startDir = QFileInfo(currentDatabasePath()).absolutePath();
    const QString file = QFileDialog::getOpenFileName(
        this,
        "Load Database",
        startDir,
        "SQLite Database (*.db *.sqlite *.sqlite3);;All Files (*.*)");
    if (file.isEmpty()) {
        return;
    }
    openDatabaseAt(file);
}

void MainWindow::onExportDbTriggered() {
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
    out << "SI No,SKU,Part Number,Part Name,Category,Sub Category,Item Serial,Variation,Description,Storage,Rack Number,Bin Number,Dimensions,Weight Value,Weight Unit,Product Family,Image Path,Comments,Created At\n";

    QSqlQuery q(m_db);
    q.prepare(
        "SELECT si_no, sku, part_number, part_name, category_code, sub_category, item_serial, unique_variation, "
        "description, storage, rack_number, bin_number, dimensions, weight_value, weight_unit, "
        "product_family, image_path, comments, created_at "
        "FROM sku_catalog ORDER BY sku");
    if (q.exec()) {
        while (q.next()) {
            QStringList cells;
            cells.reserve(19);
            for (int i = 0; i < 19; ++i) {
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
              QString());
}

void MainWindow::exportBarcodeSummaryCsv() {
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
        "FROM barcode_log b "
        "LEFT JOIN sku_catalog s ON s.sku = b.sku "
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
              QString());
}

void MainWindow::onSaveDbTriggered() {
    const QString path = defaultBackupPath();
    backupDatabaseTo(path);
}

void MainWindow::onBackupDbClicked() {
    onExportDbTriggered();
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

void MainWindow::onThemeDark() {
    applyTheme(Theme::Dark);
}

void MainWindow::onThemeLight() {
    applyTheme(Theme::Light);
}

void MainWindow::onThemeSystem() {
    applyTheme(Theme::System);
}

void MainWindow::onPrintSettingsTriggered() {
    const bool updated = PrintSettingsDialog::edit(
        this,
        m_printSettings,
        [this](const QString &value) { return renderQrCode(value); });
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
            "  created_at TEXT"
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
            "  created_at TEXT NOT NULL"
            ")")) {
        return false;
    }

    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS app_audit_log ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  timestamp TEXT NOT NULL,"
            "  user TEXT NOT NULL,"
            "  action TEXT NOT NULL,"
            "  entity TEXT NOT NULL,"
            "  old_value TEXT,"
            "  new_value TEXT,"
            "  comment TEXT"
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
            "  created_at TEXT"
            ")")) {
        return false;
    }

    q.exec("CREATE INDEX IF NOT EXISTS idx_sku_catalog_sku ON sku_catalog(sku)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_sku_catalog_part_number ON sku_catalog(part_number)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_sku_catalog_part_name ON sku_catalog(part_name)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_sku_catalog_category ON sku_catalog(category_code, sub_category)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_app_users_username ON app_users(username)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_app_roles_key ON app_roles(role_key)");

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

    QSqlQuery migrateWeight(m_db);
    migrateWeight.exec("UPDATE sku_catalog SET weight_value = CAST(weight AS REAL) "
                       "WHERE (weight_value IS NULL OR weight_value = 0) AND weight IS NOT NULL");
    migrateWeight.exec("UPDATE sku_catalog SET weight_unit = 'kg' WHERE weight_unit IS NULL OR weight_unit = ''");

    return true;
}

bool MainWindow::loadRulesIfEmpty() {
    QSqlQuery check(m_db);
    if (!check.exec("SELECT COUNT(*) FROM sku_rules")) {
        return false;
    }
    if (check.next() && check.value(0).toInt() > 0) {
        return true;
    }

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

    m_db.transaction();
    QSqlQuery insert(m_db);
    insert.prepare(
        "INSERT INTO sku_rules (category_digit, category_name, subcategory_digit, subcategory_name) "
        "VALUES (?, ?, ?, ?)");

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

        insert.addBindValue(categoryDigit);
        insert.addBindValue(categoryName);
        insert.addBindValue(subDigit);
        insert.addBindValue(subName);
        if (!insert.exec()) {
            m_db.rollback();
            return false;
        }

        lastCategoryDigit = categoryDigit;
        lastCategoryName = categoryName;
    }

    m_db.commit();
    return true;
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
        const QString dimensions = normalizedText(fields.value(12));
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
    if (!q.exec("SELECT DISTINCT category_digit, category_name FROM sku_rules ORDER BY CAST(category_digit AS INTEGER)")) {
        return;
    }
    while (q.next()) {
        m_categoryCombo->addItem(q.value(1).toString(), q.value(0).toString());
    }
}

void MainWindow::loadSubCategories(const QString &categoryDigit) {
    if (!m_subCategoryCombo) {
        appendRunLog("loadSubCategories: m_subCategoryCombo is null");
        return;
    }
    m_subCategoryCombo->clear();
    QSqlQuery q(m_db);
    q.prepare("SELECT subcategory_digit, subcategory_name FROM sku_rules WHERE category_digit = ? ORDER BY CAST(subcategory_digit AS INTEGER)");
    q.addBindValue(categoryDigit);
    if (!q.exec()) {
        return;
    }
    while (q.next()) {
        m_subCategoryCombo->addItem(q.value(1).toString(), q.value(0).toString());
    }
}

void MainWindow::updateSerialsAndSku(bool resetVariation) {
    const QString categoryDigit = m_categoryCombo->currentData().toString();
    const QString subCategoryDigit = m_subCategoryCombo->currentData().toString();
    const QString categoryText = m_categoryCombo->currentText();
    const QString subCategoryText = m_subCategoryCombo->currentText();

    if (categoryDigit.isEmpty() || subCategoryDigit.isEmpty()) {
        return;
    }

    QSqlQuery q(m_db);
    q.prepare("SELECT MAX(item_serial) FROM sku_catalog WHERE category_code = ? AND sub_category = ?");
    q.addBindValue(categoryText);
    q.addBindValue(subCategoryText);
    int maxSerial = 0;
    if (q.exec() && q.next()) {
        if (!q.isNull(0)) {
            maxSerial = q.value(0).toInt();
        }
    }

    const int newSerial = maxSerial + 1;

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
        return;
    }
    const int lastSerial = fetchLastBarcodeSerial(sku, year, quarter);
    m_barcodeNextSerialField->setText(QString::number(lastSerial + 1));
    updateBarcodeSkuDetails();
}

void MainWindow::onBarcodePeriodChanged() {
    updateNextBarcodeSerial();
}

QString MainWindow::buildBarcodeValue(const QString &sku, int serial, int year, int quarter) const {
    return QString("SK%1%2%3%4")
        .arg(sku.trimmed().toUpper().remove(' '))
        .arg(quarter)
        .arg(year % 100, 2, 10, QLatin1Char('0'))
        .arg(serial, 5, 10, QLatin1Char('0'));
}

int MainWindow::fetchLastBarcodeSerial(const QString &sku, int year, int quarter) const {
    QSqlQuery q(m_db);
    q.prepare("SELECT MAX(serial) FROM barcode_log WHERE sku = ? AND year = ? AND quarter = ?");
    q.addBindValue(sku);
    q.addBindValue(year);
    q.addBindValue(quarter);
    if (q.exec() && q.next() && !q.isNull(0)) {
        return q.value(0).toInt();
    }
    return 0;
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
    q.prepare("SELECT COUNT(*) FROM sku_catalog WHERE sku = ?");
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
        q.prepare("SELECT sku, part_number, part_name FROM sku_catalog ORDER BY sku");
    } else {
        q.prepare(
            "SELECT sku, part_number, part_name FROM sku_catalog "
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
    if (q.exec("SELECT COUNT(*) FROM sku_catalog") && q.next()) {
        totalSkus = q.value(0).toInt();
    }

    int totalBarcodes = 0;
    if (q.exec("SELECT COUNT(*) FROM barcode_log") && q.next()) {
        totalBarcodes = q.value(0).toInt();
    }

    const int quarter = currentQuarter();
    const int year = currentYear();
    int quarterCount = 0;
    q.prepare("SELECT COUNT(*) FROM barcode_log WHERE year = ? AND quarter = ?");
    q.addBindValue(year);
    q.addBindValue(quarter);
    if (q.exec() && q.next()) {
        quarterCount = q.value(0).toInt();
    }

    m_totalSkusValueLabel->setText(QString::number(totalSkus));
    m_totalBarcodesValueLabel->setText(QString::number(totalBarcodes));
    m_quarterBarcodesValueLabel->setText(QString::number(quarterCount));

    refreshLatestSkuCards();
}

void MainWindow::updateBarcodeSkuDetails() {
    const QString sku = extractSkuFromDisplay(m_barcodeSkuCombo->currentText());
    if (sku.isEmpty()) {
        m_barcodePartNameValueLabel->setText("-");
        m_barcodePartNumberValueLabel->setText("-");
        if (m_barcodeQuantityWordsValueLabel) {
            m_barcodeQuantityWordsValueLabel->setText("-");
        }
        loadBarcodeSkuImage(QByteArray(), QString());
        return;
    }

    QSqlQuery q(m_db);
    q.prepare("SELECT part_name, part_number, image_blob, image_path FROM sku_catalog WHERE sku = ?");
    q.addBindValue(sku);
    if (q.exec() && q.next()) {
        m_barcodePartNameValueLabel->setText(q.value(0).toString());
        m_barcodePartNumberValueLabel->setText(q.value(1).toString());
        loadBarcodeSkuImage(q.value(2).toByteArray(), q.value(3).toString());
    } else {
        m_barcodePartNameValueLabel->setText("-");
        m_barcodePartNumberValueLabel->setText("-");
        loadBarcodeSkuImage(QByteArray(), QString());
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

    while (QLayoutItem *item = m_latestSkuGridLayout->takeAt(0)) {
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }

    const int year = selectedBarcodeYear();
    const int quarter = selectedBarcodeQuarter();
    QSqlQuery q(m_db);
    q.prepare(
        "SELECT s.sku, s.part_name, s.image_blob, s.image_path, s.created_at, "
        "COALESCE(t.total_count, 0) AS total_serials, "
        "COALESCE(qtr.qtr_count, 0) AS quarter_incoming "
        "FROM sku_catalog s "
        "LEFT JOIN (SELECT sku, COUNT(*) AS total_count FROM barcode_log GROUP BY sku) t "
        "  ON t.sku = s.sku "
        "LEFT JOIN (SELECT sku, COUNT(*) AS qtr_count FROM barcode_log WHERE year = ? AND quarter = ? GROUP BY sku) qtr "
        "  ON qtr.sku = s.sku "
        "ORDER BY datetime(s.created_at) DESC, s.id DESC LIMIT 6");
    q.addBindValue(year);
    q.addBindValue(quarter);
    q.exec();

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
        card->setFixedSize(320, 170);

        auto *shadow = new QGraphicsDropShadowEffect(card);
        shadow->setBlurRadius(18);
        shadow->setOffset(0, 6);
        shadow->setColor(QColor(0, 0, 0, 140));
        card->setGraphicsEffect(shadow);

        auto *cardLayout = new QHBoxLayout(card);
        cardLayout->setContentsMargins(12, 12, 12, 12);
        cardLayout->setSpacing(10);

        auto *imageLabel = new QLabel(card);
        imageLabel->setObjectName("skuCardImage");
        imageLabel->setFixedSize(88, 88);
        imageLabel->setAlignment(Qt::AlignCenter);
        loadImageLabelFromData(imageLabel, imageData, imagePath, AppGlobals::noImageText());

        auto *skuLabel = new QLabel(QString("SKU: %1").arg(sku), card);
        skuLabel->setObjectName("skuCardSku");

        auto *nameLabel = new QLabel(partName, card);
        nameLabel->setWordWrap(true);
        nameLabel->setObjectName("skuCardName");

        auto *incomingLabel = new QLabel(
            QString("Incoming (Q%1 %2): %3").arg(quarter).arg(year).arg(quarterIncoming), card);
        incomingLabel->setObjectName("skuCardMeta");

        auto *totalLabel = new QLabel(QString("Total Serials: %1").arg(totalSerials), card);
        totalLabel->setObjectName("skuCardMeta");

        auto *dateLabel = new QLabel(createdAt.isEmpty() ? AppGlobals::noDateText()
                                                         : QString("Date: %1").arg(createdAt), card);
        dateLabel->setObjectName("skuCardDate");

        auto *textLayout = new QVBoxLayout();
        textLayout->setContentsMargins(0, 0, 0, 0);
        textLayout->setSpacing(4);
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

        const int row = index / 3;
        const int col = index % 3;
        m_latestSkuGridLayout->addWidget(card, row, col);
        index++;
    }

    if (index == 0) {
        auto *emptyLabel = new QLabel("No SKUs yet.", m_latestSkuContainer);
        emptyLabel->setAlignment(Qt::AlignCenter);
        emptyLabel->setStyleSheet("QLabel { color: #a7b3c6; }");
        m_latestSkuGridLayout->addWidget(emptyLabel, 0, 0);
    }
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
    m_statusLabel->setText(message);
    const QString color = ok ? "#1b5e20" : "#c62828";
    m_statusLabel->setStyleSheet(QString("color: %1;").arg(color));
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
}

void MainWindow::showResultsContextMenu(const QPoint &pos) {
    const QModelIndex index = m_resultsView->indexAt(pos);
    if (!index.isValid()) {
        return;
    }

    QMenu menu(this);
    QAction *copyCell = menu.addAction("Copy Cell");
    QAction *copyRow = menu.addAction("Copy Row");
    QAction *copyColumn = menu.addAction("Copy Column");
    QAction *selectedAction = menu.exec(m_resultsView->viewport()->mapToGlobal(pos));
    if (!selectedAction) {
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
        "FROM sku_catalog ORDER BY si_no DESC";

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
        "FROM sku_catalog WHERE " + clauses.join(" OR ") + " ORDER BY si_no DESC";

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
    m_statusLabel->clear();
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

    const int catIndex = m_categoryCombo->findText(categoryCode);
    if (catIndex >= 0) {
        m_categoryCombo->setCurrentIndex(catIndex);
    }
    const int subIndex = m_subCategoryCombo->findText(subCategory);
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
    m_dimensionsField->setText(dimensions);
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
    dup.prepare("SELECT COUNT(*) FROM sku_catalog WHERE sku = ?");
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
    insert.addBindValue(m_categoryCombo->currentText());
    insert.addBindValue(m_subCategoryCombo->currentText());
    insert.addBindValue(m_itemSerialSpin->value());
    insert.addBindValue(m_variationSpin->value());
    insert.addBindValue(m_descriptionEdit->toPlainText().trimmed());
    insert.addBindValue(normalizedText(m_storageField->text()));
    insert.addBindValue(normalizedText(m_rackNumberField ? m_rackNumberField->text() : QString()));
    insert.addBindValue(normalizedText(m_binNumberField ? m_binNumberField->text() : QString()));
    insert.addBindValue(normalizedText(m_dimensionsField->text()));
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
        return;
    }

    QJsonObject newValue;
    newValue.insert("sku", sku);
    newValue.insert("part_number", normalizedText(m_partNumberField->text()));
    newValue.insert("part_name", normalizedText(m_partNameField->text()));
    newValue.insert("category", m_categoryCombo->currentText());
    newValue.insert("sub_category", m_subCategoryCombo->currentText());
    newValue.insert("item_serial", m_itemSerialSpin->value());
    newValue.insert("variation", m_variationSpin->value());
    newValue.insert("description", m_descriptionEdit->toPlainText().trimmed());
    newValue.insert("storage", normalizedText(m_storageField->text()));
    newValue.insert("rack_number", normalizedText(m_rackNumberField ? m_rackNumberField->text() : QString()));
    newValue.insert("bin_number", normalizedText(m_binNumberField ? m_binNumberField->text() : QString()));
    newValue.insert("dimensions", normalizedText(m_dimensionsField->text()));
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
              QString());

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
    const int year = currentYear();
    const int quarter = currentQuarter();
    const int lastSerial = fetchLastBarcodeSerial(sku, year, quarter);

    QList<QStringList> rows;
    rows.reserve(quantity);
    m_lastGeneratedBarcodes.clear();

    m_db.transaction();
    QSqlQuery log(m_db);
    log.prepare("INSERT INTO barcode_log (sku, year, quarter, serial, barcode, created_at) VALUES (?,?,?,?,?,?)");

    for (int i = 1; i <= quantity; ++i) {
        const int serial = lastSerial + i;
        const QString barcodeValue = buildBarcodeValue(sku, serial, year, quarter);
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
            return;
        }

        rows.append({barcodeValue, sku, QString::number(serial), QString("Q%1").arg(quarter), QString::number(year), createdAt});
        m_lastGeneratedBarcodes.append(barcodeValue);
    }

    m_db.commit();

    QSqlQuery up(m_db);
    up.prepare("INSERT OR REPLACE INTO barcode_serials (sku, year, quarter, last_serial) VALUES (?,?,?,?)");
    up.addBindValue(sku);
    up.addBindValue(year);
    up.addBindValue(quarter);
    up.addBindValue(lastSerial + quantity);
    up.exec();

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

    m_barcodeNextSerialField->setText(QString::number(lastSerial + quantity + 1));
    setStatus(QString("Generated %1 QR code(s) for %2.").arg(quantity).arg(sku), true);
    updateDashboardMetrics();
    updateQuantityWordsLabels(sku);
    loadHistoryForSelectedSku();

    QJsonObject logDetails;
    logDetails.insert("quantity", quantity);
    logDetails.insert("year", year);
    logDetails.insert("quarter", quarter);
    logDetails.insert("serial_start", lastSerial + 1);
    logDetails.insert("serial_end", lastSerial + quantity);
    logAction("BARCODE_GENERATE",
              sku,
              QString(),
              QString::fromUtf8(QJsonDocument(logDetails).toJson(QJsonDocument::Compact)),
              QString());
}


void MainWindow::deleteSelectedBarcodes() {
    if (!requireAccess(m_access.canDelete, "You don't have permission to delete QR codes.")) {
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
    selectMeta.prepare("SELECT sku, year, quarter, serial, created_at FROM barcode_log WHERE barcode = ?");
    QSqlQuery del(m_db);
    del.prepare("DELETE FROM barcode_log WHERE barcode = ?");

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

        del.addBindValue(barcode);
        if (!del.exec()) {
            m_db.rollback();
            setStatus("Failed to delete serials.", false);
            return;
        }
        del.finish();

        if (!sku.isEmpty()) {
            QJsonObject oldValue;
            oldValue.insert("sku", sku);
            oldValue.insert("barcode", barcode);
            oldValue.insert("serial", serial);
            oldValue.insert("quarter", quarter);
            oldValue.insert("year", year);
            oldValue.insert("created_at", createdAt);
            logAction("SERIAL_DELETE",
                      sku,
                      QString::fromUtf8(QJsonDocument(oldValue).toJson(QJsonDocument::Compact)),
                      QString(),
                      comment);
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

void MainWindow::printBarcodes() {
    QStringList toPrint = m_lastGeneratedBarcodes;
    if (toPrint.isEmpty()) {
        const QString current = m_barcodeValueField->text().trimmed();
        if (!current.isEmpty()) {
            toPrint << current;
        }
    }

    if (toPrint.isEmpty()) {
        setStatus("No QR codes to export.", false);
        return;
    }

    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QDir exportDir(dataDirPath());
    if (!exportDir.exists()) {
        exportDir.mkpath(".");
    }
    const QString defaultName = exportDir.filePath(QString("qr_codes_%1.pdf").arg(stamp));
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
                  "FROM barcode_log b LEFT JOIN sku_catalog s ON s.sku = b.sku "
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

    const qreal edgeMarginMm = 2.0;
    const qreal contentLeftMm = edgeMarginMm;
    const qreal contentTopMm = edgeMarginMm;
    const qreal contentRightMm = labelWidthMm - edgeMarginMm;
    const qreal contentBottomMm = labelHeightMm - edgeMarginMm;
    const qreal contentHeightMm = qMax(1.0, contentBottomMm - contentTopMm);

    const qreal qrWidthMm = m_printSettings.barcodeWidthMm;
    const qreal qrHeightMm = m_printSettings.barcodeHeightMm;
    const qreal qrXmm = qMax(contentLeftMm, contentRightMm - qrWidthMm);
    const qreal qrYmm = qMax(contentTopMm, contentBottomMm - qrHeightMm);
    const qreal logoSizeMm = 5.0;
    const qreal logoMarginMm = 0.0;
    const qreal textLeftMm = qMax(m_printSettings.innerMarginMm, contentLeftMm);
    const qreal textBlockWidthMm = qMax(1.0, qrXmm - textLeftMm);
    const qreal textTopMm = contentTopMm;
    const int partNameLines = 3;
    const int detailLines = 5;
    const int totalLines = partNameLines + detailLines;
    const qreal lineHeightMm = qMin(3.5, contentHeightMm / qMax(1, totalLines));
    const qreal partNameHeightMm = lineHeightMm * partNameLines;

    QFont textFont("Britannic Bold");
    textFont.setBold(false);
    const int linePx = qMax(1, static_cast<int>(mmToPx(lineHeightMm)));
    textFont.setPixelSize(qMax(1, static_cast<int>(linePx * 0.68)));
    painter.setFont(textFont);

    QImage logoImage(":/assets/sticker_logo.png");
    if (logoImage.isNull()) {
        const QString fallback = QDir(QCoreApplication::applicationDirPath())
                                     .filePath("Assets/sticker_logo.png");
        logoImage.load(fallback);
    }

    for (int i = 0; i < items.size(); ++i) {
        if (i > 0) {
            printer.newPage();
        }

        const PrintItem &item = items.at(i);
        const QString partLine = QString("Part Name: %1").arg(item.partName.isEmpty() ? "-" : item.partName);
        const QString skuLine = QString("SKU: %1").arg(item.sku.isEmpty() ? "-" : item.sku);
        const QString rackLine = QString("Rack Number: %1").arg(item.rackNumber.isEmpty() ? "-" : item.rackNumber);
        const QString binLine = QString("Bin Number: %1").arg(item.binNumber.isEmpty() ? "-" : item.binNumber);
        const QString codeLine = item.barcode.isEmpty() ? "-" : item.barcode;
        const QString siteLine = "www.skykart.in";

        painter.setPen(Qt::black);
        const qreal textXpx = mmToPx(textLeftMm);
        const qreal textWidthPx = mmToPx(textBlockWidthMm);

        auto drawTextBlock = [&](const QString &text, qreal yMm, qreal hMm, bool wrap, bool fitToWidth) {
            QFont useFont = textFont;
            if (fitToWidth) {
                QFontMetrics fm(useFont);
                const int textPx = fm.horizontalAdvance(text);
                if (textPx > 0 && textPx > static_cast<int>(textWidthPx)) {
                    const qreal scale = textWidthPx / textPx;
                    useFont.setPixelSize(qMax(1, static_cast<int>(useFont.pixelSize() * scale)));
                }
            }
            painter.setFont(useFont);
            QFontMetrics fm(useFont);
            QRectF rect(textXpx, mmToPx(yMm), textWidthPx, mmToPx(hMm));
            const int flags = wrap ? (Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap)
                                   : (Qt::AlignLeft | Qt::AlignVCenter);
            const QString textToDraw = wrap ? text
                                            : fm.elidedText(text, Qt::ElideRight, static_cast<int>(textWidthPx));
            painter.drawText(rect, flags, textToDraw);
        };

        drawTextBlock(partLine, textTopMm, partNameHeightMm, true, false);
        drawTextBlock(skuLine, textTopMm + partNameHeightMm, lineHeightMm, false, false);
        drawTextBlock(rackLine, textTopMm + partNameHeightMm + lineHeightMm, lineHeightMm, false, false);
        drawTextBlock(binLine, textTopMm + partNameHeightMm + lineHeightMm * 2, lineHeightMm, false, false);
        drawTextBlock(codeLine, textTopMm + partNameHeightMm + lineHeightMm * 3, lineHeightMm, false, true);
        drawTextBlock(siteLine, textTopMm + partNameHeightMm + lineHeightMm * 4, lineHeightMm, false, true);

        if (!logoImage.isNull()) {
            const QRectF logoRect(mmToPx(contentRightMm - logoMarginMm - logoSizeMm),
                                  mmToPx(contentTopMm + logoMarginMm),
                                  mmToPx(logoSizeMm),
                                  mmToPx(logoSizeMm));
            QImage logoScaled = logoImage.scaled(logoRect.size().toSize(),
                                                 Qt::KeepAspectRatio,
                                                 Qt::SmoothTransformation);
            const qreal bx = logoRect.left() + (logoRect.width() - logoScaled.width()) / 2.0;
            const qreal by = logoRect.top() + (logoRect.height() - logoScaled.height()) / 2.0;
            painter.drawImage(QPointF(bx, by), logoScaled);
        }

        const QRectF qrRect(mmToPx(qrXmm), mmToPx(qrYmm), mmToPx(qrWidthMm), mmToPx(qrHeightMm));
        const QImage qrImage = renderQrCode(item.barcode);
        if (!qrImage.isNull()) {
            QImage scaled = qrImage.scaled(qrRect.size().toSize(), Qt::KeepAspectRatio, Qt::FastTransformation);
            const qreal bx = qrRect.left() + (qrRect.width() - scaled.width()) / 2.0;
            const qreal by = qrRect.top() + (qrRect.height() - scaled.height()) / 2.0;
            painter.drawImage(QPointF(bx, by), scaled);
        }
    }

    painter.end();
    setStatus(QString("QR codes exported to %1").arg(QDir::toNativeSeparators(chosen)), true);
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
    dup.prepare("SELECT COUNT(*) FROM sku_catalog WHERE sku = ? AND id != ?");
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
        "FROM sku_catalog WHERE id = ?");
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
        "WHERE id = ?");

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
    update.addBindValue(m_categoryCombo->currentText());
    update.addBindValue(m_subCategoryCombo->currentText());
    update.addBindValue(m_itemSerialSpin->value());
    update.addBindValue(m_variationSpin->value());
    update.addBindValue(m_descriptionEdit->toPlainText().trimmed());
    update.addBindValue(normalizedText(m_storageField->text()));
    update.addBindValue(normalizedText(m_rackNumberField ? m_rackNumberField->text() : QString()));
    update.addBindValue(normalizedText(m_binNumberField ? m_binNumberField->text() : QString()));
    update.addBindValue(normalizedText(m_dimensionsField->text()));
    update.addBindValue(weightValue);
    update.addBindValue("kg");
    update.addBindValue(normalizedText(m_productFamilyField->text()));
    update.addBindValue(imageData);
    update.addBindValue(imagePathForSave);
    update.addBindValue(m_commentsEdit->toPlainText().trimmed());
    update.addBindValue(m_selectedId);

    if (!update.exec()) {
        setStatus("Failed to update entry.", false);
        return;
    }

    QJsonObject newValue;
    newValue.insert("sku", sku);
    newValue.insert("part_number", normalizedText(m_partNumberField->text()));
    newValue.insert("part_name", normalizedText(m_partNameField->text()));
    newValue.insert("category", m_categoryCombo->currentText());
    newValue.insert("sub_category", m_subCategoryCombo->currentText());
    newValue.insert("item_serial", m_itemSerialSpin->value());
    newValue.insert("variation", m_variationSpin->value());
    newValue.insert("description", m_descriptionEdit->toPlainText().trimmed());
    newValue.insert("storage", normalizedText(m_storageField->text()));
    newValue.insert("rack_number", normalizedText(m_rackNumberField ? m_rackNumberField->text() : QString()));
    newValue.insert("bin_number", normalizedText(m_binNumberField ? m_binNumberField->text() : QString()));
    newValue.insert("dimensions", normalizedText(m_dimensionsField->text()));
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
              QString());

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
    fetch.prepare("SELECT sku FROM sku_catalog WHERE id = ?");
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
    if (!promptAdminAuthorization("Delete SKU", &comment, false)) {
        return;
    }

    QString oldValueJson;
    QSqlQuery oldFetch(m_db);
    oldFetch.prepare(
        "SELECT sku, part_number, part_name, category_code, sub_category, item_serial, unique_variation, "
        "description, storage, rack_number, bin_number, dimensions, weight_value, weight_unit, "
        "product_family, image_blob, image_path, comments "
        "FROM sku_catalog WHERE id = ?");
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
    delSku.prepare("DELETE FROM sku_catalog WHERE id = ?");
    delSku.addBindValue(m_selectedId);
    if (!delSku.exec()) {
        m_db.rollback();
        setStatus("Failed to delete SKU.", false);
        return;
    }

    QSqlQuery delSerials(m_db);
    delSerials.prepare("DELETE FROM barcode_log WHERE sku = ?");
    delSerials.addBindValue(sku);
    delSerials.exec();

    QSqlQuery delTrack(m_db);
    delTrack.prepare("DELETE FROM barcode_serials WHERE sku = ?");
    delTrack.addBindValue(sku);
    delTrack.exec();

    m_db.commit();

    logAction("SKU_DELETE", sku, oldValueJson, QString(), comment);

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
    q.prepare("SELECT image_blob, image_path FROM sku_catalog WHERE id = ?");
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
    maxq.prepare("SELECT MAX(serial) FROM barcode_log WHERE sku = ? AND year = ? AND quarter = ?");
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

