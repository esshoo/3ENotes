#include "BatchImportDialog.h"

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)  // Desktop only

#include "../ThemeColors.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QFileDialog>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QStandardPaths>
#include <QSettings>
#include <QMessageBox>
#include <QApplication>
#include <QStyle>
#include <QScreen>
#include <QGuiApplication>
#include <QLocale>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QSet>

namespace {

QString defaultLibraryDirectory()
{
    const QString path = QDir::cleanPath(
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/3ENotes"));
    QDir().mkpath(path);
    return path;
}

QString defaultImportsDirectory()
{
    const QString path = QDir::cleanPath(defaultLibraryDirectory() + QStringLiteral("/Imports"));
    QDir().mkpath(path);
    return path;
}

bool isSupportedProjectFile(const QString& filePath)
{
    return filePath.endsWith(QStringLiteral(".3en"), Qt::CaseInsensitive)
        || filePath.endsWith(QStringLiteral(".3enotes"), Qt::CaseInsensitive)
        || filePath.endsWith(QStringLiteral(".snbx"), Qt::CaseInsensitive);
}

QStringList findProjectFiles(const QString& folder)
{
    QStringList files;
    if (folder.isEmpty() || !QDir(folder).exists()) return files;

    QDirIterator it(folder, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (isSupportedProjectFile(path)) files.append(path);
    }
    files.sort(Qt::CaseInsensitive);
    return files;
}

// Auto-discovery deliberately scans only the direct contents of the standard
// drop folders. Recursively polling Documents/3ENotes would also walk every
// extracted .snb notebook and becomes expensive as the library grows.
QStringList findProjectFilesTopLevel(const QString& folder)
{
    QStringList files;
    QDir dir(folder);
    if (!dir.exists()) return files;

    const QFileInfoList entries = dir.entryInfoList(
        QDir::Files | QDir::NoDotAndDotDot,
        QDir::Name | QDir::IgnoreCase);

    for (const QFileInfo& info : entries) {
        const QString path = info.absoluteFilePath();
        if (isSupportedProjectFile(path)) files.append(path);
    }

    return files;
}

bool samePath(const QString& left, const QString& right)
{
    const QString a = QDir::cleanPath(QFileInfo(left).absoluteFilePath());
    const QString b = QDir::cleanPath(QFileInfo(right).absoluteFilePath());
#ifdef Q_OS_WIN
    return a.compare(b, Qt::CaseInsensitive) == 0;
#else
    return a == b;
#endif
}

bool isInsideOrSameDirectory(const QString& filePath, const QString& directoryPath)
{
    if (filePath.isEmpty() || directoryPath.isEmpty()) return false;

    QString file = QDir::cleanPath(QFileInfo(filePath).absoluteFilePath());
    QString dir = QDir::cleanPath(QFileInfo(directoryPath).absoluteFilePath());

#ifdef Q_OS_WIN
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif

    if (file.compare(dir, cs) == 0) return true;

    if (!dir.endsWith(QDir::separator())) {
        dir += QDir::separator();
    }
    return file.startsWith(dir, cs);
}

} // namespace

// ============================================================================
// Constructor
// ============================================================================

BatchImportDialog::BatchImportDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Import 3ENotes Projects"));
    setWindowIcon(QIcon(":/resources/icons/mainicon.svg"));
    setModal(true);
    if (QLocale().textDirection() == Qt::RightToLeft) {
        setLayoutDirection(Qt::RightToLeft);
    }

    setupUi();

    const QString documentsDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString libraryDir = defaultLibraryDirectory();
    const QString importsDir = defaultImportsDirectory();

    // Migrate the old default (Documents root) to Documents/3ENotes, while
    // preserving any genuinely custom destination selected by the user.
    QSettings settings;
    settings.beginGroup(QStringLiteral("BatchImport"));
    QString lastDestDir = settings.value(QStringLiteral("destinationDirectory")).toString();
    if (lastDestDir.isEmpty() || !QDir(lastDestDir).exists() || samePath(lastDestDir, documentsDir)) {
        lastDestDir = libraryDir;
        settings.setValue(QStringLiteral("destinationDirectory"), lastDestDir);
    }

    QString lastBrowseDir = settings.value(QStringLiteral("lastBrowseDirectory")).toString();
    if (lastBrowseDir.isEmpty() || !QDir(lastBrowseDir).exists() || samePath(lastBrowseDir, documentsDir)) {
        lastBrowseDir = importsDir;
        settings.setValue(QStringLiteral("lastBrowseDirectory"), lastBrowseDir);
    }
    settings.endGroup();

    m_destEdit->setText(lastDestDir);

    // Files copied into Documents/3ENotes (including Imports/) are discovered
    // automatically so the user does not have to select the same folder again.
    addFiles(findProjectFiles(libraryDir));
    updateImportButton();

    // 3ENOTES_LIBRARY_WATCH_V1
    // Keep the import dialog live while it is open. QFileSystemWatcher avoids
    // continuously walking the full notebook library; a short debounce also
    // prevents reacting to every chunk written during a large file copy.
    m_watchedLibraryDir = libraryDir;
    m_watchedImportsDir = importsDir;

    m_libraryWatcher = new QFileSystemWatcher(this);
    m_libraryRefreshTimer = new QTimer(this);
    m_libraryRefreshTimer->setSingleShot(true);
    m_libraryRefreshTimer->setInterval(900);

    auto ensureWatchPath = [this](const QString& path) {
        if (!path.isEmpty()
            && QDir(path).exists()
            && !m_libraryWatcher->directories().contains(path)) {
            m_libraryWatcher->addPath(path);
        }
    };

    ensureWatchPath(m_watchedLibraryDir);
    ensureWatchPath(m_watchedImportsDir);

    connect(m_libraryWatcher, &QFileSystemWatcher::directoryChanged,
            this, [this](const QString&) {
                m_libraryRefreshTimer->start();
            });
    connect(m_libraryRefreshTimer, &QTimer::timeout,
            this, &BatchImportDialog::refreshWatchedLibrary);

    setMinimumSize(DIALOG_MIN_WIDTH, DIALOG_MIN_HEIGHT);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    if (parent) {
        move(parent->geometry().center() - rect().center());
    } else if (QScreen* screen = QGuiApplication::primaryScreen()) {
        move(screen->geometry().center() - rect().center());
    }
}

// ============================================================================
// Setup UI
// ============================================================================

void BatchImportDialog::setupUi()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(16);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    
    // ===== Title =====
    m_titleLabel = new QLabel(tr("Select 3ENotes Projects to Import"));
    QFont titleFont = m_titleLabel->font();
    titleFont.setPointSize(16);
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);
    mainLayout->addWidget(m_titleLabel);
    
    // ===== Description =====
    QLabel* descLabel = new QLabel(
        tr("Choose .3EN projects or legacy .3enotes/.snbx files. Files placed in your 3ENotes library are listed automatically, or you can add files and folders manually."));
    descLabel->setWordWrap(true);
    descLabel->setStyleSheet("color: palette(placeholderText); font-size: 13px;");
    mainLayout->addWidget(descLabel);
    
    // ===== File List =====
    QGroupBox* filesGroup = new QGroupBox(tr("Files to Import"));
    QVBoxLayout* filesLayout = new QVBoxLayout(filesGroup);
    filesLayout->setSpacing(8);
    
    // File count label
    m_fileCountLabel = new QLabel(tr("No files selected"));
    m_fileCountLabel->setStyleSheet("color: palette(placeholderText); font-size: 12px;");
    filesLayout->addWidget(m_fileCountLabel);
    
    // File list widget
    m_fileList = new QListWidget();
    m_fileList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_fileList->setAlternatingRowColors(true);
    m_fileList->setMinimumHeight(150);
    filesLayout->addWidget(m_fileList);
    
    // File action buttons
    QHBoxLayout* fileButtonLayout = new QHBoxLayout();
    fileButtonLayout->setSpacing(8);
    
    m_addFilesButton = new QPushButton(tr("Add Files..."));
    m_addFilesButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_FileIcon));
    connect(m_addFilesButton, &QPushButton::clicked, this, &BatchImportDialog::onAddFilesClicked);
    fileButtonLayout->addWidget(m_addFilesButton);
    
    m_addFolderButton = new QPushButton(tr("Add Folder..."));
    m_addFolderButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirIcon));
    connect(m_addFolderButton, &QPushButton::clicked, this, &BatchImportDialog::onAddFolderClicked);
    fileButtonLayout->addWidget(m_addFolderButton);
    
    fileButtonLayout->addStretch();
    
    m_removeSelectedButton = new QPushButton(tr("Remove"));
    m_removeSelectedButton->setEnabled(false);
    connect(m_removeSelectedButton, &QPushButton::clicked, this, &BatchImportDialog::onRemoveSelectedClicked);
    connect(m_fileList, &QListWidget::itemSelectionChanged, this, [this]() {
        m_removeSelectedButton->setEnabled(!m_fileList->selectedItems().isEmpty());
    });
    fileButtonLayout->addWidget(m_removeSelectedButton);
    
    m_clearAllButton = new QPushButton(tr("Clear All"));
    m_clearAllButton->setEnabled(false);
    connect(m_clearAllButton, &QPushButton::clicked, this, &BatchImportDialog::onClearAllClicked);
    fileButtonLayout->addWidget(m_clearAllButton);
    
    filesLayout->addLayout(fileButtonLayout);
    mainLayout->addWidget(filesGroup);
    
    // ===== Destination Library =====
    QGroupBox* destGroup = new QGroupBox(tr("3ENotes Library"));
    QVBoxLayout* destGroupLayout = new QVBoxLayout(destGroup);
    destGroupLayout->setSpacing(6);

    QHBoxLayout* destLayout = new QHBoxLayout();
    destLayout->setSpacing(8);

    m_destEdit = new QLineEdit();
    m_destEdit->setPlaceholderText(tr("Choose the 3ENotes library folder..."));
    m_destEdit->setReadOnly(true);
    destLayout->addWidget(m_destEdit, 1);

    m_destBrowseButton = new QPushButton(tr("Browse..."));
    connect(m_destBrowseButton, &QPushButton::clicked, this, &BatchImportDialog::onBrowseDestClicked);
    destLayout->addWidget(m_destBrowseButton);

    QPushButton* resetDestButton = new QPushButton(tr("Default"));
    connect(resetDestButton, &QPushButton::clicked, this, [this]() {
        const QString libraryDir = defaultLibraryDirectory();
        m_destEdit->setText(libraryDir);
        QSettings settings;
        settings.beginGroup(QStringLiteral("BatchImport"));
        settings.setValue(QStringLiteral("destinationDirectory"), libraryDir);
        settings.endGroup();
        updateImportButton();
    });
    destLayout->addWidget(resetDestButton);
    destGroupLayout->addLayout(destLayout);

    QLabel* destNote = new QLabel(
        tr("Imported notebooks are extracted into this library. The project files selected above remain in their original folders."));
    destNote->setWordWrap(true);
    destNote->setStyleSheet("color: palette(placeholderText); font-size: 11px;");
    destGroupLayout->addWidget(destNote);

    mainLayout->addWidget(destGroup);
    
    // ===== Buttons =====
    mainLayout->addStretch();
    
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(12);
    buttonLayout->addStretch();
    
    m_cancelButton = new QPushButton(tr("Cancel"));
    m_cancelButton->setMinimumSize(100, 36);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    buttonLayout->addWidget(m_cancelButton);
    
    m_importButton = new QPushButton(tr("Import"));
    m_importButton->setMinimumSize(100, 36);
    m_importButton->setDefault(true);
    m_importButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_DialogApplyButton));
    connect(m_importButton, &QPushButton::clicked, this, &BatchImportDialog::onImportClicked);
    buttonLayout->addWidget(m_importButton);
    
    mainLayout->addLayout(buttonLayout);
}

// ============================================================================
// Slots
// ============================================================================

void BatchImportDialog::onAddFilesClicked()
{
    QSettings settings;
    settings.beginGroup("BatchImport");
    QString lastDir = settings.value("lastBrowseDirectory").toString();
    settings.endGroup();
    
    if (lastDir.isEmpty() || !QDir(lastDir).exists()) {
        lastDir = defaultImportsDirectory();
    }
    
    QStringList files = QFileDialog::getOpenFileNames(
        this,
        tr("Select Notebook Files"),
        lastDir,
        tr("3ENotes Projects (*.3EN *.3en *.3enotes *.snbx);;All Files (*)")
    );
    
    if (!files.isEmpty()) {
        // Save last browse directory
        settings.beginGroup("BatchImport");
        settings.setValue("lastBrowseDirectory", QFileInfo(files.first()).absolutePath());
        settings.endGroup();
        
        addFiles(files);
    }
}

void BatchImportDialog::onAddFolderClicked()
{
    QSettings settings;
    settings.beginGroup("BatchImport");
    QString lastDir = settings.value("lastBrowseDirectory").toString();
    settings.endGroup();
    
    if (lastDir.isEmpty() || !QDir(lastDir).exists()) {
        lastDir = defaultImportsDirectory();
    }
    
    QString folder = QFileDialog::getExistingDirectory(
        this,
        tr("Select Folder to Scan"),
        lastDir,
        QFileDialog::ShowDirsOnly
    );
    
    if (!folder.isEmpty()) {
        // Save last browse directory
        settings.beginGroup("BatchImport");
        settings.setValue("lastBrowseDirectory", folder);
        settings.endGroup();
        
        const QStringList foundFiles = findProjectFiles(folder);

        if (foundFiles.isEmpty()) {
            QMessageBox::information(this, tr("No Projects Found"),
                tr("No .3EN, .3enotes, or .snbx files were found in the selected folder."));
        } else {
            addFiles(foundFiles);
        }
    }
}

void BatchImportDialog::onRemoveSelectedClicked()
{
    QList<QListWidgetItem*> selected = m_fileList->selectedItems();
    for (QListWidgetItem* item : selected) {
        QString filePath = item->data(Qt::UserRole).toString();
        m_selectedFiles.removeOne(filePath);
        delete item;
    }
    updateFileCount();
    updateImportButton();
}

void BatchImportDialog::onClearAllClicked()
{
    m_fileList->clear();
    m_selectedFiles.clear();
    updateFileCount();
    updateImportButton();
}

void BatchImportDialog::onBrowseDestClicked()
{
    QString currentDir = m_destEdit->text();
    if (currentDir.isEmpty() || !QDir(currentDir).exists()) {
        currentDir = defaultLibraryDirectory();
    }
    
    QString folder = QFileDialog::getExistingDirectory(
        this,
        tr("Select 3ENotes Library Folder"),
        currentDir,
        QFileDialog::ShowDirsOnly
    );
    
    if (!folder.isEmpty()) {
        m_destEdit->setText(folder);
        QSettings settings;
        settings.beginGroup(QStringLiteral("BatchImport"));
        settings.setValue(QStringLiteral("destinationDirectory"), folder);
        settings.endGroup();
        updateImportButton();
    }
}

void BatchImportDialog::onImportClicked()
{
    // Save settings
    QSettings settings;
    settings.beginGroup("BatchImport");
    settings.setValue("destinationDirectory", destinationDirectory());
    settings.endGroup();
    
    accept();
}

// ============================================================================
// Live library auto-discovery
// ============================================================================

void BatchImportDialog::refreshWatchedLibrary()
{
    if (!m_libraryWatcher || !m_libraryRefreshTimer) {
        return;
    }

    // QFileSystemWatcher stops watching a directory if it is removed.
    // Re-add the standard paths when they exist again.
    for (const QString& path : {m_watchedLibraryDir, m_watchedImportsDir}) {
        if (!path.isEmpty()
            && QDir(path).exists()
            && !m_libraryWatcher->directories().contains(path)) {
            m_libraryWatcher->addPath(path);
        }
    }

    QStringList foundFiles = findProjectFilesTopLevel(m_watchedLibraryDir);
    const QStringList importFiles = findProjectFilesTopLevel(m_watchedImportsDir);
    for (const QString& file : importFiles) {
        if (!foundFiles.contains(file)) {
            foundFiles.append(file);
        }
    }

    QSet<QString> foundAbsolutePaths;
    QStringList stableNewFiles;
    bool needsStabilityRecheck = false;

    for (const QString& file : foundFiles) {
        const QFileInfo info(file);
        const QString absolutePath =
            QDir::cleanPath(info.absoluteFilePath());
        foundAbsolutePaths.insert(absolutePath);

        if (isDuplicate(absolutePath)) {
            // Already visible in the dialog; no need to keep size history.
            m_observedProjectSizes.remove(absolutePath);
            continue;
        }

        const qint64 currentSize = info.size();
        const qint64 previousSize =
            m_observedProjectSizes.value(absolutePath, -1);
        m_observedProjectSizes.insert(absolutePath, currentSize);

        // Require the same non-zero size on two scans. This avoids presenting
        // a .3EN file while Windows/macOS/Linux is still copying it.
        if (currentSize > 0 && previousSize == currentSize) {
            stableNewFiles.append(absolutePath);
            m_observedProjectSizes.remove(absolutePath);
        } else {
            needsStabilityRecheck = true;
        }
    }

    // If a watched project file was removed/moved while this dialog was open,
    // remove the stale entry automatically. Manually-selected external files
    // are left untouched.
    for (int i = m_selectedFiles.size() - 1; i >= 0; --i) {
        const QString selectedPath = m_selectedFiles.at(i);
        const bool watched =
            isInsideOrSameDirectory(selectedPath, m_watchedLibraryDir)
            || isInsideOrSameDirectory(selectedPath, m_watchedImportsDir);

        if (!watched || QFileInfo::exists(selectedPath)) {
            continue;
        }

        for (int row = m_fileList->count() - 1; row >= 0; --row) {
            QListWidgetItem* item = m_fileList->item(row);
            if (item && samePath(item->data(Qt::UserRole).toString(), selectedPath)) {
                delete m_fileList->takeItem(row);
            }
        }
        m_selectedFiles.removeAt(i);
    }

    // Forget size observations for files that disappeared.
    const QList<QString> observedPaths = m_observedProjectSizes.keys();
    for (const QString& observed : observedPaths) {
        if (!foundAbsolutePaths.contains(observed)) {
            m_observedProjectSizes.remove(observed);
        }
    }

    if (!stableNewFiles.isEmpty()) {
        // Only genuinely new files are passed to addFiles(), so its duplicate
        // warning is never triggered by the automatic refresh.
        addFiles(stableNewFiles);
    } else {
        updateFileCount();
        updateImportButton();
    }

    if (needsStabilityRecheck) {
        m_libraryRefreshTimer->start(1000);
    }
}
void BatchImportDialog::updateImportButton()
{
    bool canImport = !m_selectedFiles.isEmpty() && !destinationDirectory().isEmpty();
    m_importButton->setEnabled(canImport);
    m_clearAllButton->setEnabled(!m_selectedFiles.isEmpty());
}

// ============================================================================
// Helpers
// ============================================================================

void BatchImportDialog::addFiles(const QStringList& files)
{
    int addedCount = 0;
    int duplicateCount = 0;
    
    for (const QString& file : files) {
        if (!isSupportedProjectFile(file)) {
            continue;
        }
        
        // Skip duplicates
        if (isDuplicate(file)) {
            duplicateCount++;
            continue;
        }
        
        m_selectedFiles.append(file);
        
        // Add to list widget
        QString displayName = extractDisplayName(file);
        QListWidgetItem* item = new QListWidgetItem(displayName);
        item->setData(Qt::UserRole, file);
        item->setToolTip(file);
        m_fileList->addItem(item);
        
        addedCount++;
    }
    
    updateFileCount();
    updateImportButton();
    
    // Show duplicate warning if any
    if (duplicateCount > 0) {
        QString msg = duplicateCount == 1
            ? tr("1 file was already in the list and was skipped.")
            : tr("%1 files were already in the list and were skipped.").arg(duplicateCount);
        QMessageBox::information(this, tr("Duplicates Skipped"), msg);
    }
}

void BatchImportDialog::updateFileCount()
{
    int count = m_selectedFiles.size();
    if (count == 0) {
        m_fileCountLabel->setText(tr("No files selected"));
    } else if (count == 1) {
        m_fileCountLabel->setText(tr("1 file selected"));
    } else {
        m_fileCountLabel->setText(tr("%1 files selected").arg(count));
    }
}

bool BatchImportDialog::isDuplicate(const QString& filePath) const
{
    // Check by absolute path
    QString absPath = QFileInfo(filePath).absoluteFilePath();
    for (const QString& existing : m_selectedFiles) {
        if (QFileInfo(existing).absoluteFilePath() == absPath) {
            return true;
        }
    }
    return false;
}

QString BatchImportDialog::extractDisplayName(const QString& filePath) const
{
    QFileInfo info(filePath);
    QString name = info.completeBaseName();  // Filename without the project extension
    
    // Add parent folder for context
    QString parentDir = info.dir().dirName();
    if (!parentDir.isEmpty() && parentDir != ".") {
        return QString("%1  (%2)").arg(name, parentDir);
    }
    return name;
}

QString BatchImportDialog::destinationDirectory() const
{
    return m_destEdit->text().trimmed();
}

void BatchImportDialog::setDarkMode(bool dark)
{
    m_darkMode = dark;
    // Theme is applied via parent's palette
}

// ============================================================================
// Static Methods
// ============================================================================

QStringList BatchImportDialog::getImportFiles(QWidget* parent, QString* destDir)
{
    BatchImportDialog dialog(parent);
    
    if (dialog.exec() == QDialog::Accepted) {
        if (destDir) {
            *destDir = dialog.destinationDirectory();
        }
        return dialog.selectedFiles();
    }
    
    return QStringList();
}

#endif // !Q_OS_ANDROID && !Q_OS_IOS
