#ifndef LASERSUBTOOLBAR_H
#define LASERSUBTOOLBAR_H

#include "SubToolbar.h"
#include <QColor>

class QPushButton;
class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;

class LaserSubToolbar : public SubToolbar {
    Q_OBJECT
public:
    explicit LaserSubToolbar(QWidget* parent = nullptr);

    void refreshFromSettings() override;
    void restoreTabState(int tabId) override { Q_UNUSED(tabId); }
    void saveTabState(int tabId) override { Q_UNUSED(tabId); }
    void clearTabState(int tabId) override { Q_UNUSED(tabId); }
    void setDarkMode(bool darkMode) override;

    QColor currentColor() const { return m_color; }
    qreal currentSpotSize() const;
    qreal currentTrailLengthCm() const;
    qreal currentTrailThickness() const;
    int currentHoldDurationMs() const;
    int currentFadeDurationMs() const;
    bool currentPressOnly() const;
    bool currentPulseEnabled() const;
    bool currentSpotlightEnabled() const;
    qreal currentSpotlightRadius() const;
    void emitCurrentValues();

signals:
    void laserColorChanged(const QColor& color);
    void laserSpotSizeChanged(qreal pixels);
    void laserTrailLengthChanged(qreal centimeters);
    void laserTrailThicknessChanged(qreal pixels);
    void laserHoldDurationChanged(int milliseconds);
    void laserFadeDurationChanged(int milliseconds);
    void laserPressOnlyChanged(bool enabled);
    void laserPulseEnabledChanged(bool enabled);
    void laserSpotlightEnabledChanged(bool enabled);
    void laserSpotlightRadiusChanged(qreal pixels);

private:
    void buildUi();
    void loadSettings();
    void saveSettings();
    void updateColorButton();

    QColor m_color = QColor("#ff2d2d");
    QPushButton* m_colorButton = nullptr;
    QPushButton* m_advancedButton = nullptr;
    QDoubleSpinBox* m_spotSize = nullptr;
    QDoubleSpinBox* m_trailLength = nullptr;
    QDoubleSpinBox* m_trailThickness = nullptr;
    QSpinBox* m_holdDuration = nullptr;
    QSpinBox* m_fadeDuration = nullptr;
    QCheckBox* m_pressOnly = nullptr;
    QCheckBox* m_pulse = nullptr;
    QCheckBox* m_spotlight = nullptr;
    QDoubleSpinBox* m_spotlightRadius = nullptr;
};

#endif
