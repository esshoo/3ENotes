#include "LaserSubToolbar.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>

namespace {
QLabel* compactLabel(const QString& text, QWidget* parent, const QString& tip)
{
    auto* label = new QLabel(text, parent);
    label->setToolTip(tip);
    label->setStyleSheet(QStringLiteral("font-size: 10px;"));
    return label;
}
}

LaserSubToolbar::LaserSubToolbar(QWidget* parent)
    : SubToolbar(parent)
{
    buildUi();
    loadSettings();
}

void LaserSubToolbar::buildUi()
{
    m_colorButton = new QPushButton(this);
    m_colorButton->setFixedSize(28, 28);
    m_colorButton->setToolTip(tr("Laser color"));
    addWidget(m_colorButton);

    addWidget(compactLabel(tr("Spot"), this, tr("Laser point diameter in screen pixels")));
    m_spotSize = new QDoubleSpinBox(this);
    m_spotSize->setRange(4.0, 40.0);
    m_spotSize->setDecimals(0);
    m_spotSize->setSuffix(tr(" px"));
    m_spotSize->setFixedWidth(68);
    addWidget(m_spotSize);

    addSeparator();
    addWidget(compactLabel(tr("Trail"), this, tr("Maximum visible laser trail length")));
    m_trailLength = new QDoubleSpinBox(this);
    m_trailLength->setRange(0.0, 20.0);
    m_trailLength->setSingleStep(1.0);
    m_trailLength->setDecimals(1);
    m_trailLength->setSuffix(tr(" cm"));
    m_trailLength->setFixedWidth(76);
    addWidget(m_trailLength);

    // Advanced controls are kept as persistent value widgets but live outside
    // the inline toolbar. The dialog below mirrors them, keeping the toolbar
    // compact enough for phones/tablets.
    m_trailThickness = new QDoubleSpinBox(this);
    m_trailThickness->setRange(1.0, 20.0);
    m_trailThickness->setDecimals(0);
    m_trailThickness->setSuffix(tr(" px"));
    m_trailThickness->hide();

    m_holdDuration = new QSpinBox(this);
    m_holdDuration->setRange(0, 10000);
    m_holdDuration->setSingleStep(100);
    m_holdDuration->setSuffix(tr(" ms"));
    m_holdDuration->hide();

    m_fadeDuration = new QSpinBox(this);
    m_fadeDuration->setRange(100, 5000);
    m_fadeDuration->setSingleStep(100);
    m_fadeDuration->setSuffix(tr(" ms"));
    m_fadeDuration->hide();

    m_pressOnly = new QCheckBox(this);
    m_pressOnly->hide();
    m_pulse = new QCheckBox(this);
    m_pulse->hide();

    m_spotlight = new QCheckBox(this);
    m_spotlight->hide();

    m_spotlightRadius = new QDoubleSpinBox(this);
    m_spotlightRadius->setRange(40.0, 300.0);
    m_spotlightRadius->setDecimals(0);
    m_spotlightRadius->setSuffix(tr(" px"));
    m_spotlightRadius->hide();

    m_advancedButton = new QPushButton(QStringLiteral("⋯"), this);
    m_advancedButton->setFixedSize(30, 28);
    m_advancedButton->setToolTip(tr("Advanced laser settings"));
    addWidget(m_advancedButton);

    connect(m_colorButton, &QPushButton::clicked, this, [this]() {
        QColor chosen = QColorDialog::getColor(m_color, this, tr("Select Laser Color"),
                                               QColorDialog::ShowAlphaChannel);
        if (!chosen.isValid()) return;
        chosen.setAlpha(255);
        m_color = chosen;
        updateColorButton();
        saveSettings();
        emit laserColorChanged(m_color);
    });
    connect(m_spotSize, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        saveSettings(); emit laserSpotSizeChanged(value);
    });
    connect(m_trailLength, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        saveSettings(); emit laserTrailLengthChanged(value);
    });
    connect(m_trailThickness, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        saveSettings(); emit laserTrailThicknessChanged(value);
    });
    connect(m_holdDuration, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        saveSettings(); emit laserHoldDurationChanged(value);
    });
    connect(m_fadeDuration, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        saveSettings(); emit laserFadeDurationChanged(value);
    });
    connect(m_pressOnly, &QCheckBox::toggled, this, [this](bool checked) {
        saveSettings(); emit laserPressOnlyChanged(checked);
    });
    connect(m_pulse, &QCheckBox::toggled, this, [this](bool checked) {
        saveSettings(); emit laserPulseEnabledChanged(checked);
    });
    connect(m_spotlight, &QCheckBox::toggled, this, [this](bool checked) {
        saveSettings(); emit laserSpotlightEnabledChanged(checked);
    });
    connect(m_spotlightRadius, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        saveSettings(); emit laserSpotlightRadiusChanged(value);
    });

    connect(m_advancedButton, &QPushButton::clicked, this, [this]() {
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Laser Pointer Settings"));
        auto* layout = new QVBoxLayout(&dialog);
        auto* form = new QFormLayout();

        auto* thickness = new QDoubleSpinBox(&dialog);
        thickness->setRange(1.0, 20.0);
        thickness->setDecimals(0);
        thickness->setSuffix(tr(" px"));
        thickness->setValue(m_trailThickness->value());
        form->addRow(tr("Trail thickness:"), thickness);

        auto* hold = new QSpinBox(&dialog);
        hold->setRange(0, 10000);
        hold->setSingleStep(100);
        hold->setSuffix(tr(" ms"));
        hold->setValue(m_holdDuration->value());
        form->addRow(tr("Stay visible:"), hold);

        auto* fade = new QSpinBox(&dialog);
        fade->setRange(100, 5000);
        fade->setSingleStep(100);
        fade->setSuffix(tr(" ms"));
        fade->setValue(m_fadeDuration->value());
        form->addRow(tr("Fade duration:"), fade);

        auto* pressOnly = new QCheckBox(tr("Show only while pressed"), &dialog);
        pressOnly->setChecked(m_pressOnly->isChecked());
        form->addRow(QString(), pressOnly);

        auto* pulse = new QCheckBox(tr("Pulse when pressing"), &dialog);
        pulse->setChecked(m_pulse->isChecked());
        form->addRow(QString(), pulse);

        auto* spotlight = new QCheckBox(tr("Spotlight mode"), &dialog);
        spotlight->setChecked(m_spotlight->isChecked());
        form->addRow(QString(), spotlight);

        auto* spotlightRadius = new QDoubleSpinBox(&dialog);
        spotlightRadius->setRange(40.0, 300.0);
        spotlightRadius->setDecimals(0);
        spotlightRadius->setSuffix(tr(" px"));
        spotlightRadius->setValue(m_spotlightRadius->value());
        spotlightRadius->setEnabled(spotlight->isChecked());
        form->addRow(tr("Spotlight radius:"), spotlightRadius);
        connect(spotlight, &QCheckBox::toggled, spotlightRadius, &QDoubleSpinBox::setEnabled);

        layout->addLayout(form);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                             Qt::Horizontal, &dialog);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(buttons);

        if (dialog.exec() == QDialog::Accepted) {
            m_trailThickness->setValue(thickness->value());
            m_holdDuration->setValue(hold->value());
            m_fadeDuration->setValue(fade->value());
            m_pressOnly->setChecked(pressOnly->isChecked());
            m_pulse->setChecked(pulse->isChecked());
            m_spotlight->setChecked(spotlight->isChecked());
            m_spotlightRadius->setValue(spotlightRadius->value());
            saveSettings();
        }
    });
}

void LaserSubToolbar::loadSettings()
{
    // The widgets are connected to saveSettings(), so restoring one value at a
    // time without blockers would overwrite settings that have not been read
    // yet. Keep the restore atomic and emit the complete state afterwards only
    // when MainWindow explicitly requests it.
    const QSignalBlocker blockSpot(m_spotSize);
    const QSignalBlocker blockLength(m_trailLength);
    const QSignalBlocker blockThickness(m_trailThickness);
    const QSignalBlocker blockHold(m_holdDuration);
    const QSignalBlocker blockFade(m_fadeDuration);
    const QSignalBlocker blockPressOnly(m_pressOnly);
    const QSignalBlocker blockPulse(m_pulse);
    const QSignalBlocker blockSpotlight(m_spotlight);
    const QSignalBlocker blockSpotlightRadius(m_spotlightRadius);

    QSettings settings;
    settings.beginGroup(QStringLiteral("laser"));
    m_color = settings.value(QStringLiteral("color"), QColor(QStringLiteral("#ff2d2d"))).value<QColor>();
    m_spotSize->setValue(settings.value(QStringLiteral("spotSize"), 14.0).toDouble());
    m_trailLength->setValue(settings.value(QStringLiteral("trailLengthCm"), 12.0).toDouble());
    m_trailThickness->setValue(settings.value(QStringLiteral("trailThickness"), 6.0).toDouble());
    m_holdDuration->setValue(settings.value(QStringLiteral("holdDurationMs"), 1200).toInt());
    m_fadeDuration->setValue(settings.value(QStringLiteral("fadeDurationMs"), 700).toInt());
    m_pressOnly->setChecked(settings.value(QStringLiteral("pressOnly"), false).toBool());
    m_pulse->setChecked(settings.value(QStringLiteral("pulse"), true).toBool());
    m_spotlight->setChecked(settings.value(QStringLiteral("spotlight"), false).toBool());
    m_spotlightRadius->setValue(settings.value(QStringLiteral("spotlightRadius"), 110.0).toDouble());
    settings.endGroup();
    updateColorButton();
}

void LaserSubToolbar::saveSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("laser"));
    settings.setValue(QStringLiteral("color"), m_color);
    settings.setValue(QStringLiteral("spotSize"), m_spotSize->value());
    settings.setValue(QStringLiteral("trailLengthCm"), m_trailLength->value());
    settings.setValue(QStringLiteral("trailThickness"), m_trailThickness->value());
    settings.setValue(QStringLiteral("holdDurationMs"), m_holdDuration->value());
    settings.setValue(QStringLiteral("fadeDurationMs"), m_fadeDuration->value());
    settings.setValue(QStringLiteral("pressOnly"), m_pressOnly->isChecked());
    settings.setValue(QStringLiteral("pulse"), m_pulse->isChecked());
    settings.setValue(QStringLiteral("spotlight"), m_spotlight->isChecked());
    settings.setValue(QStringLiteral("spotlightRadius"), m_spotlightRadius->value());
    settings.endGroup();
}

void LaserSubToolbar::updateColorButton()
{
    m_colorButton->setStyleSheet(QStringLiteral(
        "QPushButton { border-radius: 14px; border: 2px solid rgba(255,255,255,190); "
        "background-color: %1; } QPushButton:hover { border: 2px solid white; }").arg(m_color.name()));
}

void LaserSubToolbar::refreshFromSettings() { loadSettings(); }
void LaserSubToolbar::setDarkMode(bool darkMode) { SubToolbar::setDarkMode(darkMode); updateColorButton(); }
qreal LaserSubToolbar::currentSpotSize() const { return m_spotSize->value(); }
qreal LaserSubToolbar::currentTrailLengthCm() const { return m_trailLength->value(); }
qreal LaserSubToolbar::currentTrailThickness() const { return m_trailThickness->value(); }
int LaserSubToolbar::currentHoldDurationMs() const { return m_holdDuration->value(); }
int LaserSubToolbar::currentFadeDurationMs() const { return m_fadeDuration->value(); }
bool LaserSubToolbar::currentPressOnly() const { return m_pressOnly->isChecked(); }
bool LaserSubToolbar::currentPulseEnabled() const { return m_pulse->isChecked(); }
bool LaserSubToolbar::currentSpotlightEnabled() const { return m_spotlight->isChecked(); }
qreal LaserSubToolbar::currentSpotlightRadius() const { return m_spotlightRadius->value(); }

void LaserSubToolbar::emitCurrentValues()
{
    emit laserColorChanged(m_color);
    emit laserSpotSizeChanged(currentSpotSize());
    emit laserTrailLengthChanged(currentTrailLengthCm());
    emit laserTrailThicknessChanged(currentTrailThickness());
    emit laserHoldDurationChanged(currentHoldDurationMs());
    emit laserFadeDurationChanged(currentFadeDurationMs());
    emit laserPressOnlyChanged(currentPressOnly());
    emit laserPulseEnabledChanged(currentPulseEnabled());
    emit laserSpotlightEnabledChanged(currentSpotlightEnabled());
    emit laserSpotlightRadiusChanged(currentSpotlightRadius());
}
