#include "printsettingsdialog.h"

#include <QSettings>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QPainter>
#include <QPen>
#include <QColor>

void PrintSettings::load() {
    QSettings settings;
    labelWidthMm = kLabelWidthMm;
    labelHeightMm = kLabelHeightMm;
    barcodeWidthMm = kQrWidthMm;
    barcodeHeightMm = kQrHeightMm;
    innerMarginMm = settings.value("print/innerMarginMm", innerMarginMm).toDouble();
    if (innerMarginMm < 2.0) {
        innerMarginMm = 2.0;
    }
}

void PrintSettings::save() const {
    QSettings settings;
    settings.setValue("print/innerMarginMm", innerMarginMm);
}

PrintSettingsDialog::PrintSettingsDialog(const PrintSettings &settings,
                                         std::function<QImage(const QString &)> renderer,
                                         QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
    , m_renderer(std::move(renderer)) {
    setWindowTitle("QR Code Print Settings");
    setModal(true);

    QVBoxLayout *layout = new QVBoxLayout(this);
    QFormLayout *form = new QFormLayout();

    m_labelWidth = new QDoubleSpinBox(this);
    m_labelWidth->setRange(10.0, 200.0);
    m_labelWidth->setDecimals(1);
    m_labelWidth->setSingleStep(0.5);
    m_labelWidth->setValue(m_settings.labelWidthMm);
    m_labelWidth->setEnabled(false);
    m_labelWidth->setToolTip("Sticker width is fixed at 50 mm.");

    m_labelHeight = new QDoubleSpinBox(this);
    m_labelHeight->setRange(10.0, 200.0);
    m_labelHeight->setDecimals(1);
    m_labelHeight->setSingleStep(0.5);
    m_labelHeight->setValue(m_settings.labelHeightMm);
    m_labelHeight->setEnabled(false);
    m_labelHeight->setToolTip("Sticker height is fixed at 30 mm.");

    m_barcodeWidth = new QDoubleSpinBox(this);
    m_barcodeWidth->setRange(10.0, 200.0);
    m_barcodeWidth->setDecimals(1);
    m_barcodeWidth->setSingleStep(0.5);
    m_barcodeWidth->setValue(m_settings.barcodeWidthMm);
    m_barcodeWidth->setEnabled(false);
    m_barcodeWidth->setToolTip("QR code width is fixed at 19 mm.");

    m_barcodeHeight = new QDoubleSpinBox(this);
    m_barcodeHeight->setRange(4.0, 80.0);
    m_barcodeHeight->setDecimals(1);
    m_barcodeHeight->setSingleStep(0.5);
    m_barcodeHeight->setValue(m_settings.barcodeHeightMm);
    m_barcodeHeight->setEnabled(false);
    m_barcodeHeight->setToolTip("QR code height is fixed at 19 mm.");

    m_innerMargin = new QDoubleSpinBox(this);
    m_innerMargin->setRange(0.0, 10.0);
    m_innerMargin->setDecimals(1);
    m_innerMargin->setSingleStep(0.5);
    m_innerMargin->setValue(m_settings.innerMarginMm);

    form->addRow("Sticker Width (mm)", m_labelWidth);
    form->addRow("Sticker Height (mm)", m_labelHeight);
    form->addRow("QR Code Width (mm)", m_barcodeWidth);
    form->addRow("QR Code Height (mm)", m_barcodeHeight);
    form->addRow("Inner Margin (mm)", m_innerMargin);

    m_previewLabel = new QLabel(this);
    m_previewLabel->setFixedSize(360, 220);
    m_previewLabel->setFrameShape(QFrame::StyledPanel);
    m_previewLabel->setAlignment(Qt::AlignCenter);

    QPushButton *resetButton = new QPushButton("Reset Defaults", this);
    connect(resetButton, &QPushButton::clicked, this, &PrintSettingsDialog::resetDefaults);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    layout->addLayout(form);
    layout->addWidget(m_previewLabel);
    layout->addWidget(resetButton);
    layout->addWidget(buttons);

    connect(m_labelWidth, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &PrintSettingsDialog::updatePreview);
    connect(m_labelHeight, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &PrintSettingsDialog::updatePreview);
    connect(m_barcodeWidth, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &PrintSettingsDialog::updatePreview);
    connect(m_barcodeHeight, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &PrintSettingsDialog::updatePreview);
    connect(m_innerMargin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &PrintSettingsDialog::updatePreview);

    updatePreview();
}

PrintSettings PrintSettingsDialog::settings() const {
    PrintSettings settings = m_settings;
    settings.labelWidthMm = m_labelWidth->value();
    settings.labelHeightMm = m_labelHeight->value();
    settings.barcodeWidthMm = m_barcodeWidth->value();
    settings.barcodeHeightMm = m_barcodeHeight->value();
    settings.innerMarginMm = m_innerMargin->value();
    return settings;
}

bool PrintSettingsDialog::edit(QWidget *parent,
                               PrintSettings &settings,
                               std::function<QImage(const QString &)> renderer) {
    PrintSettingsDialog dialog(settings, std::move(renderer), parent);
    if (dialog.exec() == QDialog::Accepted) {
        settings = dialog.settings();
        return true;
    }
    return false;
}

void PrintSettingsDialog::resetDefaults() {
    m_labelWidth->setValue(PrintSettings::kLabelWidthMm);
    m_labelHeight->setValue(PrintSettings::kLabelHeightMm);
    m_barcodeWidth->setValue(PrintSettings::kQrWidthMm);
    m_barcodeHeight->setValue(PrintSettings::kQrHeightMm);
    m_innerMargin->setValue(2.0);
}

void PrintSettingsDialog::updatePreview() {
    if (!m_previewLabel) {
        return;
    }

    const int w = m_previewLabel->width();
    const int h = m_previewLabel->height();
    QImage preview(w, h, QImage::Format_ARGB32_Premultiplied);
    preview.fill(Qt::white);

    QPainter painter(&preview);
    painter.setRenderHint(QPainter::Antialiasing);

    const qreal labelWmm = m_labelWidth->value();
    const qreal labelHmm = m_labelHeight->value();
    const qreal marginPx = 10.0;
    const qreal scale = qMin((w - marginPx * 2.0) / labelWmm, (h - marginPx * 2.0) / labelHmm);
    const qreal labelWpx = labelWmm * scale;
    const qreal labelHpx = labelHmm * scale;

    QRectF labelRect((w - labelWpx) / 2.0, (h - labelHpx) / 2.0, labelWpx, labelHpx);
    painter.setPen(QPen(QColor("#666666")));
    painter.drawRect(labelRect);

    const qreal edgeMarginMm = 2.0;
    const qreal contentLeftMm = edgeMarginMm;
    const qreal contentTopMm = edgeMarginMm;
    const qreal contentRightMm = labelWmm - edgeMarginMm;
    const qreal contentBottomMm = labelHmm - edgeMarginMm;

    const qreal qrWpx = m_barcodeWidth->value() * scale;
    const qreal qrHpx = m_barcodeHeight->value() * scale;
    const qreal qrXmm = qMax(contentLeftMm, contentRightMm - m_barcodeWidth->value());
    const qreal qrYmm = qMax(contentTopMm, contentBottomMm - m_barcodeHeight->value());
    const qreal qrXpx = labelRect.left() + qrXmm * scale;
    const qreal qrYpx = labelRect.top() + qrYmm * scale;
    QRectF qrRect(qrXpx, qrYpx, qrWpx, qrHpx);

    QImage qr;
    if (m_renderer) {
        qr = m_renderer("SAMPLE123");
    }
    if (!qr.isNull()) {
        QImage scaled = qr.scaled(qrRect.size().toSize(), Qt::KeepAspectRatio, Qt::FastTransformation);
        const qreal bx = qrRect.left() + (qrRect.width() - scaled.width()) / 2.0;
        const qreal by = qrRect.top() + (qrRect.height() - scaled.height()) / 2.0;
        painter.drawImage(QPointF(bx, by), scaled);
    }

    QImage logo(":/assets/sticker_logo.png");
    if (!logo.isNull()) {
        const qreal logoSizeMm = 5.0;
        const qreal logoMarginMm = 0.0;
        const qreal logoWpx = logoSizeMm * scale;
        const qreal logoHpx = logoSizeMm * scale;
        const qreal logoXmm = contentRightMm - logoMarginMm - logoSizeMm;
        const qreal logoYmm = contentTopMm + logoMarginMm;
        QRectF logoRect(labelRect.left() + logoXmm * scale,
                        labelRect.top() + logoYmm * scale,
                        logoWpx,
                        logoHpx);
        QImage logoScaled = logo.scaled(logoRect.size().toSize(),
                                        Qt::KeepAspectRatio,
                                        Qt::SmoothTransformation);
        const qreal bx = logoRect.left() + (logoRect.width() - logoScaled.width()) / 2.0;
        const qreal by = logoRect.top() + (logoRect.height() - logoScaled.height()) / 2.0;
        painter.drawImage(QPointF(bx, by), logoScaled);
    }

    m_previewLabel->setPixmap(QPixmap::fromImage(preview));
}
