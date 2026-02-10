#ifndef PRINTSETTINGSDIALOG_H
#define PRINTSETTINGSDIALOG_H

#include <QDialog>
#include <QImage>
#include <functional>

class QDoubleSpinBox;
class QLabel;

struct PrintSettings {
    static constexpr qreal kLabelWidthMm = 50.0;
    static constexpr qreal kLabelHeightMm = 30.0;
    static constexpr qreal kQrWidthMm = 20.0;
    static constexpr qreal kQrHeightMm = 20.0;

    qreal labelWidthMm = kLabelWidthMm;
    qreal labelHeightMm = kLabelHeightMm;
    qreal barcodeWidthMm = kQrWidthMm;
    qreal barcodeHeightMm = kQrHeightMm;
    qreal innerMarginMm = 2.0;

    void load();
    void save() const;
};

class PrintSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit PrintSettingsDialog(const PrintSettings &settings,
                                 std::function<QImage(const QString &)> renderer,
                                 QWidget *parent = nullptr);

    PrintSettings settings() const;
    static bool edit(QWidget *parent,
                     PrintSettings &settings,
                     std::function<QImage(const QString &)> renderer);

private slots:
    void updatePreview();
    void resetDefaults();

private:
    PrintSettings m_settings;
    std::function<QImage(const QString &)> m_renderer;
    QDoubleSpinBox *m_labelWidth = nullptr;
    QDoubleSpinBox *m_labelHeight = nullptr;
    QDoubleSpinBox *m_barcodeWidth = nullptr;
    QDoubleSpinBox *m_barcodeHeight = nullptr;
    QDoubleSpinBox *m_innerMargin = nullptr;
    QLabel *m_previewLabel = nullptr;
};

#endif // PRINTSETTINGSDIALOG_H
