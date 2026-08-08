#include "RecoveryCenterDialog.h"

#include "../../MainWindow.h"
#include "../../core/DocumentManager.h"

#include <QAbstractButton>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

QJsonObject readRecoveryMetadata(const QString& snapshotPath)
{
    QFile file(QDir(snapshotPath).filePath(QStringLiteral("recovery.json")));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    const QJsonDocument json = QJsonDocument::fromJson(file.readAll());
    file.close();
    return json.isObject() ? json.object() : QJsonObject();
}

} // namespace

RecoveryCenterDialog::RecoveryCenterDialog(
    MainWindow* mainWindow,
    DocumentManager* documentManager,
    QWidget* parent)
    : QDialog(parent),
      m_mainWindow(mainWindow),
      m_documentManager(documentManager)
{
    setWindowTitle(tr("Recovery Center"));
    setWindowIcon(QIcon(QStringLiteral(":/resources/icons/mainicon.svg")));
    setModal(true);
    resize(760, 470);

    if (QLocale().textDirection() == Qt::RightToLeft) {
        setLayoutDirection(Qt::RightToLeft);
    }

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);

    auto* title = new QLabel(tr("Recovery Center"), this);
    QFont titleFont = title->font();
    titleFont.setPointSize(16);
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);

    auto* description = new QLabel(
        tr("Recovery copies are stored separately from your projects. "
           "You can open a copy for inspection or restore an older version safely."),
        this);
    description->setWordWrap(true);
    description->setStyleSheet(QStringLiteral("color: palette(placeholderText);"));
    layout->addWidget(description);

    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setAlternatingRowColors(true);
    layout->addWidget(m_list, 1);

    m_emptyLabel = new QLabel(tr("No recovery copies are available."), this);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setStyleSheet(
        QStringLiteral("color: palette(placeholderText); padding: 16px;"));
    layout->addWidget(m_emptyLabel);

    auto* buttons = new QHBoxLayout();

    m_openButton = new QPushButton(tr("Open Recovery Copy"), this);
    m_restoreButton = new QPushButton(tr("Restore Previous Version"), this);
    m_deleteButton = new QPushButton(tr("Delete Copy"), this);
    m_clearButton = new QPushButton(tr("Clear All"), this);
    auto* closeButton = new QPushButton(tr("Close"), this);

    buttons->addWidget(m_openButton);
    buttons->addWidget(m_restoreButton);
    buttons->addWidget(m_deleteButton);
    buttons->addStretch();
    buttons->addWidget(m_clearButton);
    buttons->addWidget(closeButton);
    layout->addLayout(buttons);

    connect(m_list, &QListWidget::itemSelectionChanged,
            this, &RecoveryCenterDialog::updateButtons);
    connect(m_openButton, &QPushButton::clicked, this, [this]() {
        const QString path = selectedSnapshotPath();
        if (!path.isEmpty() && m_mainWindow) {
            m_mainWindow->openFileInNewTab(path);
        }
    });
    connect(m_restoreButton, &QPushButton::clicked, this, [this]() {
        if (!m_documentManager) {
            return;
        }

        const QString snapshotPath = selectedSnapshotPath();
        if (snapshotPath.isEmpty()) {
            return;
        }

        const QString originalPath =
            m_documentManager->recoveryOriginalPath(snapshotPath);

        // 3ENOTES_RECOVERY_RESTORE_BUTTON_FIX_V1
        QMessageBox restoreBox(this);
        restoreBox.setIcon(QMessageBox::Warning);
        restoreBox.setWindowTitle(tr("Restore Previous Version"));
        restoreBox.setText(
            tr("This will replace the saved project at:\n%1\n\n"
               "The current saved version will be kept as a safety recovery copy.")
                .arg(QDir::toNativeSeparators(originalPath)));

        QAbstractButton* restoreButton =
            restoreBox.addButton(tr("Restore"), QMessageBox::AcceptRole);
        restoreBox.addButton(QMessageBox::Cancel);
        restoreBox.setDefaultButton(QMessageBox::Cancel);
        restoreBox.exec();

        if (restoreBox.clickedButton() != restoreButton) {
            return;
        }

        QString restoredPath;
        QString error;
        if (!m_documentManager->restoreRecoverySnapshot(
                snapshotPath, &restoredPath, &error)) {
            QMessageBox::critical(
                this,
                tr("Restore Failed"),
                error.isEmpty() ? tr("The project could not be restored.") : error);
            return;
        }

        QMessageBox::information(
            this,
            tr("Restore Complete"),
            tr("The previous version was restored successfully."));

        refresh();
        if (m_mainWindow && !restoredPath.isEmpty()) {
            m_mainWindow->openFileInNewTab(restoredPath);
        }
    });
    connect(m_deleteButton, &QPushButton::clicked, this, [this]() {
        if (!m_documentManager) {
            return;
        }

        const QString path = selectedSnapshotPath();
        if (path.isEmpty()) {
            return;
        }

        if (QMessageBox::question(
                this,
                tr("Delete Recovery Copy"),
                tr("Delete the selected recovery copy permanently?"),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No) != QMessageBox::Yes) {
            return;
        }

        if (!m_documentManager->deleteRecoverySnapshot(path)) {
            QMessageBox::warning(
                this,
                tr("Delete Failed"),
                tr("The selected recovery copy could not be deleted."));
        }
        refresh();
    });
    connect(m_clearButton, &QPushButton::clicked, this, [this]() {
        if (!m_documentManager) {
            return;
        }

        if (QMessageBox::warning(
                this,
                tr("Clear Recovery Copies"),
                tr("Delete all recovery copies? This cannot be undone."),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No) != QMessageBox::Yes) {
            return;
        }

        m_documentManager->clearRecoverySnapshots();
        refresh();
    });
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);

    refresh();
}

QString RecoveryCenterDialog::selectedSnapshotPath() const
{
    if (!m_list || !m_list->currentItem()) {
        return QString();
    }
    return m_list->currentItem()->data(Qt::UserRole).toString();
}

void RecoveryCenterDialog::refresh()
{
    m_list->clear();

    if (!m_documentManager) {
        updateButtons();
        return;
    }

    const QStringList snapshots = m_documentManager->recoverySnapshots();
    for (const QString& snapshotPath : snapshots) {
        const QJsonObject metadata = readRecoveryMetadata(snapshotPath);

        QString name =
            metadata.value(QStringLiteral("document_name")).toString();
        QString original =
            metadata.value(QStringLiteral("original_path")).toString();
        QDateTime time = QDateTime::fromString(
            metadata.value(QStringLiteral("snapshot_utc")).toString(),
            Qt::ISODate);

        if (name.isEmpty()) {
            name = QFileInfo(original).completeBaseName();
        }
        if (name.isEmpty()) {
            name = tr("Recovered Project");
        }
        if (!time.isValid()) {
            time = QFileInfo(snapshotPath).lastModified();
        }

        const QString originalDisplay = original.isEmpty()
            ? tr("Original project path unavailable")
            : QDir::toNativeSeparators(original);

        const QString label =
            tr("%1\n%2\n%3")
                .arg(name,
                     QLocale().toString(time.toLocalTime(), QLocale::ShortFormat),
                     originalDisplay);

        auto* item = new QListWidgetItem(label, m_list);
        item->setData(Qt::UserRole, snapshotPath);
        item->setToolTip(QDir::toNativeSeparators(snapshotPath));
    }

    m_emptyLabel->setVisible(m_list->count() == 0);
    if (m_list->count() > 0) {
        m_list->setCurrentRow(0);
    }
    updateButtons();
}

void RecoveryCenterDialog::updateButtons()
{
    const bool hasSelection = !selectedSnapshotPath().isEmpty();
    const bool hasAny = m_list && m_list->count() > 0;

    m_openButton->setEnabled(hasSelection);
    m_restoreButton->setEnabled(hasSelection);
    m_deleteButton->setEnabled(hasSelection);
    m_clearButton->setEnabled(hasAny);
}