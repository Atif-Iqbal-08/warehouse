#include "mainwindow.h"
#include "globals.h"

#include <QComboBox>
#include <QDateEdit>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QModelIndex>
#include <QSet>
#include <QSpinBox>
#include <QSqlQuery>
#include <QSqlError>
#include <QStandardItemModel>
#include <QTableView>
#include <QSignalBlocker>
#include <QTime>
#include <QStringList>
#include <algorithm>
#include <functional>

void MainWindow::loadHistorySkuList(const QString &filter, bool preserveText) {
    if (!m_historySkuCombo) {
        appendRunLogWithUser("loadHistorySkuList: m_historySkuCombo is null");
        return;
    }

    const QString trimmed = filter.trimmed();
    QString currentText;
    int cursorPos = -1;
    QLineEdit *edit = m_historySkuCombo->lineEdit();
    if (preserveText && edit) {
        currentText = edit->text();
        cursorPos = edit->cursorPosition();
    }

    QSignalBlocker blocker(m_historySkuCombo);
    const bool editSignalsBlocked = edit ? edit->blockSignals(true) : false;
    m_historySkuCombo->clear();

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
            m_historySkuCombo->addItem(display, sku);
        }
    }

    if (preserveText) {
        m_historySkuCombo->setCurrentIndex(-1);
        if (edit) {
            edit->setText(currentText);
            if (cursorPos >= 0) {
                edit->setCursorPosition(cursorPos);
            }
        }
    } else {
        if (m_historySkuCombo->count() > 0) {
            m_historySkuCombo->setCurrentIndex(0);
        } else {
            m_historySkuCombo->setCurrentIndex(-1);
        }
        loadHistoryForSelectedSku();
    }

    if (edit) {
        edit->blockSignals(editSignalsBlocked);
    }
}

void MainWindow::loadHistoryForSelectedSku() {
    const QString sku = extractSkuFromDisplay(m_historySkuCombo->currentText());
    updateHistorySkuDetails();
    loadBarcodeHistory(sku);
}

void MainWindow::loadBarcodeHistory(const QString &sku) {
    if (!m_historyModel || !m_historyTableView) {
        appendRunLogWithUser("loadBarcodeHistory: model/view null");
        return;
    }
    QList<QStringList> rows;
    const QString cleanedSku = extractSkuFromDisplay(sku);
    const QString serialFilter = m_historySerialSearchField ? m_historySerialSearchField->text().trimmed() : QString();

    QStringList clauses;
    if (!cleanedSku.isEmpty()) {
        clauses << "b.sku = ?";
    }
    if (!serialFilter.isEmpty()) {
        clauses << "CAST(b.serial AS TEXT) LIKE ?";
    }

    QString sql =
        "SELECT b.sku, s.part_name, b.serial, b.created_at, b.barcode, b.quarter, b.year "
        "FROM barcode_log_active b "
        "LEFT JOIN sku_catalog_active s ON s.sku = b.sku";
    if (!clauses.isEmpty()) {
        sql += " WHERE " + clauses.join(" AND ");
    }
    sql += " ORDER BY datetime(b.created_at) DESC, b.serial DESC";

    QSqlQuery q(m_db);
    q.prepare(sql);
    if (!cleanedSku.isEmpty()) {
        q.addBindValue(cleanedSku);
    }
    if (!serialFilter.isEmpty()) {
        q.addBindValue("%" + serialFilter + "%");
    }

    if (q.exec()) {
        while (q.next()) {
            rows.append({
                q.value(0).toString(),
                q.value(1).toString(),
                q.value(2).toString(),
                q.value(3).toString(),
                q.value(4).toString(),
                QString("Q%1").arg(q.value(5).toInt()),
                q.value(6).toString()
            });
        }
    }

    populateHistoryModel(rows);
}

void MainWindow::populateHistoryModel(const QList<QStringList> &rows) {
    m_historyModel->clear();
    const QStringList headers = { "SKU", "Part Name", "Serial", "Generated", "QR Code", "Quarter", "Year" };
    m_historyModel->setColumnCount(headers.size());
    m_historyModel->setHorizontalHeaderLabels(headers);

    for (const auto &row : rows) {
        QList<QStandardItem *> items;
        items.reserve(headers.size());
        for (int i = 0; i < headers.size(); ++i) {
            items.append(new QStandardItem(row.value(i)));
        }
        m_historyModel->appendRow(items);
    }

    m_historyTableView->resizeColumnsToContents();
    if (m_historyTableView) {
        m_historyTableView->setColumnHidden(4, true);
        m_historyTableView->setColumnHidden(5, true);
        m_historyTableView->setColumnHidden(6, true);
    }
    if (!rows.isEmpty()) {
        m_historyTableView->selectRow(0);
    } else {
        updateHistoryBarcodeDetails();
    }
}

void MainWindow::onHistorySkuInputChanged(const QString &text) {
    loadHistorySkuList(text, true);
}

void MainWindow::onHistorySkuChanged(int) {
    loadHistoryForSelectedSku();
}

void MainWindow::onHistorySerialSearchChanged(const QString &text) {
    Q_UNUSED(text);
    const QString sku = extractSkuFromDisplay(m_historySkuCombo->currentText());
    loadBarcodeHistory(sku);
}

void MainWindow::updateHistorySkuDetails() {
    const QString sku = extractSkuFromDisplay(m_historySkuCombo->currentText());
    if (sku.isEmpty()) {
        m_historyPartNameValueLabel->setText("-");
        m_historyPartNumberValueLabel->setText("-");
        loadHistorySkuImage(QByteArray(), QString());
        return;
    }

    QSqlQuery q(m_db);
    q.prepare("SELECT part_name, part_number, image_blob, image_path FROM sku_catalog_active WHERE sku = ?");
    q.addBindValue(sku);
    if (q.exec() && q.next()) {
        m_historyPartNameValueLabel->setText(q.value(0).toString());
        m_historyPartNumberValueLabel->setText(q.value(1).toString());
        loadHistorySkuImage(q.value(2).toByteArray(), q.value(3).toString());
    } else {
        m_historyPartNameValueLabel->setText("-");
        m_historyPartNumberValueLabel->setText("-");
        loadHistorySkuImage(QByteArray(), QString());
    }
}

void MainWindow::loadHistorySkuImage(const QByteArray &data, const QString &legacyPath) {
    loadImageLabelFromData(m_historySkuImageLabel, data, legacyPath, AppGlobals::noImageText());
}

void MainWindow::updateHistoryBarcodeDetails() {
    auto clearLabels = [this]() {
        m_historyBarcodeValueLabel->setText("-");
        m_historySerialValueLabel->setText("-");
        m_historyDateValueLabel->setText("-");
        m_historyQuarterValueLabel->setText("-");
        m_historyYearValueLabel->setText("-");
    };

    if (!m_historyTableView || !m_historyModel || m_historyModel->rowCount() == 0) {
        clearLabels();
        return;
    }

    const QModelIndex current = m_historyTableView->currentIndex();
    if (!current.isValid()) {
        clearLabels();
        return;
    }

    const int row = current.row();
    m_historyBarcodeValueLabel->setText(m_historyModel->item(row, 4)->text());
    m_historySerialValueLabel->setText(m_historyModel->item(row, 2)->text());
    m_historyDateValueLabel->setText(m_historyModel->item(row, 3)->text());
    m_historyQuarterValueLabel->setText(m_historyModel->item(row, 5)->text());
    m_historyYearValueLabel->setText(m_historyModel->item(row, 6)->text());
}

void MainWindow::onHistoryTableSelectionChanged(const QModelIndex &current, const QModelIndex &previous) {
    Q_UNUSED(current);
    Q_UNUSED(previous);
    updateHistoryBarcodeDetails();
}

void MainWindow::editSelectedHistoryBarcode() {
    if (!requireAccess(m_access.canSerialEdit, "You don't have permission to edit or reassign serial numbers.")) {
        return;
    }

    const QModelIndex current = m_historyTableView->currentIndex();
    if (!current.isValid()) {
        setStatus("Select a QR code row to edit.", false);
        return;
    }

    const int row = current.row();
    const QString sku = m_historyModel->item(row, 0)->text().trimmed().toUpper();
    if (sku.isEmpty()) {
        setStatus("Select an SKU to edit serials.", false);
        return;
    }
    const QString createdAt = m_historyModel->item(row, 3)->text();
    const int serial = m_historyModel->item(row, 2)->text().toInt();
    const QString oldBarcode = m_historyModel->item(row, 4)->text();
    const QString barcodePrefix = barcodePrefixFromValue(oldBarcode);
    const QString quarterText = m_historyModel->item(row, 5)->text();
    const int quarter = quarterText.startsWith('Q') ? quarterText.mid(1).toInt() : quarterText.toInt();
    const int year = m_historyModel->item(row, 6)->text().toInt();

    QDialog dialog(this);
    dialog.setWindowTitle("Edit QR Code");
    QFormLayout *formLayout = new QFormLayout(&dialog);

    QLabel *skuLabel = new QLabel(sku, &dialog);
    formLayout->addRow("SKU", skuLabel);

    QSpinBox *serialSpin = new QSpinBox(&dialog);
    serialSpin->setRange(1, 99999);
    serialSpin->setValue(serial);
    formLayout->addRow("Serial", serialSpin);

    QSpinBox *quarterSpin = new QSpinBox(&dialog);
    quarterSpin->setRange(1, 4);
    quarterSpin->setValue(quarter > 0 ? quarter : currentQuarter());
    formLayout->addRow("Quarter", quarterSpin);

    QSpinBox *yearSpin = new QSpinBox(&dialog);
    yearSpin->setRange(2000, 2100);
    yearSpin->setValue(year > 0 ? year : currentYear());
    formLayout->addRow("Year", yearSpin);

    QDateTime parsed = QDateTime::fromString(createdAt, Qt::ISODate);
    if (!parsed.isValid()) {
        parsed = QDateTime::currentDateTime();
    }
    QDateEdit *dateEdit = new QDateEdit(parsed.date(), &dialog);
    dateEdit->setCalendarPopup(true);
    formLayout->addRow("Date", dateEdit);

    QLineEdit *barcodeField = new QLineEdit(&dialog);
    barcodeField->setReadOnly(true);
    formLayout->addRow("QR Code", barcodeField);

    const auto updateBarcodeField = [&]() {
        const QString value = buildBarcodeValue(sku, serialSpin->value(), yearSpin->value(), quarterSpin->value(), barcodePrefix);
        barcodeField->setText(value);
    };
    updateBarcodeField();
    connect(serialSpin, qOverload<int>(&QSpinBox::valueChanged), &dialog, updateBarcodeField);
    connect(quarterSpin, qOverload<int>(&QSpinBox::valueChanged), &dialog, updateBarcodeField);
    connect(yearSpin, qOverload<int>(&QSpinBox::valueChanged), &dialog, updateBarcodeField);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    formLayout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const int newSerial = serialSpin->value();
    const int newQuarter = quarterSpin->value();
    const int newYear = yearSpin->value();
    const QString newBarcode = barcodeField->text().trimmed();
    const QDateTime newDateTime(dateEdit->date(), parsed.time().isValid() ? parsed.time() : QTime::currentTime());
    const QString newCreatedAt = newDateTime.toString(Qt::ISODate);

    if (newBarcode.isEmpty()) {
        setStatus("QR code value is empty.", false);
        return;
    }

    if (newBarcode != oldBarcode) {
        QSqlQuery dup(m_db);
        dup.prepare("SELECT COUNT(*) FROM barcode_log_active WHERE sku = ? AND barcode = ?");
        dup.addBindValue(sku);
        dup.addBindValue(newBarcode);
        if (!dup.exec() || !dup.next()) {
            setStatus("Failed to validate QR code.", false);
            return;
        }
        if (dup.value(0).toInt() > 0) {
            setStatus("QR code already exists for this SKU.", false);
            return;
        }
    }

    QString comment;
    if (!promptAdminAuthorization("Edit Serial", &comment, true)) {
        return;
    }

    m_db.transaction();
    QSqlQuery update(m_db);
    update.prepare("UPDATE barcode_log SET serial = ?, quarter = ?, year = ?, barcode = ?, created_at = ? "
                   "WHERE sku = ? AND barcode = ? AND COALESCE(is_deleted, 0) = 0");
    update.addBindValue(newSerial);
    update.addBindValue(newQuarter);
    update.addBindValue(newYear);
    update.addBindValue(newBarcode);
    update.addBindValue(newCreatedAt);
    update.addBindValue(sku);
    update.addBindValue(oldBarcode);

    if (!update.exec()) {
        m_db.rollback();
        setStatus("Failed to update QR code.", false);
        logAction("SERIAL_EDIT",
                  sku,
                  oldBarcode,
                  QString(),
                  "Failed to update QR code.",
                  "History",
                  "EDIT",
                  false,
                  update.lastError().text(),
                  oldBarcode);
        return;
    }

    if (year != newYear || quarter != newQuarter) {
        refreshBarcodeSerialTracker(sku, year, quarter);
    }
    refreshBarcodeSerialTracker(sku, newYear, newQuarter);

    m_db.commit();

    QJsonObject oldValue;
    oldValue.insert("sku", sku);
    oldValue.insert("barcode", oldBarcode);
    oldValue.insert("serial", serial);
    oldValue.insert("quarter", quarter);
    oldValue.insert("year", year);
    oldValue.insert("created_at", createdAt);

    QJsonObject newValue;
    newValue.insert("sku", sku);
    newValue.insert("barcode", newBarcode);
    newValue.insert("serial", newSerial);
    newValue.insert("quarter", newQuarter);
    newValue.insert("year", newYear);
    newValue.insert("created_at", newCreatedAt);

    logAction("SERIAL_EDIT",
              sku,
              QString::fromUtf8(QJsonDocument(oldValue).toJson(QJsonDocument::Compact)),
              QString::fromUtf8(QJsonDocument(newValue).toJson(QJsonDocument::Compact)),
              comment,
              "History",
              "EDIT",
              true,
              QString(),
              oldBarcode);

    loadHistoryForSelectedSku();
    if (extractSkuFromDisplay(m_barcodeSkuCombo->currentText()) == sku) {
        updateNextBarcodeSerial();
    }
    setStatus("QR code updated successfully.", true);
}

void MainWindow::deleteSelectedHistoryBarcodes() {
    if (!requireAccess(m_access.canSerialDelete, "You don't have permission to delete serial numbers.")) {
        return;
    }

    const QModelIndexList rows = m_historyTableView->selectionModel()->selectedRows();
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
        const QString barcode = m_historyModel->item(rowIndex.row(), 4)->text();
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
                      "History",
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
                      "History",
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

    QList<int> rowNumbers;
    for (const QModelIndex &rowIndex : rows) {
        rowNumbers.append(rowIndex.row());
    }
    std::sort(rowNumbers.begin(), rowNumbers.end(), std::greater<int>());
    for (int row : rowNumbers) {
        m_historyModel->removeRow(row);
    }

    setStatus("Selected serials deleted.", true);
    updateDashboardMetrics();
    updateNextBarcodeSerial();
    loadHistoryForSelectedSku();
    const QString currentSku = extractSkuFromDisplay(m_barcodeSkuCombo->currentText());
    if (!currentSku.isEmpty() && affectedSkus.contains(currentSku)) {
        updateQuantityWordsLabels(currentSku);
    }
}

void MainWindow::clearHistoryFields() {
    if (m_historySkuCombo) {
        QSignalBlocker blocker(m_historySkuCombo);
        m_historySkuCombo->setCurrentIndex(-1);
        if (m_historySkuCombo->lineEdit()) {
            m_historySkuCombo->lineEdit()->clear();
        }
    }
    if (m_historySerialSearchField) {
        m_historySerialSearchField->clear();
    }
    populateHistoryModel({});
    updateHistorySkuDetails();
    setStatus("History fields cleared.", true);
}
