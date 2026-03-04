#include "printsettingsdialog.h"

#include <QColor>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFontMetrics>
#include <QFormLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace {
qreal clampValue(qreal value, qreal minValue, qreal maxValue) {
    return qMax(minValue, qMin(value, maxValue));
}

QString normalizedPrefix(const QString &value) {
    const QString prefix = value.trimmed().left(2).toUpper();
    if (prefix == "SD" || prefix == "SK" || prefix == "SM") {
        return prefix;
    }
    return "SK";
}

QString websiteForPrefix(const QString &prefix) {
    const QString normalized = normalizedPrefix(prefix);
    if (normalized == "SD") {
        return "www.skylarkdrones.com";
    }
    if (normalized == "SK") {
        return "www.skykart.in";
    }
    return "Skylark Drones Manufacturing\nPrivate Limitd";
}
} // namespace

void PrintSettings::load() {
    QSettings settings;
    labelWidthMm = clampValue(settings.value("print/labelWidthMm", kLabelWidthMm).toDouble(), 10.0, 200.0);
    labelHeightMm = clampValue(settings.value("print/labelHeightMm", kLabelHeightMm).toDouble(), 10.0, 200.0);
    barcodeWidthMm = clampValue(settings.value("print/barcodeWidthMm", kQrWidthMm).toDouble(), 4.0, 80.0);
    barcodeHeightMm = clampValue(settings.value("print/barcodeHeightMm", kQrHeightMm).toDouble(), 4.0, 80.0);
    edgeMarginMm = clampValue(settings.value("print/edgeMarginMm", kEdgeMarginMm).toDouble(), 0.0, 10.0);
    innerMarginMm = clampValue(settings.value("print/innerMarginMm", kInnerMarginMm).toDouble(), 0.0, 20.0);
    logoSizeMm = clampValue(settings.value("print/logoSizeMm", kLogoSizeMm).toDouble(), 0.0, 20.0);
    logoPosXmm = settings.contains("print/logoPosXmm")
                     ? settings.value("print/logoPosXmm", kAutoLogoPosMm).toDouble()
                     : kAutoLogoPosMm;
    logoPosYmm = settings.contains("print/logoPosYmm")
                     ? settings.value("print/logoPosYmm", kAutoLogoPosMm).toDouble()
                     : kAutoLogoPosMm;
    partNameFontSizePt = kPartNameFontSizePt;
    detailFontSizePt = kDetailFontSizePt;
    fontFamily = QString::fromLatin1(kDefaultFontFamily);
}

void PrintSettings::save() const {
    QSettings settings;
    settings.setValue("print/labelWidthMm", labelWidthMm);
    settings.setValue("print/labelHeightMm", labelHeightMm);
    settings.setValue("print/barcodeWidthMm", barcodeWidthMm);
    settings.setValue("print/barcodeHeightMm", barcodeHeightMm);
    settings.setValue("print/edgeMarginMm", edgeMarginMm);
    settings.setValue("print/innerMarginMm", innerMarginMm);
    settings.setValue("print/logoSizeMm", logoSizeMm);
    settings.setValue("print/logoPosXmm", logoPosXmm);
    settings.setValue("print/logoPosYmm", logoPosYmm);
    settings.remove("print/partNameFontSizePt");
    settings.remove("print/detailFontSizePt");
    settings.remove("print/fontFamily");
}

PrintSettingsDialog::PrintSettingsDialog(const PrintSettings &settings,
                                         const QString &previewPrefix,
                                         std::function<QImage(const QString &)> renderer,
                                         std::function<QImage(const QString &)> logoResolver,
                                         QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
    , m_renderer(std::move(renderer))
    , m_logoResolver(std::move(logoResolver)) {
    setWindowTitle("QR Code Print Settings");
    setModal(true);
    setMinimumWidth(560);

    QVBoxLayout *layout = new QVBoxLayout(this);
    QFormLayout *form = new QFormLayout();

    m_previewPrefixCombo = new QComboBox(this);
    m_previewPrefixCombo->addItem("SD - Skylark Drones", "SD");
    m_previewPrefixCombo->addItem("SK - Skykart", "SK");
    m_previewPrefixCombo->addItem("SM - SDMPL", "SM");
    int previewPrefixIndex = m_previewPrefixCombo->findData(normalizedPrefix(previewPrefix));
    if (previewPrefixIndex < 0) {
        previewPrefixIndex = m_previewPrefixCombo->findData("SK");
    }
    if (previewPrefixIndex >= 0) {
        m_previewPrefixCombo->setCurrentIndex(previewPrefixIndex);
    }

    m_labelWidth = new QDoubleSpinBox(this);
    m_labelWidth->setRange(10.0, 200.0);
    m_labelWidth->setDecimals(1);
    m_labelWidth->setSingleStep(0.5);
    m_labelWidth->setValue(m_settings.labelWidthMm);

    m_labelHeight = new QDoubleSpinBox(this);
    m_labelHeight->setRange(10.0, 200.0);
    m_labelHeight->setDecimals(1);
    m_labelHeight->setSingleStep(0.5);
    m_labelHeight->setValue(m_settings.labelHeightMm);

    m_barcodeWidth = new QDoubleSpinBox(this);
    m_barcodeWidth->setRange(4.0, 80.0);
    m_barcodeWidth->setDecimals(1);
    m_barcodeWidth->setSingleStep(0.5);
    m_barcodeWidth->setValue(m_settings.barcodeWidthMm);

    m_barcodeHeight = new QDoubleSpinBox(this);
    m_barcodeHeight->setRange(4.0, 80.0);
    m_barcodeHeight->setDecimals(1);
    m_barcodeHeight->setSingleStep(0.5);
    m_barcodeHeight->setValue(m_settings.barcodeHeightMm);

    m_edgeMargin = new QDoubleSpinBox(this);
    m_edgeMargin->setRange(0.0, 10.0);
    m_edgeMargin->setDecimals(1);
    m_edgeMargin->setSingleStep(0.5);
    m_edgeMargin->setValue(m_settings.edgeMarginMm);

    m_innerMargin = new QDoubleSpinBox(this);
    m_innerMargin->setRange(0.0, 20.0);
    m_innerMargin->setDecimals(1);
    m_innerMargin->setSingleStep(0.5);
    m_innerMargin->setValue(m_settings.innerMarginMm);

    m_logoSize = new QDoubleSpinBox(this);
    m_logoSize->setRange(0.0, 20.0);
    m_logoSize->setDecimals(1);
    m_logoSize->setSingleStep(0.5);
    m_logoSize->setValue(m_settings.logoSizeMm);

    form->addRow("Preview Prefix", m_previewPrefixCombo);
    form->addRow("Sticker Width (mm)", m_labelWidth);
    form->addRow("Sticker Height (mm)", m_labelHeight);
    form->addRow("QR Code Width (mm)", m_barcodeWidth);
    form->addRow("QR Code Height (mm)", m_barcodeHeight);
    form->addRow("Outer Margin (mm)", m_edgeMargin);
    form->addRow("Text Left Margin (mm)", m_innerMargin);
    form->addRow("Logo Size (mm)", m_logoSize);

    m_previewLabel = new QLabel(this);
    m_previewLabel->setFixedSize(420, 250);
    m_previewLabel->setFrameShape(QFrame::StyledPanel);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setMouseTracking(true);
    m_previewLabel->installEventFilter(this);

    QPushButton *resetButton = new QPushButton("Reset Defaults", this);
    connect(resetButton, &QPushButton::clicked, this, &PrintSettingsDialog::resetDefaults);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    layout->addLayout(form);
    layout->addWidget(m_previewLabel);
    layout->addWidget(resetButton);
    layout->addWidget(buttons);

    connect(m_previewPrefixCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &PrintSettingsDialog::updatePreview);
    connect(m_labelWidth, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &PrintSettingsDialog::updatePreview);
    connect(m_labelHeight, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &PrintSettingsDialog::updatePreview);
    connect(m_barcodeWidth, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &PrintSettingsDialog::updatePreview);
    connect(m_barcodeHeight, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &PrintSettingsDialog::updatePreview);
    connect(m_edgeMargin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &PrintSettingsDialog::updatePreview);
    connect(m_innerMargin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &PrintSettingsDialog::updatePreview);
    connect(m_logoSize, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &PrintSettingsDialog::updatePreview);

    m_logoPosXmm = m_settings.logoPosXmm;
    m_logoPosYmm = m_settings.logoPosYmm;
    updatePreview();
}

PrintSettings PrintSettingsDialog::settings() const {
    PrintSettings settings = m_settings;
    settings.labelWidthMm = m_labelWidth->value();
    settings.labelHeightMm = m_labelHeight->value();
    settings.barcodeWidthMm = m_barcodeWidth->value();
    settings.barcodeHeightMm = m_barcodeHeight->value();
    settings.edgeMarginMm = m_edgeMargin->value();
    settings.innerMarginMm = m_innerMargin->value();
    settings.logoSizeMm = m_logoSize->value();
    settings.logoPosXmm = m_logoPosXmm;
    settings.logoPosYmm = m_logoPosYmm;
    settings.partNameFontSizePt = PrintSettings::kPartNameFontSizePt;
    settings.detailFontSizePt = PrintSettings::kDetailFontSizePt;
    settings.fontFamily = QString::fromLatin1(PrintSettings::kDefaultFontFamily);
    return settings;
}

bool PrintSettingsDialog::edit(QWidget *parent,
                               PrintSettings &settings,
                               const QString &previewPrefix,
                               std::function<QImage(const QString &)> renderer,
                               std::function<QImage(const QString &)> logoResolver) {
    PrintSettingsDialog dialog(settings, previewPrefix, std::move(renderer), std::move(logoResolver), parent);
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
    m_edgeMargin->setValue(PrintSettings::kEdgeMarginMm);
    m_innerMargin->setValue(PrintSettings::kInnerMarginMm);
    m_logoSize->setValue(PrintSettings::kLogoSizeMm);
    m_logoPosXmm = PrintSettings::kAutoLogoPosMm;
    m_logoPosYmm = PrintSettings::kAutoLogoPosMm;
    updatePreview();
}

bool PrintSettingsDialog::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_previewLabel) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton &&
                !m_lastLogoRectPx.isEmpty() &&
                m_lastLogoRectPx.contains(mouseEvent->position())) {
                m_draggingLogo = true;
                m_logoDragOffsetPx = mouseEvent->position() - m_lastLogoRectPx.topLeft();
                m_previewLabel->setCursor(Qt::ClosedHandCursor);
                return true;
            }
        } else if (event->type() == QEvent::MouseMove) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (m_draggingLogo && m_lastScalePxPerMm > 0.0 && m_lastLogoSizeMm > 0.0) {
                const QPointF topLeftPx = mouseEvent->position() - m_logoDragOffsetPx;
                qreal xMm = (topLeftPx.x() - m_lastLabelRectPx.left()) / m_lastScalePxPerMm;
                qreal yMm = (topLeftPx.y() - m_lastLabelRectPx.top()) / m_lastScalePxPerMm;
                const qreal minXmm = m_lastContentLeftMm;
                const qreal minYmm = m_lastContentTopMm;
                const qreal maxXmm = qMax(minXmm, m_lastContentRightMm - m_lastLogoSizeMm);
                const qreal maxYmm = qMax(minYmm, m_lastContentBottomMm - m_lastLogoSizeMm);
                m_logoPosXmm = clampValue(xMm, minXmm, maxXmm);
                m_logoPosYmm = clampValue(yMm, minYmm, maxYmm);
                updatePreview();
                return true;
            }
            if (!m_draggingLogo) {
                if (!m_lastLogoRectPx.isEmpty() && m_lastLogoRectPx.contains(mouseEvent->position())) {
                    m_previewLabel->setCursor(Qt::OpenHandCursor);
                } else {
                    m_previewLabel->setCursor(Qt::ArrowCursor);
                }
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton && m_draggingLogo) {
                m_draggingLogo = false;
                if (!m_lastLogoRectPx.isEmpty() && m_lastLogoRectPx.contains(mouseEvent->position())) {
                    m_previewLabel->setCursor(Qt::OpenHandCursor);
                } else {
                    m_previewLabel->setCursor(Qt::ArrowCursor);
                }
                return true;
            }
        } else if (event->type() == QEvent::Leave && !m_draggingLogo) {
            m_previewLabel->setCursor(Qt::ArrowCursor);
        }
    }
    return QDialog::eventFilter(watched, event);
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
    auto mmToPx = [&](qreal mm) {
        return mm * scale;
    };

    QRectF labelRect((w - labelWpx) / 2.0, (h - labelHpx) / 2.0, labelWpx, labelHpx);
    painter.setPen(QPen(QColor("#666666")));
    painter.drawRect(labelRect);

    const qreal edgeMarginMm = m_edgeMargin->value();
    const qreal contentLeftMm = qMax<qreal>(0.0, edgeMarginMm);
    const qreal contentTopMm = qMax<qreal>(0.0, edgeMarginMm);
    const qreal contentRightMm = qMax(contentLeftMm + 1.0, labelWmm - edgeMarginMm);
    const qreal contentBottomMm = qMax(contentTopMm + 1.0, labelHmm - edgeMarginMm);
    const qreal contentHeightMm = qMax(1.0, contentBottomMm - contentTopMm);

    const qreal qrWidthMm = m_barcodeWidth->value();
    const qreal qrHeightMm = m_barcodeHeight->value();
    const qreal qrXmm = qMax(contentLeftMm, contentRightMm - qrWidthMm);
    const qreal qrYmm = qMax(contentTopMm, contentBottomMm - qrHeightMm);
    const QRectF qrRect(labelRect.left() + mmToPx(qrXmm),
                        labelRect.top() + mmToPx(qrYmm),
                        mmToPx(qrWidthMm),
                        mmToPx(qrHeightMm));

    const qreal textLeftMm = qMax(m_innerMargin->value(), contentLeftMm);
    const qreal interBlockGapMm = edgeMarginMm;
    const qreal textRightMm = qMax(textLeftMm + 1.0, qrXmm - interBlockGapMm);
    const qreal textBlockWidthMm = qMax(1.0, textRightMm - textLeftMm);
    const qreal textTopMm = contentTopMm;
    const QString selectedPrefix = normalizedPrefix(m_previewPrefixCombo->currentData().toString());
    const QString codeLine = QString("%1SAMPLE123Q12600001").arg(selectedPrefix);
    const QString siteLine = websiteForPrefix(selectedPrefix);
    const int siteLineRows = qMax(1, siteLine.count('\n') + 1);
    const int partNameLines = 3;
    const int detailLines = 4 + siteLineRows;
    const qreal partFontScale = 1.0;
    const qreal partNameFontSizePt = PrintSettings::kPartNameFontSizePt;
    const qreal detailFontSizePt = PrintSettings::kDetailFontSizePt;
    const qreal ptToMm = 25.4 / 72.0;
    qreal textScale = 1.0;
    qreal partLineHeightMm = qMax<qreal>(1.0, partNameFontSizePt * partFontScale * ptToMm * 1.35);
    qreal detailLineHeightMm = qMax<qreal>(1.0, detailFontSizePt * ptToMm * 1.35);
    qreal partNameHeightMm = partLineHeightMm * partNameLines;
    qreal detailHeightMm = detailLineHeightMm * detailLines;
    const qreal totalTextHeightMm = partNameHeightMm + detailHeightMm;
    if (totalTextHeightMm > contentHeightMm && totalTextHeightMm > 0.0) {
        textScale = contentHeightMm / totalTextHeightMm;
        partLineHeightMm *= textScale;
        detailLineHeightMm *= textScale;
        partNameHeightMm = partLineHeightMm * partNameLines;
    }

    const QString family = QString::fromLatin1(PrintSettings::kDefaultFontFamily);
    QFont partFont(family);
    partFont.setBold(true);
    partFont.setPointSizeF(qMax<qreal>(1.0, partNameFontSizePt * partFontScale * textScale));
    QFont detailFont(family);
    detailFont.setBold(false);
    detailFont.setPointSizeF(qMax<qreal>(1.0, detailFontSizePt * textScale));

    painter.setPen(Qt::black);
    const qreal textXpx = labelRect.left() + mmToPx(textLeftMm);
    const qreal textWidthPx = mmToPx(textBlockWidthMm);

    auto drawTextBlock = [&](const QString &text, qreal yMm, qreal hMm, const QFont &baseFont, bool wrap, bool fitToWidth) {
        QFont useFont(baseFont);
        if (fitToWidth || wrap) {
            qreal sizePt = useFont.pointSizeF();
            if (sizePt <= 0.0) {
                sizePt = 10.0;
            }
            while (sizePt > 1.0) {
                QFontMetricsF fm(useFont);
                bool needsShrink = false;
                if (fitToWidth && fm.horizontalAdvance(text) > textWidthPx) {
                    needsShrink = true;
                }
                if (!needsShrink && wrap) {
                    const QRectF wrappedRect = fm.boundingRect(QRectF(0, 0, textWidthPx, mmToPx(hMm)),
                                                               Qt::AlignLeft | Qt::TextWordWrap,
                                                               text);
                    if (wrappedRect.height() > mmToPx(hMm)) {
                        needsShrink = true;
                    }
                }
                if (!needsShrink) {
                    break;
                }
                sizePt -= 0.25;
                useFont.setPointSizeF(sizePt);
            }
        }
        painter.setFont(useFont);
        QFontMetrics fm(useFont);
        const QRectF rect(textXpx, labelRect.top() + mmToPx(yMm), textWidthPx, mmToPx(hMm));
        const int flags = wrap ? (Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap)
                               : (Qt::AlignLeft | Qt::AlignVCenter);
        const QString textToDraw = wrap ? text
                                        : fm.elidedText(text, Qt::ElideRight, static_cast<int>(textWidthPx));
        painter.drawText(rect, flags, textToDraw);
    };

    drawTextBlock("Part Name: Demo Motor", textTopMm, partNameHeightMm, partFont, true, false);
    drawTextBlock("SKU: DEMO-1001", textTopMm + partNameHeightMm, detailLineHeightMm, detailFont, false, false);
    drawTextBlock("Rack Number: R-1", textTopMm + partNameHeightMm + detailLineHeightMm, detailLineHeightMm, detailFont, false, false);
    drawTextBlock("Bin Number: B-2", textTopMm + partNameHeightMm + detailLineHeightMm * 2, detailLineHeightMm, detailFont, false, false);
    drawTextBlock(codeLine, textTopMm + partNameHeightMm + detailLineHeightMm * 3, detailLineHeightMm, detailFont, false, true);
    drawTextBlock(siteLine,
                  textTopMm + partNameHeightMm + detailLineHeightMm * 4,
                  detailLineHeightMm * siteLineRows,
                  detailFont,
                  siteLineRows > 1,
                  siteLineRows == 1);

    QImage logo;
    if (m_logoResolver) {
        logo = m_logoResolver(selectedPrefix);
    }
    if (logo.isNull()) {
        logo.load(":/assets/sticker_logo.png");
    }
    const qreal logoSizeMm = m_logoSize->value();
    m_lastScalePxPerMm = scale;
    m_lastLabelRectPx = labelRect;
    m_lastContentLeftMm = contentLeftMm;
    m_lastContentTopMm = contentTopMm;
    m_lastContentRightMm = contentRightMm;
    m_lastContentBottomMm = contentBottomMm;
    m_lastLogoSizeMm = logoSizeMm;
    m_lastLogoRectPx = QRectF();
    if (!logo.isNull() && logoSizeMm > 0.0) {
        const qreal defaultLogoXmm = contentRightMm - logoSizeMm;
        const qreal defaultLogoYmm = contentTopMm;
        qreal logoXmm = m_logoPosXmm;
        qreal logoYmm = m_logoPosYmm;
        if (logoXmm < 0.0) {
            logoXmm = defaultLogoXmm;
        }
        if (logoYmm < 0.0) {
            logoYmm = defaultLogoYmm;
        }
        const qreal minLogoXmm = contentLeftMm;
        const qreal minLogoYmm = contentTopMm;
        const qreal maxLogoXmm = qMax(minLogoXmm, contentRightMm - logoSizeMm);
        const qreal maxLogoYmm = qMax(minLogoYmm, contentBottomMm - logoSizeMm);
        logoXmm = clampValue(logoXmm, minLogoXmm, maxLogoXmm);
        logoYmm = clampValue(logoYmm, minLogoYmm, maxLogoYmm);
        m_logoPosXmm = logoXmm;
        m_logoPosYmm = logoYmm;

        const QRectF logoRect(labelRect.left() + mmToPx(logoXmm),
                              labelRect.top() + mmToPx(logoYmm),
                              mmToPx(logoSizeMm),
                              mmToPx(logoSizeMm));
        m_lastLogoRectPx = logoRect;
        const QImage logoScaled = logo.scaled(logoRect.size().toSize(),
                                              Qt::KeepAspectRatio,
                                              Qt::SmoothTransformation);
        const qreal bx = logoRect.left() + (logoRect.width() - logoScaled.width()) / 2.0;
        const qreal by = logoRect.top() + (logoRect.height() - logoScaled.height()) / 2.0;
        painter.drawImage(QPointF(bx, by), logoScaled);
    }

    QImage qr;
    if (m_renderer) {
        qr = m_renderer(codeLine);
    }
    if (!qr.isNull()) {
        const QImage scaledQr = qr.scaled(qrRect.size().toSize(), Qt::KeepAspectRatio, Qt::FastTransformation);
        const qreal bx = qrRect.left() + (qrRect.width() - scaledQr.width()) / 2.0;
        const qreal by = qrRect.top() + (qrRect.height() - scaledQr.height()) / 2.0;
        painter.drawImage(QPointF(bx, by), scaledQr);
    }

    m_previewLabel->setPixmap(QPixmap::fromImage(preview));
}
