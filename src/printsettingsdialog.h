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
    static constexpr qreal kLabelWidthMm = 50.0;
    static constexpr qreal kLabelHeightMm = 30.0;
    static constexpr qreal kQrWidthMm = 20.0;
    static constexpr qreal kQrHeightMm = 20.0;
    static constexpr qreal kEdgeMarginMm = 2.0;
    static constexpr qreal kInnerMarginMm = 2.0;
    static constexpr qreal kLogoSizeMm = 8.0;
    static constexpr qreal kAutoLogoPosMm = -1.0;
    static constexpr qreal kPartNameFontSizePt = 5.0;
    static constexpr qreal kDetailFontSizePt = 5.0;
    static constexpr const char *kDefaultFontFamily = "Britannic Bold";

    qreal labelWidthMm = kLabelWidthMm;
    qreal labelHeightMm = kLabelHeightMm;
    qreal barcodeWidthMm = kQrWidthMm;
    qreal barcodeHeightMm = kQrHeightMm;
    qreal edgeMarginMm = kEdgeMarginMm;
    qreal innerMarginMm = kInnerMarginMm;
    qreal logoSizeMm = kLogoSizeMm;
    qreal logoPosXmm = kAutoLogoPosMm;
    qreal logoPosYmm = kAutoLogoPosMm;
    qreal partNameFontSizePt = kPartNameFontSizePt;
    qreal detailFontSizePt = kDetailFontSizePt;
    QString fontFamily = QString::fromLatin1(kDefaultFontFamily);

    void load();
    void save() const;
};

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
    bool eventFilter(QObject *watched, QEvent *event) override;

    PrintSettings m_settings;
    std::function<QImage(const QString &)> m_renderer;
    std::function<QImage(const QString &)> m_logoResolver;
    QDoubleSpinBox *m_labelWidth = nullptr;
    QDoubleSpinBox *m_labelHeight = nullptr;
    QDoubleSpinBox *m_barcodeWidth = nullptr;
    QDoubleSpinBox *m_barcodeHeight = nullptr;
    QDoubleSpinBox *m_edgeMargin = nullptr;
    QDoubleSpinBox *m_innerMargin = nullptr;
    QDoubleSpinBox *m_logoSize = nullptr;
    QComboBox *m_previewPrefixCombo = nullptr;
    QLabel *m_previewLabel = nullptr;

    qreal m_logoPosXmm = PrintSettings::kAutoLogoPosMm;
    qreal m_logoPosYmm = PrintSettings::kAutoLogoPosMm;
    bool m_draggingLogo = false;
    QPointF m_logoDragOffsetPx;
    QRectF m_lastLogoRectPx;
    QRectF m_lastLabelRectPx;
    qreal m_lastScalePxPerMm = 1.0;
    qreal m_lastContentLeftMm = 0.0;
    qreal m_lastContentTopMm = 0.0;
    qreal m_lastContentRightMm = 0.0;
    qreal m_lastContentBottomMm = 0.0;
    qreal m_lastLogoSizeMm = 0.0;
};

#endif // PRINTSETTINGSDIALOG_H
