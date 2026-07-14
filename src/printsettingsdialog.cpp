#include "printsettingsdialog.h"

#include <QColor>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QRect>
#include <QScreen>
#include <QSettings>
#include <QVBoxLayout>

// ── file-local helpers ────────────────────────────────────────────────────────
namespace {

qreal clampValue(qreal value, qreal minValue, qreal maxValue) {
    return qMax(minValue, qMin(value, maxValue));
}

QString normalizedPrefix(const QString &value) {
    const QString prefix = value.trimmed().left(2).toUpper();
    if (prefix == "SD" || prefix == "SK" || prefix == "SM") return prefix;
    return "SK";
}

QString websiteForPrefix(const QString &prefix) {
    const QString n = normalizedPrefix(prefix);
    if (n == "SD") return "www.skylarkdrones.com";
    if (n == "SK") return "www.skykart.in";
    return "Skylark Drones Manufacturing\nPrivate Limited";
}

} // namespace

// ── PrintSettings ─────────────────────────────────────────────────────────────

void PrintSettings::load() {
    QSettings s;
    labelWidthMm   = clampValue(s.value("print/labelWidthMm",  kLabelWidthMm).toDouble(),  10.0, 200.0);
    labelHeightMm  = clampValue(s.value("print/labelHeightMm", kLabelHeightMm).toDouble(), 10.0, 200.0);
    barcodeWidthMm = clampValue(s.value("print/barcodeWidthMm", kQrWidthMm).toDouble(),    4.0,  80.0);
    barcodeHeightMm= clampValue(s.value("print/barcodeHeightMm",kQrHeightMm).toDouble(),   4.0,  80.0);
    edgeMarginMm   = clampValue(s.value("print/edgeMarginMm",  kEdgeMarginMm).toDouble(),  0.0,  10.0);
    innerMarginMm  = clampValue(s.value("print/innerMarginMm", kInnerMarginMm).toDouble(), 0.0,  20.0);
    logoSizeMm     = clampValue(s.value("print/logoSizeMm",    kLogoSizeMm).toDouble(),    0.0,  20.0);

    auto loadPos = [&](const QString &key) -> qreal {
        return s.contains(key) ? s.value(key).toDouble() : kAutoPosMm;
    };
    logoPosXmm      = loadPos("print/logoPosXmm");
    logoPosYmm      = loadPos("print/logoPosYmm");
    qrPosXmm        = loadPos("print/qrPosXmm");
    qrPosYmm        = loadPos("print/qrPosYmm");
    partNamePosXmm  = loadPos("print/partNamePosXmm");
    partNamePosYmm  = loadPos("print/partNamePosYmm");
    infoBlockPosXmm = loadPos("print/infoBlockPosXmm");
    infoBlockPosYmm = loadPos("print/infoBlockPosYmm");

    partNameFontSizePt = kPartNameFontSizePt;
    detailFontSizePt   = kDetailFontSizePt;
    fontFamily = QString::fromLatin1(kDefaultFontFamily);
}

void PrintSettings::save() const {
    QSettings s;
    s.setValue("print/labelWidthMm",   labelWidthMm);
    s.setValue("print/labelHeightMm",  labelHeightMm);
    s.setValue("print/barcodeWidthMm", barcodeWidthMm);
    s.setValue("print/barcodeHeightMm",barcodeHeightMm);
    s.setValue("print/edgeMarginMm",   edgeMarginMm);
    s.setValue("print/innerMarginMm",  innerMarginMm);
    s.setValue("print/logoSizeMm",     logoSizeMm);

    auto savePos = [&](const QString &key, qreal val) {
        if (val >= 0.0) s.setValue(key, val); else s.remove(key);
    };
    savePos("print/logoPosXmm",      logoPosXmm);
    savePos("print/logoPosYmm",      logoPosYmm);
    savePos("print/qrPosXmm",        qrPosXmm);
    savePos("print/qrPosYmm",        qrPosYmm);
    savePos("print/partNamePosXmm",  partNamePosXmm);
    savePos("print/partNamePosYmm",  partNamePosYmm);
    savePos("print/infoBlockPosXmm", infoBlockPosXmm);
    savePos("print/infoBlockPosYmm", infoBlockPosYmm);

    s.remove("print/partNameFontSizePt");
    s.remove("print/detailFontSizePt");
    s.remove("print/fontFamily");
}

// ── PrintSettingsDialog ───────────────────────────────────────────────────────

PrintSettingsDialog::PrintSettingsDialog(const PrintSettings &settings,
                                         const QString &previewPrefix,
                                         std::function<QImage(const QString &)> renderer,
                                         std::function<QImage(const QString &)> logoResolver,
                                         QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
    , m_renderer(std::move(renderer))
    , m_logoResolver(std::move(logoResolver)) {
    setWindowTitle("Sticker Print Settings");
    setModal(true);

    // ── spin-box factory ─────────────────────────────────────────────────
    auto makeSpinBox = [this](qreal minVal, qreal maxVal, qreal value) -> QDoubleSpinBox * {
        QDoubleSpinBox *sb = new QDoubleSpinBox(this);
        sb->setRange(minVal, maxVal);
        sb->setDecimals(1);
        sb->setSingleStep(0.5);
        sb->setSuffix(" mm");
        sb->setValue(value);
        return sb;
    };

    m_labelWidth    = makeSpinBox(10.0, 200.0, m_settings.labelWidthMm);
    m_labelHeight   = makeSpinBox(10.0, 200.0, m_settings.labelHeightMm);
    m_barcodeWidth  = makeSpinBox(4.0,  80.0,  m_settings.barcodeWidthMm);
    m_barcodeHeight = makeSpinBox(4.0,  80.0,  m_settings.barcodeHeightMm);
    m_edgeMargin    = makeSpinBox(0.0,  10.0,  m_settings.edgeMarginMm);
    m_innerMargin   = makeSpinBox(0.0,  20.0,  m_settings.innerMarginMm);
    m_logoSize      = makeSpinBox(0.0,  20.0,  m_settings.logoSizeMm);

    m_previewPrefixCombo = new QComboBox(this);
    m_previewPrefixCombo->addItem("SD \xe2\x80\x93 Skylark Drones", "SD");
    m_previewPrefixCombo->addItem("SK \xe2\x80\x93 Skykart",        "SK");
    m_previewPrefixCombo->addItem("SM \xe2\x80\x93 SDMPL",          "SM");
    {
        int idx = m_previewPrefixCombo->findData(normalizedPrefix(previewPrefix));
        if (idx < 0) idx = m_previewPrefixCombo->findData("SK");
        if (idx >= 0) m_previewPrefixCombo->setCurrentIndex(idx);
    }

    // ── left panel: grouped controls ────────────────────────────────────

    auto addGroup = [this](const QString &title, QFormLayout *&formOut) -> QGroupBox * {
        QGroupBox *g = new QGroupBox(title, this);
        formOut = new QFormLayout(g);
        formOut->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        return g;
    };

    QFormLayout *lf = nullptr;
    QGroupBox *labelGroup = addGroup("Sticker Size", lf);
    lf->addRow("Width",  m_labelWidth);
    lf->addRow("Height", m_labelHeight);

    QGroupBox *qrGroup = addGroup("QR Code Size", lf);
    lf->addRow("Width",  m_barcodeWidth);
    lf->addRow("Height", m_barcodeHeight);

    QGroupBox *marginsGroup = addGroup("Margins & Spacing", lf);
    lf->addRow("Outer margin", m_edgeMargin);
    lf->addRow("Text indent",  m_innerMargin);

    QGroupBox *logoGroup = addGroup("Logo", lf);
    lf->addRow("Size", m_logoSize);
    QLabel *logoHint = new QLabel("Tip: drag the <b>Logo</b> element in the preview "
                                  "to reposition it.", this);
    logoHint->setWordWrap(true);
    logoHint->setStyleSheet("color: #666;");
    {
        QVBoxLayout *vl = qobject_cast<QVBoxLayout *>(logoGroup->layout());
        if (!vl) {
            vl = new QVBoxLayout();
            logoGroup->setLayout(vl);
            vl->addLayout(lf);
        }
        vl->addWidget(logoHint);
    }

    QGroupBox *prefixGroup = addGroup("Preview Label Type", lf);
    lf->addRow("Prefix", m_previewPrefixCombo);

    QLabel *dragTip = new QLabel(
        "<b>Drag any coloured element</b> in the preview to reposition it.<br>"
        "Click <i>Reset Defaults</i> to restore original positions.", this);
    dragTip->setWordWrap(true);
    dragTip->setStyleSheet("color: #444; padding: 4px;");

    QVBoxLayout *leftLayout = new QVBoxLayout();
    leftLayout->setSpacing(8);
    leftLayout->addWidget(labelGroup);
    leftLayout->addWidget(qrGroup);
    leftLayout->addWidget(marginsGroup);
    leftLayout->addWidget(logoGroup);
    leftLayout->addWidget(prefixGroup);
    leftLayout->addWidget(dragTip);
    leftLayout->addStretch();

    // ── right panel: live preview ────────────────────────────────────────
    QLabel *previewTitle = new QLabel("Live Preview — drag elements to reposition", this);
    previewTitle->setAlignment(Qt::AlignCenter);
    QFont tf = previewTitle->font();
    tf.setBold(true);
    previewTitle->setFont(tf);

    m_previewLabel = new QLabel(this);
    m_previewLabel->setMinimumSize(340, 400);
    m_previewLabel->setFrameShape(QFrame::StyledPanel);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setMouseTracking(true);
    m_previewLabel->installEventFilter(this);

    QVBoxLayout *rightLayout = new QVBoxLayout();
    rightLayout->addWidget(previewTitle);
    rightLayout->addWidget(m_previewLabel, 1);

    // ── content row ──────────────────────────────────────────────────────
    QHBoxLayout *contentLayout = new QHBoxLayout();
    contentLayout->setSpacing(16);
    contentLayout->addLayout(leftLayout);
    contentLayout->addLayout(rightLayout, 1);

    // ── bottom buttons ────────────────────────────────────────────────────
    QPushButton *resetBtn = new QPushButton("Reset Defaults", this);
    connect(resetBtn, &QPushButton::clicked, this, &PrintSettingsDialog::resetDefaults);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QHBoxLayout *bottomLayout = new QHBoxLayout();
    bottomLayout->addWidget(resetBtn);
    bottomLayout->addStretch();
    bottomLayout->addWidget(buttons);

    // ── main layout ───────────────────────────────────────────────────────
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);
    mainLayout->addLayout(contentLayout, 1);
    mainLayout->addLayout(bottomLayout);

    // ── signal wiring ─────────────────────────────────────────────────────
    connect(m_previewPrefixCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &PrintSettingsDialog::updatePreview);
    connect(m_labelWidth,    qOverload<double>(&QDoubleSpinBox::valueChanged), this, &PrintSettingsDialog::updatePreview);
    connect(m_labelHeight,   qOverload<double>(&QDoubleSpinBox::valueChanged), this, &PrintSettingsDialog::updatePreview);
    connect(m_barcodeWidth,  qOverload<double>(&QDoubleSpinBox::valueChanged), this, &PrintSettingsDialog::updatePreview);
    connect(m_barcodeHeight, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &PrintSettingsDialog::updatePreview);
    connect(m_edgeMargin,    qOverload<double>(&QDoubleSpinBox::valueChanged), this, &PrintSettingsDialog::updatePreview);
    connect(m_innerMargin,   qOverload<double>(&QDoubleSpinBox::valueChanged), this, &PrintSettingsDialog::updatePreview);
    connect(m_logoSize,      qOverload<double>(&QDoubleSpinBox::valueChanged), this, &PrintSettingsDialog::updatePreview);

    m_logoPosXmm      = m_settings.logoPosXmm;
    m_logoPosYmm      = m_settings.logoPosYmm;
    m_qrPosXmm        = m_settings.qrPosXmm;
    m_qrPosYmm        = m_settings.qrPosYmm;
    m_partNamePosXmm  = m_settings.partNamePosXmm;
    m_partNamePosYmm  = m_settings.partNamePosYmm;
    m_infoBlockPosXmm = m_settings.infoBlockPosXmm;
    m_infoBlockPosYmm = m_settings.infoBlockPosYmm;

    updatePreview();

    // Clamp to the available screen so that, on small/high-DPI-scaled displays
    // (e.g. 14" laptops at 150% scaling), the bottom button row never ends up
    // pushed off-screen by the dialog's natural content height.
    const QScreen *dialogScreen = this->screen() ? this->screen() : QGuiApplication::primaryScreen();
    if (dialogScreen) {
        const QRect avail = dialogScreen->availableGeometry();
        const int maxWidth = qMax(320, static_cast<int>(avail.width() * 0.92));
        const int maxHeight = qMax(240, static_cast<int>(avail.height() * 0.88));
        setMinimumSize(qMin(740, maxWidth), qMin(500, maxHeight));
        resize(qMin(sizeHint().width(), maxWidth), qMin(sizeHint().height(), maxHeight));
    }
}

// ── settings() ───────────────────────────────────────────────────────────────

PrintSettings PrintSettingsDialog::settings() const {
    PrintSettings s = m_settings;
    s.labelWidthMm    = m_labelWidth->value();
    s.labelHeightMm   = m_labelHeight->value();
    s.barcodeWidthMm  = m_barcodeWidth->value();
    s.barcodeHeightMm = m_barcodeHeight->value();
    s.edgeMarginMm    = m_edgeMargin->value();
    s.innerMarginMm   = m_innerMargin->value();
    s.logoSizeMm      = m_logoSize->value();
    s.logoPosXmm      = m_logoPosXmm;
    s.logoPosYmm      = m_logoPosYmm;
    s.qrPosXmm        = m_qrPosXmm;
    s.qrPosYmm        = m_qrPosYmm;
    s.partNamePosXmm  = m_partNamePosXmm;
    s.partNamePosYmm  = m_partNamePosYmm;
    s.infoBlockPosXmm = m_infoBlockPosXmm;
    s.infoBlockPosYmm = m_infoBlockPosYmm;
    s.partNameFontSizePt = PrintSettings::kPartNameFontSizePt;
    s.detailFontSizePt   = PrintSettings::kDetailFontSizePt;
    s.fontFamily = QString::fromLatin1(PrintSettings::kDefaultFontFamily);
    return s;
}

// ── static edit() ─────────────────────────────────────────────────────────────

bool PrintSettingsDialog::edit(QWidget *parent,
                               PrintSettings &settings,
                               const QString &previewPrefix,
                               std::function<QImage(const QString &)> renderer,
                               std::function<QImage(const QString &)> logoResolver) {
    PrintSettingsDialog dlg(settings, previewPrefix, std::move(renderer), std::move(logoResolver), parent);
    if (dlg.exec() == QDialog::Accepted) {
        settings = dlg.settings();
        return true;
    }
    return false;
}

// ── resetDefaults() ──────────────────────────────────────────────────────────

void PrintSettingsDialog::resetDefaults() {
    m_labelWidth->setValue(PrintSettings::kLabelWidthMm);
    m_labelHeight->setValue(PrintSettings::kLabelHeightMm);
    m_barcodeWidth->setValue(PrintSettings::kQrWidthMm);
    m_barcodeHeight->setValue(PrintSettings::kQrHeightMm);
    m_edgeMargin->setValue(PrintSettings::kEdgeMarginMm);
    m_innerMargin->setValue(PrintSettings::kInnerMarginMm);
    m_logoSize->setValue(PrintSettings::kLogoSizeMm);
    m_logoPosXmm      = PrintSettings::kAutoPosMm;
    m_logoPosYmm      = PrintSettings::kAutoPosMm;
    m_qrPosXmm        = PrintSettings::kAutoPosMm;
    m_qrPosYmm        = PrintSettings::kAutoPosMm;
    m_partNamePosXmm  = PrintSettings::kAutoPosMm;
    m_partNamePosYmm  = PrintSettings::kAutoPosMm;
    m_infoBlockPosXmm = PrintSettings::kAutoPosMm;
    m_infoBlockPosYmm = PrintSettings::kAutoPosMm;
    updatePreview();
}

// ── hitTest() ─────────────────────────────────────────────────────────────────

PrintSettingsDialog::DragTarget PrintSettingsDialog::hitTest(const QPointF &posPx) const {
    // Check in reverse paint order so topmost element wins.
    if (!m_lastLogoRectPx.isEmpty()      && m_lastLogoRectPx.contains(posPx))      return DragTarget::Logo;
    if (!m_lastQrRectPx.isEmpty()        && m_lastQrRectPx.contains(posPx))        return DragTarget::QrCode;
    if (!m_lastInfoBlockRectPx.isEmpty() && m_lastInfoBlockRectPx.contains(posPx)) return DragTarget::InfoBlock;
    if (!m_lastPartNameRectPx.isEmpty()  && m_lastPartNameRectPx.contains(posPx))  return DragTarget::PartName;
    return DragTarget::None;
}

// ── eventFilter() ─────────────────────────────────────────────────────────────

bool PrintSettingsDialog::eventFilter(QObject *watched, QEvent *event) {
    if (watched != m_previewLabel) {
        return QDialog::eventFilter(watched, event);
    }

    if (event->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton) {
            const DragTarget target = hitTest(me->position());
            if (target != DragTarget::None) {
                m_dragTarget = target;
                QRectF rect;
                switch (target) {
                    case DragTarget::PartName:  rect = m_lastPartNameRectPx;  break;
                    case DragTarget::InfoBlock: rect = m_lastInfoBlockRectPx; break;
                    case DragTarget::QrCode:    rect = m_lastQrRectPx;        break;
                    case DragTarget::Logo:      rect = m_lastLogoRectPx;      break;
                    default: break;
                }
                m_dragOffsetPx = me->position() - rect.topLeft();
                m_previewLabel->setCursor(Qt::ClosedHandCursor);
                return true;
            }
        }

    } else if (event->type() == QEvent::MouseMove) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (m_dragTarget != DragTarget::None && m_lastScalePxPerMm > 0.0) {
            const QPointF topLeftPx = me->position() - m_dragOffsetPx;
            qreal xMm = (topLeftPx.x() - m_lastLabelRectPx.left()) / m_lastScalePxPerMm;
            qreal yMm = (topLeftPx.y() - m_lastLabelRectPx.top())  / m_lastScalePxPerMm;

            qreal elemWmm = 0.0, elemHmm = 0.0;
            switch (m_dragTarget) {
                case DragTarget::PartName:  elemWmm = m_lastPartNameWidthMm;  elemHmm = m_lastPartNameHeightMm;  break;
                case DragTarget::InfoBlock: elemWmm = m_lastInfoBlockWidthMm; elemHmm = m_lastInfoBlockHeightMm; break;
                case DragTarget::QrCode:    elemWmm = m_lastQrWidthMm;        elemHmm = m_lastQrHeightMm;        break;
                case DragTarget::Logo:      elemWmm = m_lastLogoSizeMm;       elemHmm = m_lastLogoSizeMm;        break;
                default: break;
            }

            const qreal maxXmm = qMax(m_lastContentLeftMm, m_lastContentRightMm  - elemWmm);
            const qreal maxYmm = qMax(m_lastContentTopMm,  m_lastContentBottomMm - elemHmm);
            xMm = qMax(m_lastContentLeftMm, qMin(xMm, maxXmm));
            yMm = qMax(m_lastContentTopMm,  qMin(yMm, maxYmm));

            switch (m_dragTarget) {
                case DragTarget::PartName:  m_partNamePosXmm  = xMm; m_partNamePosYmm  = yMm; break;
                case DragTarget::InfoBlock: m_infoBlockPosXmm = xMm; m_infoBlockPosYmm = yMm; break;
                case DragTarget::QrCode:    m_qrPosXmm        = xMm; m_qrPosYmm        = yMm; break;
                case DragTarget::Logo:      m_logoPosXmm      = xMm; m_logoPosYmm      = yMm; break;
                default: break;
            }
            updatePreview();
            return true;
        }
        // Hover cursor
        if (m_dragTarget == DragTarget::None) {
            const DragTarget hover = hitTest(me->position());
            m_previewLabel->setCursor(hover != DragTarget::None ? Qt::OpenHandCursor : Qt::ArrowCursor);
        }

    } else if (event->type() == QEvent::MouseButtonRelease) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton && m_dragTarget != DragTarget::None) {
            m_dragTarget = DragTarget::None;
            const DragTarget hover = hitTest(me->position());
            m_previewLabel->setCursor(hover != DragTarget::None ? Qt::OpenHandCursor : Qt::ArrowCursor);
            return true;
        }

    } else if (event->type() == QEvent::Leave && m_dragTarget == DragTarget::None) {
        m_previewLabel->setCursor(Qt::ArrowCursor);
    }

    return QDialog::eventFilter(watched, event);
}

// ── updatePreview() ───────────────────────────────────────────────────────────

void PrintSettingsDialog::updatePreview() {
    if (!m_previewLabel) return;

    const int w = m_previewLabel->width();
    const int h = m_previewLabel->height();
    QImage preview(w, h, QImage::Format_ARGB32_Premultiplied);
    preview.fill(QColor("#e8e8e8")); // neutral background outside label

    QPainter painter(&preview);
    painter.setRenderHint(QPainter::Antialiasing);

    // ── scale label to fit widget ─────────────────────────────────────────
    const qreal labelWmm = m_labelWidth->value();
    const qreal labelHmm = m_labelHeight->value();
    const qreal padPx    = 14.0;
    const qreal scale    = qMin((w - padPx * 2.0) / labelWmm, (h - padPx * 2.0) / labelHmm);
    const qreal labelWpx = labelWmm * scale;
    const qreal labelHpx = labelHmm * scale;
    auto mmToPx = [&](qreal mm) { return mm * scale; };

    const QRectF labelRect((w - labelWpx) / 2.0, (h - labelHpx) / 2.0, labelWpx, labelHpx);
    painter.fillRect(labelRect, Qt::white);
    painter.setPen(QPen(QColor("#888888"), 1));
    painter.drawRect(labelRect);

    // ── content / margin geometry ─────────────────────────────────────────
    const qreal edgeMm  = m_edgeMargin->value();
    const qreal innerMm = m_innerMargin->value();
    const qreal cLeftMm   = edgeMm;
    const qreal cTopMm    = edgeMm;
    const qreal cRightMm  = qMax(cLeftMm + 1.0, labelWmm - edgeMm);
    const qreal cBottomMm = qMax(cTopMm  + 1.0, labelHmm - edgeMm);
    const qreal cWidthMm  = cRightMm - cLeftMm;

    // ── font / line-height metrics ────────────────────────────────────────
    const qreal ptToMm       = 25.4 / 72.0;
    const qreal partLineHmm  = qMax(1.0, PrintSettings::kPartNameFontSizePt * ptToMm * 1.35);
    const qreal detailLineHmm= qMax(1.0, PrintSettings::kDetailFontSizePt   * ptToMm * 1.35);
    const qreal partNameHmm  = partLineHmm  * 2.0;  // 2-line header
    const qreal infoBlockHmm = detailLineHmm * 5.0; // SKU + Rack + Bin + Serial + Website

    const qreal logoSizeMm  = m_logoSize->value();
    const qreal qrWmm       = m_barcodeWidth->value();
    const qreal qrHmm       = m_barcodeHeight->value();

    // ── default positions ─────────────────────────────────────────────────
    const qreal defPartNameXmm  = cLeftMm;
    const qreal defPartNameYmm  = cTopMm;
    const qreal defQrXmm        = cRightMm - qrWmm;
    const qreal defQrYmm        = cTopMm + partNameHmm;
    const qreal resolvedQrXmm   = (m_qrPosXmm >= 0.0) ? m_qrPosXmm : defQrXmm;
    const qreal infoGapMm       = edgeMm;
    const qreal defInfoXmm      = cLeftMm + innerMm;
    const qreal defInfoYmm      = cTopMm + partNameHmm;
    const qreal infoWidthMm     = qMax(1.0, resolvedQrXmm - defInfoXmm - infoGapMm);
    const qreal defLogoXmm      = cLeftMm;
    const qreal defLogoYmm      = cBottomMm - logoSizeMm;

    // ── resolve stored vs default ─────────────────────────────────────────
    const qreal partNameXmm  = (m_partNamePosXmm  >= 0.0) ? m_partNamePosXmm  : defPartNameXmm;
    const qreal partNameYmm  = (m_partNamePosYmm  >= 0.0) ? m_partNamePosYmm  : defPartNameYmm;
    const qreal qrXmm        = (m_qrPosXmm        >= 0.0) ? m_qrPosXmm        : defQrXmm;
    const qreal qrYmm        = (m_qrPosYmm        >= 0.0) ? m_qrPosYmm        : defQrYmm;
    const qreal infoXmm      = (m_infoBlockPosXmm >= 0.0) ? m_infoBlockPosXmm : defInfoXmm;
    const qreal infoYmm      = (m_infoBlockPosYmm >= 0.0) ? m_infoBlockPosYmm : defInfoYmm;
    const qreal logoXmm      = (m_logoPosXmm      >= 0.0) ? m_logoPosXmm      : defLogoXmm;
    const qreal logoYmm      = (m_logoPosYmm      >= 0.0) ? m_logoPosYmm      : defLogoYmm;

    // ── save layout state for drag ────────────────────────────────────────
    m_lastScalePxPerMm     = scale;
    m_lastLabelRectPx      = labelRect;
    m_lastContentLeftMm    = cLeftMm;
    m_lastContentTopMm     = cTopMm;
    m_lastContentRightMm   = cRightMm;
    m_lastContentBottomMm  = cBottomMm;
    m_lastPartNameWidthMm  = cWidthMm;
    m_lastPartNameHeightMm = partNameHmm;
    m_lastInfoBlockWidthMm = infoWidthMm;
    m_lastInfoBlockHeightMm= infoBlockHmm;
    m_lastQrWidthMm        = qrWmm;
    m_lastQrHeightMm       = qrHmm;
    m_lastLogoSizeMm       = logoSizeMm;

    // ── pixel rects ───────────────────────────────────────────────────────
    QRectF partNameRectPx(labelRect.left() + mmToPx(partNameXmm),
                          labelRect.top()  + mmToPx(partNameYmm),
                          mmToPx(cWidthMm), mmToPx(partNameHmm));
    QRectF infoRectPx(labelRect.left() + mmToPx(infoXmm),
                      labelRect.top()  + mmToPx(infoYmm),
                      mmToPx(infoWidthMm), mmToPx(infoBlockHmm));
    QRectF qrRectPx(labelRect.left() + mmToPx(qrXmm),
                    labelRect.top()  + mmToPx(qrYmm),
                    mmToPx(qrWmm), mmToPx(qrHmm));
    QRectF logoRectPx(labelRect.left() + mmToPx(logoXmm),
                      labelRect.top()  + mmToPx(logoYmm),
                      mmToPx(logoSizeMm), mmToPx(logoSizeMm));

    m_lastPartNameRectPx  = partNameRectPx;
    m_lastInfoBlockRectPx = infoRectPx;
    m_lastQrRectPx        = qrRectPx;
    m_lastLogoRectPx      = (logoSizeMm > 0.0) ? logoRectPx : QRectF();

    // ── drawing helpers ───────────────────────────────────────────────────
    auto drawElement = [&](const QRectF &rect, const QColor &fill, const QColor &border, bool dashed) {
        painter.fillRect(rect, fill);
        painter.setPen(QPen(border, 1.5, dashed ? Qt::DashLine : Qt::SolidLine));
        painter.drawRect(rect);
    };
    auto drawTag = [&](const QRectF &rect, const QString &label, const QColor &col) {
        if (rect.width() < 6 || rect.height() < 6) return;
        QFont f; f.setPixelSize(9); f.setBold(true);
        painter.setFont(f);
        painter.setPen(col);
        painter.drawText(rect.adjusted(2, 1, -2, -1), Qt::AlignTop | Qt::AlignLeft, label);
    };

    const QString selectedPrefix = normalizedPrefix(m_previewPrefixCombo->currentData().toString());
    const QString codeLine  = QString("%1SAMPLE123Q12600001").arg(selectedPrefix);
    const QString siteLine  = websiteForPrefix(selectedPrefix);
    const QString fontFamily = QString::fromLatin1(PrintSettings::kDefaultFontFamily);

    QFont partFont(fontFamily); partFont.setBold(true);
    partFont.setPointSizeF(qMax(1.0, PrintSettings::kPartNameFontSizePt));
    QFont detailFont(fontFamily);
    detailFont.setPointSizeF(qMax(1.0, PrintSettings::kDetailFontSizePt));

    // 1. Part Name — blue, full-width header
    drawElement(partNameRectPx, QColor(173, 216, 230, 100), QColor(30, 100, 210), false);
    painter.setPen(Qt::black);
    painter.setFont(partFont);
    painter.drawText(partNameRectPx.adjusted(2, 1, -2, -1),
                     Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap,
                     "Demo Part Name");
    drawTag(partNameRectPx, "Part Name", QColor(20, 80, 180));

    // 2. Info block — green dashed (SKU / Rack / Bin / Serial / Website)
    drawElement(infoRectPx, QColor(144, 238, 144, 90), QColor(30, 150, 60), true);
    {
        const QStringList lines = {
            "SKU: DEMO-1001", "Rack: R-1", "Bin: B-2", codeLine, siteLine
        };
        qreal lineY = infoYmm;
        painter.setFont(detailFont);
        for (const QString &line : lines) {
            QRectF lr(labelRect.left() + mmToPx(infoXmm),
                      labelRect.top()  + mmToPx(lineY),
                      mmToPx(infoWidthMm), mmToPx(detailLineHmm));
            painter.setPen(Qt::black);
            QFontMetrics fm(detailFont);
            painter.drawText(lr, Qt::AlignLeft | Qt::AlignVCenter,
                             fm.elidedText(line, Qt::ElideRight, qMax(1, (int)lr.width())));
            lineY += detailLineHmm;
        }
    }
    drawTag(infoRectPx, "Info Block", QColor(20, 120, 40));

    // 3. QR code — orange
    drawElement(qrRectPx, QColor(255, 220, 150, 100), QColor(200, 120, 0), false);
    {
        QImage qr;
        if (m_renderer) qr = m_renderer(codeLine);
        if (!qr.isNull()) {
            QImage scaled = qr.scaled(qrRectPx.size().toSize(), Qt::KeepAspectRatio, Qt::FastTransformation);
            painter.drawImage(qrRectPx.topLeft() +
                              QPointF((qrRectPx.width()  - scaled.width())  / 2.0,
                                      (qrRectPx.height() - scaled.height()) / 2.0), scaled);
        }
    }
    drawTag(qrRectPx, "QR Code", QColor(160, 80, 0));

    // 4. Logo — purple, bottom-left
    if (logoSizeMm > 0.0) {
        drawElement(logoRectPx, QColor(200, 160, 220, 100), QColor(120, 0, 180), true);
        QImage logo;
        if (m_logoResolver) logo = m_logoResolver(selectedPrefix);
        if (logo.isNull()) logo.load(":/assets/sticker_logo.png");
        if (!logo.isNull()) {
            QImage scaled = logo.scaled(logoRectPx.size().toSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            painter.drawImage(logoRectPx.topLeft() +
                              QPointF((logoRectPx.width()  - scaled.width())  / 2.0,
                                      (logoRectPx.height() - scaled.height()) / 2.0), scaled);
        }
        drawTag(logoRectPx, "Logo", QColor(100, 0, 160));
    }

    // ── legend row at bottom ──────────────────────────────────────────────
    struct { QColor col; QString label; } legend[] = {
        {QColor(30,100,210), "Part Name"},
        {QColor(30,150,60),  "Info Block"},
        {QColor(200,120,0),  "QR Code"},
        {QColor(120,0,180),  "Logo"},
    };
    const int legendY = h - 14;
    QFont lf2; lf2.setPixelSize(9);
    painter.setFont(lf2);
    qreal legendX = padPx;
    for (auto &item : legend) {
        painter.fillRect(QRectF(legendX, legendY + 1, 10, 9), item.col);
        painter.setPen(item.col.darker(150));
        painter.drawRect(QRectF(legendX, legendY + 1, 10, 9));
        painter.setPen(QColor("#333333"));
        painter.drawText(QRectF(legendX + 12, legendY, 72, 12), Qt::AlignLeft | Qt::AlignVCenter, item.label);
        legendX += 86;
    }

    m_previewLabel->setPixmap(QPixmap::fromImage(preview));
}
