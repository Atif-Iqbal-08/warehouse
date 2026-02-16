#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSqlDatabase>
#include <QImage>
#include <QByteArray>
#include "printsettingsdialog.h"

namespace Ui {
class MainWindow;
}

class QComboBox;
class QDateTime;
class QLineEdit;
class QLabel;
class QPushButton;
class QSpinBox;
class QPlainTextEdit;
class QTableView;
class QStandardItemModel;
class QModelIndex;
class QTabWidget;
class QGridLayout;
class QScrollArea;
class QWidget;
class QPoint;
class QAction;
class QTableWidget;
class QEvent;
class QCloseEvent;
class QTimer;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void searchRecords();
    void clearSearch();
    void fillFormFromSearch();
    void saveForm();
    void updateSelected();
    void deleteSelectedSku();
    void clearFormFields();
    void browseImage();

    void onCategoryChanged();
    void onSubCategoryChanged();
    void onSerialOrVariationChanged();
    void onSearchSelectionChanged(const QModelIndex &current, const QModelIndex &previous);
    void generateBarcodes();
    void printBarcodes();
    void deleteSelectedBarcodes();
    void onBarcodeSkuInputChanged(const QString &text);
    void onBarcodeSkuActivated(int index);
    void onBarcodeTableSelectionChanged(const QModelIndex &current, const QModelIndex &previous);
    void showResultsContextMenu(const QPoint &pos);
    void onHistorySkuInputChanged(const QString &text);
    void onHistorySkuChanged(int index);
    void loadHistoryForSelectedSku();
    void onHistoryTableSelectionChanged(const QModelIndex &current, const QModelIndex &previous);
    void deleteSelectedHistoryBarcodes();
    void editSelectedHistoryBarcode();
    void onHistorySerialSearchChanged(const QString &text);
    void clearBarcodeFields();
    void clearHistoryFields();
    void exportSkuMasterCsv();
    void exportBarcodeSummaryCsv();
    void onTabChanged(int index);
    void onBarcodePeriodChanged();
    void onLoadDbTriggered();
    void onExportDbTriggered();
    void onSaveDbTriggered();
    void onBackupDbClicked();
    void onExitTriggered();
    void onFullScreenToggled(bool enabled);
    void onThemeDark();
    void onThemeLight();
    void onThemeSystem();
    void onHelpGuidesTriggered();
    void onPrintSettingsTriggered();
    void onManageUsersTriggered();
    void onSwitchUserTriggered();

private:
    enum class Theme {
        Dark,
        Light,
        System
    };

    enum class UserRole {
        ViewOnly,
        AddOnly,
        FullAccess
    };

    struct AccessPolicy {
        bool canAdd = false;
        bool canEdit = false;
        bool canDelete = false;
        bool canSerialEdit = false;
        bool canSerialDelete = false;
        bool canPrint = false;
        bool canManageUsers = false;
        bool canBackupRestore = false;
        bool canExport = false;
    };

    void setupUi();
    void createMenusAndToolbars();
    void applyTheme(Theme theme);
    void applyAccessControl(const QString &roleKey);
    AccessPolicy accessPolicyForBaseRole(UserRole role) const;
    bool requireAccess(bool allowed, const QString &message);
    bool ensureInitialAdmin();
    bool promptLogin();
    bool promptCreateUser(const QString &forcedRoleKey, bool allowCancel);
    bool createUserAccount(const QString &username,
                           const QString &password,
                           const QString &roleKey,
                           const QString &email,
                           const QString &userId,
                           const QString &fullName,
                           QString *errorMessage = nullptr);
    bool verifyUserCredentials(const QString &username,
                               const QString &password,
                               QString *roleKeyOut);
    QString normalizeUsername(const QString &username) const;
    QString baseRoleToStorage(UserRole role) const;
    UserRole baseRoleFromStorage(const QString &role) const;
    QString baseRoleToDisplay(UserRole role) const;
    QString roleDisplayName(const QString &roleKey) const;
    QString roleKeyFromName(const QString &name) const;
    QString generateSalt() const;
    QString hashPasswordWithSalt(const QString &password, const QString &salt) const;
    void updateWindowTitleWithUser();
    bool ensureDefaultRoles();
    bool fetchRoleInfo(const QString &roleKey, UserRole *baseRole, QString *displayName) const;
    void loadRolesIntoCombo(QComboBox *combo);
    void showSettingsDialog();
    void showUserAccountsDialog();
    void loadUsersIntoTable(QTableWidget *table);
    void showSkuDetailsDialog(const QString &sku);
    void showHelpGuidesDialog();
    bool selectDatabaseOnStartup();
    bool promptAdminAuthorization(const QString &action, QString *commentOut, bool requireComment);
    bool verifyAdminCredentials(const QString &username, const QString &password);
    bool logAction(const QString &action,
                   const QString &entity,
                   const QString &oldValue,
                   const QString &newValue,
                   const QString &comment,
                   const QString &module = QString(),
                   const QString &actionType = QString(),
                   bool success = true,
                   const QString &errorMessage = QString(),
                   const QString &recordId = QString());
    bool applyRolePolicy(const QString &roleKey, AccessPolicy *policyOut) const;
    bool applyUserOverrides(const QString &username, AccessPolicy *policy) const;
    bool upsertRolePermissions(const QString &roleKey, const AccessPolicy &policy);
    QString rolePolicySummary(const AccessPolicy &policy) const;
    AccessPolicy rolePolicyFor(const QString &roleKey) const;
    bool hasPermissionOverride(const QString &username) const;
    bool upsertUserPermissionOverride(const QString &username, const AccessPolicy *policy);
    QString machineId() const;
    bool isStrongPassword(const QString &password, QString *reasonOut = nullptr) const;
    bool isPermissionDeniedOpenError(const QString &errorText) const;
    bool requestUacElevationForDatabase(const QString &dbPath, const QString &errorText);
    void appendRunLogWithUser(const QString &message) const;
    void registerUiInteractionLogging();
    bool importBootstrapAdminFromSettings();
    QString configuredBackupRoot() const;
    QString automatedBackupFilePath(const QString &classification) const;
    bool createEncryptedBackup(const QString &sourcePath,
                               const QString &targetPath,
                               QString *checksumOut,
                               qint64 *sizeOut,
                               QString *metadataOut,
                               QString *errorOut);
    bool restoreEncryptedBackup(const QString &sourcePath,
                                const QString &targetPath,
                                QString *errorOut);
    bool pruneBackupRetention(const QString &backupRoot, QString *errorOut = nullptr);
    void scheduleAutomatedBackups();
    void evaluateAutomatedBackupWindow();
    void scheduleNextBackupTick();
    bool shouldRunBootFallback() const;
    bool runAutomatedBackup(const QString &trigger, bool silent);
    int totalQuantityForSku(const QString &sku) const;
    QString numberToWords(int value) const;
    void updateQuantityWordsLabels(const QString &sku);
    QString extractSkuFromDisplay(const QString &text) const;
    void resetBarcodeFieldsForSkuChange();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    bool openDatabaseAt(const QString &path);
    QString currentDatabasePath() const;
    bool backupDatabaseTo(const QString &targetPath);
    QString defaultBackupPath() const;
    bool initDb();
    bool ensureSchema();
    bool loadRulesIfEmpty();
    bool loadCatalogIfEmpty();
    QString findRulesCsvPath() const;
    QString findCatalogCsvPath() const;
    QString catalogImportFlagPath() const;
    QString dataRootPath() const;
    QString dataDirPath() const;
    void migrateLegacyDataIfNeeded();

    void loadCategories();
    void loadSubCategories(const QString &categoryDigit);
    void updateSerialsAndSku(bool resetVariation);
    void updateSkuPreview();
    QString currentSkuValue() const;
    QString selectedBarcodePrefix() const;
    QString barcodePrefixFromValue(const QString &barcodeValue) const;
    void updateNextBarcodeSerial();
    QString buildBarcodeValue(const QString &sku, int serial, int year, int quarter, const QString &prefix) const;
    int fetchLastBarcodeSerial(const QString &sku, int year, int quarter) const;
    int currentQuarter() const;
    int currentYear() const;
    int selectedBarcodeQuarter() const;
    int selectedBarcodeYear() const;
    QString stickerWebsiteForPrefix(const QString &prefix) const;
    QImage stickerLogoForPrefix(const QString &prefix) const;
    bool skuExists(const QString &sku) const;
    void loadSkuList(const QString &filter = QString(), bool preserveText = false);
    QImage renderQrCode(const QString &value) const;
    void populateBarcodeModel(const QList<QStringList> &rows);
    void updateDashboardMetrics();
    void refreshLatestSkuCards();
    void updateBarcodeSkuDetails();
    void loadBarcodeSkuImage(const QByteArray &data, const QString &legacyPath);
    void updateHistorySkuDetails();
    void updateHistoryBarcodeDetails();
    void loadHistorySkuImage(const QByteArray &data, const QString &legacyPath);
    void loadHistorySkuList(const QString &filter = QString(), bool preserveText = false);
    void loadBarcodeHistory(const QString &sku);
    void populateHistoryModel(const QList<QStringList> &rows);
    void refreshBarcodeSerialTracker(const QString &sku, int year, int quarter);
    void loadImageLabel(QLabel *label, const QString &path, const QString &fallbackText);
    void loadImageLabelFromData(QLabel *label, const QByteArray &data, const QString &legacyPath, const QString &fallbackText);
    QString resolveImagePath(const QString &path) const;
    QByteArray loadImageBytesFromPath(const QString &path) const;
    void updateImagePathFieldDisplay(const QString &fileNameHint = QString());

    QString computeSku(const QString &categoryDigit,
                       const QString &subCategoryDigit,
                       int itemSerial,
                       int variation) const;
    QString getVariationCode(int num) const;

    void setStatus(const QString &message, bool ok);
    void updateNoDbBanner();
    void populateResultsModel(const QList<QStringList> &rows);
    void loadImagePreview(const QByteArray &data, const QString &legacyPath = QString());
    void loadImageForSelectedId(int id);

    QString dbPath() const;
    QString imagesDirPath() const;

    QSqlDatabase m_db;
    Ui::MainWindow *ui = nullptr;

    QLineEdit *m_searchSku = nullptr;
    QLineEdit *m_searchPartNumber = nullptr;
    QLineEdit *m_searchPartName = nullptr;
    QPushButton *m_searchButton = nullptr;
    QPushButton *m_clearSearchButton = nullptr;
    QPushButton *m_fillFromSearchButton = nullptr;

    QTableView *m_resultsView = nullptr;
    QStandardItemModel *m_resultsModel = nullptr;

    QLabel *m_statusLabel = nullptr;
    QLabel *m_noDbBannerLabel = nullptr;

    QLineEdit *m_skuField = nullptr;
    QLineEdit *m_partNameField = nullptr;
    QLineEdit *m_partNumberField = nullptr;
    QComboBox *m_categoryCombo = nullptr;
    QComboBox *m_subCategoryCombo = nullptr;
    QSpinBox *m_itemSerialSpin = nullptr;
    QSpinBox *m_variationSpin = nullptr;

    QLineEdit *m_dimensionsField = nullptr;
    QLineEdit *m_weightField = nullptr;
    QPlainTextEdit *m_descriptionEdit = nullptr;
    QLineEdit *m_storageField = nullptr;
    QLineEdit *m_rackNumberField = nullptr;
    QLineEdit *m_binNumberField = nullptr;
    QLineEdit *m_productFamilyField = nullptr;
    QLineEdit *m_imagePathField = nullptr;
    QPushButton *m_browseImageButton = nullptr;
    QLabel *m_imagePreview = nullptr;
    QPlainTextEdit *m_commentsEdit = nullptr;

    QPushButton *m_clearFormButton = nullptr;
    QPushButton *m_saveButton = nullptr;
    QPushButton *m_updateButton = nullptr;
    QPushButton *m_deleteSkuButton = nullptr;

    QTabWidget *m_mainTabs = nullptr;
    QPushButton *m_backupDbButton = nullptr;
    QLabel *m_totalSkusValueLabel = nullptr;
    QLabel *m_totalBarcodesValueLabel = nullptr;
    QLabel *m_quarterBarcodesValueLabel = nullptr;
    QLabel *m_companyLogoLabel = nullptr;
    QLabel *m_companyNameLabel = nullptr;
    QLabel *m_softwareNameLabel = nullptr;
    QLabel *m_authorLabel = nullptr;
    QScrollArea *m_latestSkuScrollArea = nullptr;
    QWidget *m_latestSkuContainer = nullptr;
    QGridLayout *m_latestSkuGridLayout = nullptr;
    QLabel *m_searchQuantityWordsValueLabel = nullptr;
    QComboBox *m_historySkuCombo = nullptr;
    QLineEdit *m_historySerialSearchField = nullptr;
    QPushButton *m_historyRefreshButton = nullptr;
    QPushButton *m_historyEditButton = nullptr;
    QPushButton *m_historyDeleteButton = nullptr;
    QPushButton *m_historyClearFieldsButton = nullptr;
    QTableView *m_historyTableView = nullptr;
    QStandardItemModel *m_historyModel = nullptr;
    QLabel *m_historySkuImageLabel = nullptr;
    QLabel *m_historyPartNameValueLabel = nullptr;
    QLabel *m_historyPartNumberValueLabel = nullptr;
    QLabel *m_historyBarcodeValueLabel = nullptr;
    QLabel *m_historySerialValueLabel = nullptr;
    QLabel *m_historyDateValueLabel = nullptr;
    QLabel *m_historyQuarterValueLabel = nullptr;
    QLabel *m_historyYearValueLabel = nullptr;

    QComboBox *m_barcodeSkuCombo = nullptr;
    QComboBox *m_barcodePrefixCombo = nullptr;
    QLineEdit *m_barcodeQuantityField = nullptr;
    QLineEdit *m_barcodeNextSerialField = nullptr;
    QLineEdit *m_barcodeValueField = nullptr;
    QComboBox *m_barcodeQuarterCombo = nullptr;
    QComboBox *m_barcodeYearCombo = nullptr;
    QLabel *m_barcodePartNameValueLabel = nullptr;
    QLabel *m_barcodePartNumberValueLabel = nullptr;
    QLabel *m_barcodeQuantityWordsValueLabel = nullptr;
    QLabel *m_barcodeSkuImageLabel = nullptr;
    QLabel *m_barcodePreview = nullptr;
    QPushButton *m_generateBarcodeButton = nullptr;
    QPushButton *m_clearBarcodeFieldsButton = nullptr;
    QPushButton *m_printBarcodeButton = nullptr;
    QPushButton *m_deleteBarcodeButton = nullptr;
    QTableView *m_barcodeTableView = nullptr;
    QStandardItemModel *m_barcodeModel = nullptr;
    QImage m_barcodeImage;
    QStringList m_lastGeneratedBarcodes;
    QString m_customDbPath;
    QString m_lastDatabaseOpenError;
    QString m_darkStyleSheet;
    QString m_lightStyleSheet;
    Theme m_currentTheme = Theme::Dark;
    PrintSettings m_printSettings;

    QByteArray m_imageBytes;
    QString m_legacyImagePath;

    QAction *m_actionLoadDb = nullptr;
    QAction *m_actionExportDb = nullptr;
    QAction *m_actionSaveDb = nullptr;
    QAction *m_actionExportSkuCsv = nullptr;
    QAction *m_actionExportBarcodeSummaryCsv = nullptr;
    QAction *m_actionExit = nullptr;
    QAction *m_actionFullScreen = nullptr;
    QAction *m_actionThemeDark = nullptr;
    QAction *m_actionThemeLight = nullptr;
    QAction *m_actionThemeSystem = nullptr;
    QAction *m_actionPrintSettings = nullptr;
    QAction *m_actionManageUsers = nullptr;
    QAction *m_actionSwitchUser = nullptr;
    QAction *m_actionSettings = nullptr;
    QAction *m_actionHelpGuides = nullptr;

    int m_selectedId = 0;
    QString m_currentUsername;
    QString m_currentUserId;
    QString m_currentUserFullName;
    QString m_currentRoleKey;
    UserRole m_currentBaseRole = UserRole::FullAccess;
    AccessPolicy m_access;
    bool m_relaunchingElevated = false;
    QTimer *m_autoBackupTimer = nullptr;
};

#endif // MAINWINDOW_H
