#ifndef PRINTSETTINGSDIALOG_H
#define PRINTSETTINGSDIALOG_H

#include <QDialog>
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <functional>

class QDoubleSpinBox;
class QLabel;
class QComboBox;

struct PrintSettings {
    // Physical defaults matching the warehouse sticker template.
    static constexpr qreal kLabelWidthMm     = 50.0;
    static constexpr qreal kLabelHeightMm    = 30.0;
    static constexpr qreal kQrWidthMm        = 20.0;
    static constexpr qreal kQrHeightMm       = 20.0;
    static constexpr qreal kEdgeMarginMm     = 2.0;
    static constexpr qreal kInnerMarginMm    = 2.0;
    static constexpr qreal kLogoSizeMm       = 8.0;
    static constexpr qreal kAutoPosMm        = -1.0; // sentinel: use layout default
    static constexpr qreal kAutoLogoPosMm    = kAutoPosMm; // backward-compat alias
    static constexpr qreal kPartNameFontSizePt = 5.0;
    static constexpr qreal kDetailFontSizePt   = 5.0;
    static constexpr const char *kDefaultFontFamily = "Britannic Bold";

    qreal labelWidthMm    = kLabelWidthMm;
    qreal labelHeightMm   = kLabelHeightMm;
    qreal barcodeWidthMm  = kQrWidthMm;
    qreal barcodeHeightMm = kQrHeightMm;
    qreal edgeMarginMm    = kEdgeMarginMm;
    qreal innerMarginMm   = kInnerMarginMm;
    qreal logoSizeMm      = kLogoSizeMm;

    // Per-element positions in mm from label top-left.
    // kAutoPosMm means "use the computed layout default".
    qreal logoPosXmm      = kAutoPosMm; // logo  (default: bottom-left)
    qreal logoPosYmm      = kAutoPosMm;
    qreal qrPosXmm        = kAutoPosMm; // QR code (default: right, below header)
    qreal qrPosYmm        = kAutoPosMm;
    qreal partNamePosXmm  = kAutoPosMm; // part-name header (default: full-width top)
    qreal partNamePosYmm  = kAutoPosMm;
    qreal infoBlockPosXmm = kAutoPosMm; // SKU/Rack/Bin/Serial/Website block
    qreal infoBlockPosYmm = kAutoPosMm;

    qreal partNameFontSizePt = kPartNameFontSizePt;
    qreal detailFontSizePt   = kDetailFontSizePt;
    QString fontFamily = QString::fromLatin1(kDefaultFontFamily);

    void load();
    void save() const;
};

// ──────────────────────────────────────────────────────────────────────────────

class PrintSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit PrintSettingsDialog(const PrintSettings &settings,
                                 const QString &previewPrefix,
                                 std::function<QImage(const QString &)> renderer,
                                 std::function<QImage(const QString &)> logoResolver,
                                 QWidget *parent = nullptr);

    PrintSettings settings() const;

    static bool edit(QWidget *parent,
                     PrintSettings &settings,
                     const QString &previewPrefix,
                     std::function<QImage(const QString &)> renderer,
                     std::function<QImage(const QString &)> logoResolver);

private slots:
    void updatePreview();
    void resetDefaults();

private:
    enum class DragTarget { None, PartName, InfoBlock, QrCode, Logo };

    bool eventFilter(QObject *watched, QEvent *event) override;
    DragTarget hitTest(const QPointF &posPx) const;

    PrintSettings m_settings;
    std::function<QImage(const QString &)> m_renderer;
    std::function<QImage(const QString &)> m_logoResolver;

    QDoubleSpinBox *m_labelWidth    = nullptr;
    QDoubleSpinBox *m_labelHeight   = nullptr;
    QDoubleSpinBox *m_barcodeWidth  = nullptr;
    QDoubleSpinBox *m_barcodeHeight = nullptr;
    QDoubleSpinBox *m_edgeMargin    = nullptr;
    QDoubleSpinBox *m_innerMargin   = nullptr;
    QDoubleSpinBox *m_logoSize      = nullptr;
    QComboBox      *m_previewPrefixCombo = nullptr;
    QLabel         *m_previewLabel  = nullptr;

    // Per-element positions tracked in the dialog (mm, < 0 = auto).
    qreal m_logoPosXmm      = PrintSettings::kAutoPosMm;
    qreal m_logoPosYmm      = PrintSettings::kAutoPosMm;
    qreal m_qrPosXmm        = PrintSettings::kAutoPosMm;
    qreal m_qrPosYmm        = PrintSettings::kAutoPosMm;
    qreal m_partNamePosXmm  = PrintSettings::kAutoPosMm;
    qreal m_partNamePosYmm  = PrintSettings::kAutoPosMm;
    qreal m_infoBlockPosXmm = PrintSettings::kAutoPosMm;
    qreal m_infoBlockPosYmm = PrintSettings::kAutoPosMm;

    // Drag state.
    DragTarget m_dragTarget = DragTarget::None;
    QPointF    m_dragOffsetPx;

    // Element rects in preview-widget pixels (saved each render for hit-testing).
    QRectF m_lastPartNameRectPx;
    QRectF m_lastInfoBlockRectPx;
    QRectF m_lastQrRectPx;
    QRectF m_lastLogoRectPx;
    QRectF m_lastLabelRectPx;

    // Layout state saved from the last render (needed for mm↔px conversion).
    qreal m_lastScalePxPerMm     = 1.0;
    qreal m_lastContentLeftMm    = 0.0;
    qreal m_lastContentTopMm     = 0.0;
    qreal m_lastContentRightMm   = 0.0;
    qreal m_lastContentBottomMm  = 0.0;
    qreal m_lastPartNameWidthMm  = 0.0;
    qreal m_lastPartNameHeightMm = 0.0;
    qreal m_lastInfoBlockWidthMm = 0.0;
    qreal m_lastInfoBlockHeightMm= 0.0;
    qreal m_lastQrWidthMm        = 0.0;
    qreal m_lastQrHeightMm       = 0.0;
    qreal m_lastLogoSizeMm       = 0.0;
};

#endif // PRINTSETTINGSDIALOG_H
