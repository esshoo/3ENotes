// ============================================================================
// DocumentManager Implementation
// ============================================================================
// Part of the SpeedyNote document architecture (Phase 3.0.1)
// ============================================================================

#include "DocumentManager.h"
#include "Document.h"
#include "NotebookLibrary.h"
#include "../sharing/NotebookImporter.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUuid>
#include <QDateTime>
#include <QRegularExpression>
#include <QDebug>
#include <algorithm>

// Settings key for recent documents persistence
const QString DocumentManager::SETTINGS_RECENT_KEY = QStringLiteral("RecentDocuments");

// Temp bundle prefixes for unsaved documents
const QString DocumentManager::TEMP_EDGELESS_PREFIX = QStringLiteral("speedynote_edgeless_");
const QString DocumentManager::TEMP_PAGED_PREFIX = QStringLiteral("speedynote_paged_");

// ============================================================================
// Constructor / Destructor
// ============================================================================

DocumentManager::DocumentManager(QObject* parent)
    : QObject(parent)
{
    // 3ENOTES_AUTOSAVE_RECOVERY_V1
    // A marker survives abnormal termination. A normal destructor removes it.
    const QString markerPath = recoverySessionMarkerPath();
    m_uncleanShutdownDetected = QFileInfo::exists(markerPath);

    QFile markerFile(markerPath);
    if (markerFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        markerFile.write(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toUtf8());
        markerFile.close();
    } else {
        qWarning() << "DocumentManager: Could not create recovery session marker:" << markerPath;
    }

    loadRecentFromSettings();
}

DocumentManager::~DocumentManager()
{
    // Clean up temp bundles and delete all owned documents
    for (Document* doc : m_documents) {
        // Clean up temp bundle if exists (handles discarded edgeless docs)
        cleanupTempBundle(doc);
        
        // Phase C.0.4: Clean up orphaned assets before deletion
        // This is the same cleanup that closeDocument() does, but for
        // documents still open when the application quits.
        doc->cleanupOrphanedAssets();
        
        delete doc;
    }
    m_documents.clear();
    m_tempBundlePaths.clear();
    m_lastRecoverySnapshotMs.clear();

    // Mark this session as clean only after normal shutdown reaches the manager destructor.
    QFile::remove(recoverySessionMarkerPath());
}

// ============================================================================
// Document Lifecycle
// ============================================================================

Document* DocumentManager::createDocument(const QString& name)
{
    // 3ENOTES_CANONICAL_NEW_NOTE_FACTORY_V1
    //
    // Product invariant:
    // Generic New Document / New Note == Infinite Canvas.
    // A bounded blank sheet must be requested explicitly.
    return createEdgelessDocument(
        name.isEmpty() ? tr("Untitled Note") : name
    );
}

Document* DocumentManager::createPagedDocument(const QString& name)
{
    // Explicit fixed-page document.
    auto docPtr = Document::createNew(
        name.isEmpty() ? tr("Untitled Page") : name,
        Document::Mode::Paged
    );

    if (!docPtr) {
        qWarning() << "DocumentManager::createPagedDocument: Failed to create document";
        return nullptr;
    }

    Document* doc = docPtr.release();

    m_documents.append(doc);
    m_documentPaths[doc] = QString();
    m_modifiedFlags[doc] = false;

    emit documentCreated(doc);
    return doc;
}

Document* DocumentManager::createEdgelessDocument(const QString& name)
{
    // Create a new edgeless (infinite canvas) document
    auto docPtr = Document::createNew(
        name.isEmpty() ? tr("Untitled Canvas") : name,
        Document::Mode::Edgeless
    );
    
    if (!docPtr) {
        qWarning() << "DocumentManager::createEdgelessDocument: Failed to create document";
        return nullptr;
    }
    
    Document* doc = docPtr.release();  // Transfer ownership to DocumentManager
    
    m_documents.append(doc);
    m_documentPaths[doc] = QString();  // New document has no path yet
    m_modifiedFlags[doc] = false;      // New document is not modified
    
    // ========== TEMP BUNDLE CREATION (A3: Create immediately) ==========
    // Create a temp .snb bundle directory immediately to enable tile eviction.
    // This prevents unbounded memory growth for unsaved edgeless canvases.
    QString tempPath = createTempBundlePath(doc);
    if (!tempPath.isEmpty()) {
        m_tempBundlePaths[doc] = tempPath;
        
        // CRITICAL: Call saveBundle() to:
        // 1. Write document.json manifest
        // 2. Set m_lazyLoadEnabled = true (enables evictDistantTiles())
        // Without this, eviction won't work and memory will grow unbounded.
        if (doc->saveBundle(tempPath)) {
#ifdef SPEEDYNOTE_DEBUG
            qDebug() << "DocumentManager: Initialized temp bundle at" << tempPath;
#endif
        } else {
            qWarning() << "DocumentManager: Failed to initialize temp bundle, tile eviction disabled";
        }
    } else {
        qWarning() << "DocumentManager: Failed to create temp bundle dir, tile eviction disabled";
    }
    // ====================================================================
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "DocumentManager: Created edgeless document" << doc->name;
#endif
    
    emit documentCreated(doc);
    return doc;
}

Document* DocumentManager::loadDocument(const QString& path)
{
    if (path.isEmpty()) {
        qWarning() << "DocumentManager::loadDocument: Empty path";
        return nullptr;
    }
    
    QFileInfo fileInfo(path);
    if (!fileInfo.exists()) {
        qWarning() << "DocumentManager::loadDocument: File does not exist:" << path;
        return nullptr;
    }
    
    QString suffix = fileInfo.suffix().toLower();
    
    // Handle PDF files - create document for PDF annotation
    if (suffix == "pdf") {
        auto docPtr = Document::createForPdf(fileInfo.baseName(), path);
        if (!docPtr) {
            qWarning() << "DocumentManager::loadDocument: Failed to load PDF:" << path;
            return nullptr;
        }
        
        Document* doc = docPtr.release();
        m_documents.append(doc);
        m_documentPaths[doc] = QString();  // PDF-based doc has no .snx path yet
        m_modifiedFlags[doc] = false;
        
        addToRecent(path);
        emit documentLoaded(doc);
        return doc;
    }
    
    // Handle .snbx packages - extract and load the contained notebook
    if (suffix == "snbx" || suffix == "3enotes" || suffix == "3en") {
        // Determine extraction destination
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
        // On Android/iOS, extract to app-private notebooks directory
        QString destDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/notebooks";
#else
        // On desktop, extract next to the .snbx file
        QString destDir = fileInfo.absolutePath();
#endif
        
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "DocumentManager: Importing 3ENotes package" << path << "to" << destDir;
#endif
        
        auto importResult = NotebookImporter::importPackage(path, destDir);
        if (!importResult.success) {
            qWarning() << "DocumentManager::loadDocument: Failed to import package:" << importResult.errorMessage;
            return nullptr;
        }
        
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "DocumentManager: Extracted to" << importResult.extractedSnbPath;
        if (!importResult.embeddedPdfPath.isEmpty()) {
            qDebug() << "DocumentManager: Embedded PDF at" << importResult.embeddedPdfPath;
        }
#endif
        
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
        // Clean up the source .snbx file from /imports/ directory
        // This prevents disk space leaks from accumulated imports
        // Note: Only do this on Android/iOS where we control the import copy location
        QString appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QString importsDir = appDataDir + "/imports";
        if (path.startsWith(importsDir)) {
            QFile::remove(path);
#ifdef SPEEDYNOTE_DEBUG
            qDebug() << "DocumentManager: Cleaned up imported package file:" << path;
#endif
        }
#endif
        
        // Recursively load the extracted .snb bundle
        // The dual-path system in Document::loadBundle() will resolve the PDF
        return loadDocument(importResult.extractedSnbPath);
    }
    
    // Handle .snb bundle directories - edgeless documents with O(1) tile loading
    if (suffix == "snb" || fileInfo.isDir()) {
        // Check if it's a valid bundle (has document.json)
        QString manifestPath = path + "/document.json";
        if (!QFile::exists(manifestPath)) {
            if (suffix == "snb") {
                qWarning() << "DocumentManager::loadDocument: Invalid bundle (no manifest):" << path;
                return nullptr;
            }
            // Not a bundle directory, fall through to other handlers
        } else {
            auto docPtr = Document::loadBundle(path);
            if (!docPtr) {
                qWarning() << "DocumentManager::loadDocument: Failed to load bundle:" << path;
                return nullptr;
            }
            
            Document* doc = docPtr.release();
            m_documents.append(doc);
            m_documentPaths[doc] = path;
            m_modifiedFlags[doc] = false;
            
            addToRecent(path);
            
            // Phase P.2.8: Add to NotebookLibrary for launcher
            NotebookLibrary::instance()->addToRecent(path);
            
            emit documentLoaded(doc);
            return doc;
        }
    }
    
    qWarning() << "DocumentManager::loadDocument: Unsupported file format:" << suffix;
    return nullptr;
}

bool DocumentManager::saveDocument(Document* doc)
{
    if (!doc) {
        qWarning() << "DocumentManager::saveDocument: Null document";
        return false;
    }
    
    QString path = documentPath(doc);
    if (path.isEmpty()) {
        qWarning() << "DocumentManager::saveDocument: Document has no path, use saveDocumentAs";
        return false;
    }
    
    return doSave(doc, path);
}

bool DocumentManager::saveDocumentAs(Document* doc, const QString& path)
{
    if (!doc) {
        qWarning() << "DocumentManager::saveDocumentAs: Null document";
        return false;
    }
    
    if (path.isEmpty()) {
        qWarning() << "DocumentManager::saveDocumentAs: Empty path";
        return false;
    }
    
    if (doSave(doc, path)) {
        m_documentPaths[doc] = path;
        return true;
    }
    
    return false;
}

void DocumentManager::closeDocument(Document* doc)
{
    if (!doc) {
        return;
    }
    
    qsizetype index = m_documents.indexOf(doc);
    if (index < 0) {
        qWarning() << "DocumentManager::closeDocument: Document not found";
        return;
    }
    
#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "DocumentManager::closeDocument: Closing document" << doc 
             << "remaining=" << (m_documents.size() - 1);
#endif
    
    // Emit signal before deletion so receivers can clean up
    // Phase P.2.8: MainWindow should connect to this signal to save thumbnail
    // via NotebookLibrary::instance()->saveThumbnail(path, thumbnail)
    emit documentClosed(doc);
    
    // ========== TEMP BUNDLE CLEANUP ==========
    // If document was using a temp bundle and user didn't save,
    // clean up the temp directory to prevent storage space leak.
    // Note: If user saved to a permanent location, cleanupTempBundle()
    // was already called in doSave(), so this is a no-op.
    cleanupTempBundle(doc);
    // ==========================================
    
    // Remove from collections
    m_documents.removeAt(index);
    m_documentPaths.remove(doc);
    m_modifiedFlags.remove(doc);
    // Note: m_tempBundlePaths already cleaned by cleanupTempBundle()
    
    // Phase C.0.4: Clean up orphaned assets before deletion
    // This deletes image files that are no longer referenced by any object.
    doc->cleanupOrphanedAssets();
    
    // Delete the document
    delete doc;
}

// ============================================================================
// Document Access
// ============================================================================

Document* DocumentManager::documentAt(int index) const
{
    if (index < 0 || index >= m_documents.size()) {
        return nullptr;
    }
    return m_documents.at(index);
}

int DocumentManager::documentCount() const
{
    return static_cast<int>(m_documents.size());
}

int DocumentManager::indexOf(Document* doc) const
{
    return static_cast<int>(m_documents.indexOf(doc));
}

// ============================================================================
// Document State
// ============================================================================

bool DocumentManager::hasUnsavedChanges(Document* doc) const
{
    if (!doc) {
        return false;
    }
    return m_modifiedFlags.value(doc, false) || doc->modified;
}

QString DocumentManager::documentPath(Document* doc) const
{
    if (!doc) {
        return QString();
    }
    return m_documentPaths.value(doc);
}

void DocumentManager::setDocumentPath(Document* doc, const QString& path)
{
    if (!doc || !m_documents.contains(doc)) {
        return;
    }
    
    QString oldPath = m_documentPaths.value(doc);
    if (oldPath != path) {
        m_documentPaths[doc] = path;
        
        // Also update the document's internal bundle path if it's a .snb bundle
        if (path.endsWith(".snb", Qt::CaseInsensitive) || QFileInfo(path).isDir()) {
            doc->setBundlePath(path);
        }
    }
}

void DocumentManager::markModified(Document* doc)
{
    if (!doc || !m_documents.contains(doc)) {
        return;
    }
    
    bool wasModified = m_modifiedFlags.value(doc, false);
    m_modifiedFlags[doc] = true;
    doc->markModified();
    
    if (!wasModified) {
        emit documentModified(doc);
    }
}

void DocumentManager::clearModified(Document* doc)
{
    if (!doc || !m_documents.contains(doc)) {
        return;
    }
    
    m_modifiedFlags[doc] = false;
    doc->clearModified();
}

// ============================================================================
// Recent Documents
// ============================================================================

QStringList DocumentManager::recentDocuments() const
{
    return m_recentPaths;
}

void DocumentManager::addToRecent(const QString& path)
{
    if (path.isEmpty()) {
        return;
    }
    
    // Remove existing entry (if any) to move it to front
    m_recentPaths.removeAll(path);
    
    // Add to front
    m_recentPaths.prepend(path);
    
    // Trim to max size
    while (m_recentPaths.size() > MAX_RECENT) {
        m_recentPaths.removeLast();
    }
    
    saveRecentToSettings();
    emit recentDocumentsChanged();
}

void DocumentManager::clearRecentDocuments()
{
    if (m_recentPaths.isEmpty()) {
        return;
    }
    
    m_recentPaths.clear();
    saveRecentToSettings();
    emit recentDocumentsChanged();
}

void DocumentManager::removeFromRecent(const QString& path)
{
    if (m_recentPaths.removeAll(path) > 0) {
        saveRecentToSettings();
        emit recentDocumentsChanged();
    }
}

// ============================================================================
// Private Methods
// ============================================================================

void DocumentManager::loadRecentFromSettings()
{
    QSettings settings;
    m_recentPaths = settings.value(SETTINGS_RECENT_KEY).toStringList();
    
    // Validate paths - remove non-existent files
    QStringList validPaths;
    for (const QString& path : m_recentPaths) {
        if (QFileInfo::exists(path)) {
            validPaths.append(path);
        }
    }
    
    if (validPaths.size() != m_recentPaths.size()) {
        m_recentPaths = validPaths;
        saveRecentToSettings();
    }
}

void DocumentManager::saveRecentToSettings()
{
    QSettings settings;
    settings.setValue(SETTINGS_RECENT_KEY, m_recentPaths);
}

bool DocumentManager::doSave(Document* doc, const QString& path)
{
    if (!doc || path.isEmpty()) {
        return false;
    }
    
    // ========== UNIFIED BUNDLE FORMAT (.snb) - Phase O1.7.6 ==========
    // ALL documents (paged and edgeless) now use the bundle format.
    // This enables:
    // - Lazy loading for paged mode (pages loaded on demand)
    // - Asset folder for images/objects
    // - Consistent O(1) save/load for large documents
    
    QString bundlePath = path;
    // Ensure .snb extension
    if (!bundlePath.endsWith(".snb", Qt::CaseInsensitive)) {
        bundlePath += ".snb";
    }
    
    if (QDir(bundlePath).exists()) {
        if (!createRecoverySnapshot(doc, bundlePath)) {
            qWarning() << "DocumentManager: Could not create pre-save recovery snapshot for"
                       << bundlePath;
        }
    }

    if (!doc->saveBundle(bundlePath)) {
        qWarning() << "DocumentManager::doSave: Failed to save bundle:" << bundlePath;
        return false;
    }
    
    // ========== TEMP BUNDLE CLEANUP ==========
    // If this was a temp bundle and now saving to a different location,
    // clean up the temp directory. Note: saveBundle() already updated
    // m_bundlePath to the new location, so no need to call setBundlePath().
    QString tempPath = m_tempBundlePaths.value(doc);
    if (!tempPath.isEmpty() && tempPath != bundlePath) {
        cleanupTempBundle(doc);
#ifdef SPEEDYNOTE_DEBUG
        qDebug() << "DocumentManager: Moved from temp bundle to" << bundlePath;
#endif
    }
    // ==========================================
    
    // Update state
    clearModified(doc);
    addToRecent(bundlePath);
    
    // Phase P.2.8: Update NotebookLibrary for launcher
    NotebookLibrary::instance()->addToRecent(bundlePath);
    
    emit documentSaved(doc);
    return true;
}

// ============================================================================
// Temp Bundle Management (Edgeless/Paged Auto-save)
// ============================================================================

QString DocumentManager::createTempBundlePath(Document* doc)
{
    if (!doc) {
        return QString();
    }
    
    // Create temp directory path for unsaved documents:
    // QStandardPaths::TempLocation + prefix + uuid
    QString tempBase = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QString uuid = doc->id.left(8);  // Use first 8 chars of doc ID for uniqueness
    QString prefix = doc->isEdgeless() ? TEMP_EDGELESS_PREFIX : TEMP_PAGED_PREFIX;
    QString tempPath = tempBase + "/" + prefix + uuid + ".snb";
    
    // Create the directory
    QDir dir;
    if (!dir.mkpath(tempPath)) {
        qWarning() << "DocumentManager: Failed to create temp bundle directory:" << tempPath;
        return QString();
    }
    
    // Create subdirectories based on document type
    bool subdirOk = false;
    if (doc->isEdgeless()) {
        // Edgeless needs tiles subdirectory
        subdirOk = dir.mkpath(tempPath + "/tiles");
        if (!subdirOk) {
            qWarning() << "DocumentManager: Failed to create tiles subdirectory:" << tempPath + "/tiles";
        }
    } else {
        // Paged needs pages subdirectory
        subdirOk = dir.mkpath(tempPath + "/pages");
        if (!subdirOk) {
            qWarning() << "DocumentManager: Failed to create pages subdirectory:" << tempPath + "/pages";
        }
    }
    
    // If subdirectory creation failed, clean up the parent directory to avoid disk space leak
    if (!subdirOk) {
        QDir(tempPath).removeRecursively();
        return QString();
    }
    
    return tempPath;
}

// ============================================================================
// 3ENotes Auto Save / Crash Recovery
// ============================================================================

QString DocumentManager::recoveryRootPath() const
{
    const QString root =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/recovery");
    QDir().mkpath(root);
    return root;
}

QString DocumentManager::recoverySessionMarkerPath() const
{
    return recoveryRootPath() + QStringLiteral("/session.active");
}

bool DocumentManager::copyDirectoryRecursively(
    const QString& sourcePath,
    const QString& destinationPath) const
{
    QDir source(sourcePath);
    if (!source.exists()) {
        return false;
    }

    if (!QDir().mkpath(destinationPath)) {
        return false;
    }

    const QFileInfoList entries = source.entryInfoList(
        QDir::NoDotAndDotDot | QDir::AllEntries,
        QDir::DirsFirst | QDir::Name);

    for (const QFileInfo& entry : entries) {
        const QString sourceItem = entry.absoluteFilePath();
        const QString destinationItem =
            QDir(destinationPath).filePath(entry.fileName());

        if (entry.isDir()) {
            if (!copyDirectoryRecursively(sourceItem, destinationItem)) {
                return false;
            }
        } else if (entry.isFile()) {
            QFile::remove(destinationItem);
            if (!QFile::copy(sourceItem, destinationItem)) {
                qWarning() << "DocumentManager: Recovery copy failed:"
                           << sourceItem << "->" << destinationItem;
                return false;
            }
        }
    }

    return true;
}

void DocumentManager::pruneRecoverySnapshots(
    const QString& documentRecoveryRoot,
    int keepCount) const
{
    QDir dir(documentRecoveryRoot);
    if (!dir.exists()) {
        return;
    }

    QFileInfoList snapshots = dir.entryInfoList(
        QStringList() << QStringLiteral("*.snb"),
        QDir::Dirs | QDir::NoDotAndDotDot,
        QDir::Time);

    std::sort(snapshots.begin(), snapshots.end(),
              [](const QFileInfo& a, const QFileInfo& b) {
                  return a.lastModified() > b.lastModified();
              });

    for (int i = qMax(keepCount, 0); i < snapshots.size(); ++i) {
        QDir(snapshots.at(i).absoluteFilePath()).removeRecursively();
    }
}

bool DocumentManager::createRecoverySnapshot(
    Document* doc,
    const QString& bundlePath)
{
    if (!doc || bundlePath.isEmpty() || !QDir(bundlePath).exists()) {
        return false;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 lastMs = m_lastRecoverySnapshotMs.value(doc, 0);

    QSettings recoverySettings(QStringLiteral("3E"), QStringLiteral("3ENotes"));
    const int snapshotIntervalMs = qBound(
        10000,
        recoverySettings.value(QStringLiteral("recovery/snapshotIntervalMs"), 60000).toInt(),
        3600000);

    // Full bundle copies are throttled separately from the lightweight Auto Save.
    if (lastMs > 0 && (nowMs - lastMs) < snapshotIntervalMs) {
        return true;
    }

    QString documentKey = doc->id;
    documentKey.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")),
                        QStringLiteral("_"));
    if (documentKey.isEmpty()) {
        documentKey = QStringLiteral("document");
    }

    const QString documentRoot =
        recoveryRootPath() + QStringLiteral("/") + documentKey;
    QDir().mkpath(documentRoot);

    const QString snapshotName =
        QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"))
        + QStringLiteral(".snb");
    const QString snapshotPath =
        QDir(documentRoot).filePath(snapshotName);

    if (QDir(snapshotPath).exists()) {
        QDir(snapshotPath).removeRecursively();
    }

    if (!copyDirectoryRecursively(bundlePath, snapshotPath)) {
        QDir(snapshotPath).removeRecursively();
        return false;
    }

    QJsonObject metadata;
    metadata[QStringLiteral("document_name")] = doc->name;
    metadata[QStringLiteral("document_id")] = doc->id;
    metadata[QStringLiteral("original_path")] = m_documentPaths.value(doc);
    metadata[QStringLiteral("snapshot_utc")] =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    metadata[QStringLiteral("recovery_format_version")] = 1;

    QFile metadataFile(snapshotPath + QStringLiteral("/recovery.json"));
    if (metadataFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        metadataFile.write(QJsonDocument(metadata).toJson(QJsonDocument::Indented));
        metadataFile.close();
    }

    m_lastRecoverySnapshotMs[doc] = nowMs;
    const int keepCount = qBound(
        1,
        recoverySettings.value(QStringLiteral("recovery/keepCount"), 5).toInt(),
        20);
    pruneRecoverySnapshots(documentRoot, keepCount);

#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "DocumentManager: Created recovery snapshot:" << snapshotPath;
#endif
    return true;
}

QStringList DocumentManager::recoverySnapshots() const
{
    QStringList result;
    QFileInfoList snapshotInfos;

    const QDir root(recoveryRootPath());
    const QFileInfoList documentDirs = root.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot,
        QDir::Name);

    for (const QFileInfo& documentDirInfo : documentDirs) {
        QDir documentDir(documentDirInfo.absoluteFilePath());
        const QFileInfoList snapshots = documentDir.entryInfoList(
            QStringList() << QStringLiteral("*.snb"),
            QDir::Dirs | QDir::NoDotAndDotDot,
            QDir::Time);
        snapshotInfos.append(snapshots);
    }

    std::sort(snapshotInfos.begin(), snapshotInfos.end(),
              [](const QFileInfo& a, const QFileInfo& b) {
                  return a.lastModified() > b.lastModified();
              });

    for (const QFileInfo& info : snapshotInfos) {
        result.append(info.absoluteFilePath());
    }
    return result;
}

QString DocumentManager::latestRecoverySnapshot() const
{
    const QStringList snapshots = recoverySnapshots();
    return snapshots.isEmpty() ? QString() : snapshots.first();
}
// 3ENOTES_RECOVERY_CENTER_V2
QString DocumentManager::recoveryOriginalPath(const QString& snapshotPath) const
{
    QFile metadataFile(QDir(snapshotPath).filePath(QStringLiteral("recovery.json")));
    if (!metadataFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }

    const QJsonDocument json = QJsonDocument::fromJson(metadataFile.readAll());
    metadataFile.close();
    if (!json.isObject()) {
        return QString();
    }

    return QDir::cleanPath(
        json.object().value(QStringLiteral("original_path")).toString());
}

bool DocumentManager::deleteRecoverySnapshot(const QString& snapshotPath)
{
    if (snapshotPath.isEmpty()) {
        return false;
    }

    const QString root = QDir::cleanPath(QFileInfo(recoveryRootPath()).absoluteFilePath());
    const QString candidate = QDir::cleanPath(QFileInfo(snapshotPath).absoluteFilePath());

#ifdef Q_OS_WIN
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif

    const QString rootPrefix = root + QDir::separator();
    if (!candidate.startsWith(rootPrefix, cs) || candidate.compare(root, cs) == 0) {
        qWarning() << "DocumentManager: Refusing to delete recovery path outside recovery root:"
                   << candidate;
        return false;
    }

    return QDir(candidate).removeRecursively();
}

int DocumentManager::clearRecoverySnapshots()
{
    int removed = 0;
    QDir root(recoveryRootPath());
    const QFileInfoList documentDirs = root.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot,
        QDir::Name);

    for (const QFileInfo& info : documentDirs) {
        if (QDir(info.absoluteFilePath()).removeRecursively()) {
            ++removed;
        }
    }
    return removed;
}

bool DocumentManager::restoreRecoverySnapshot(
    const QString& snapshotPath,
    QString* restoredPath,
    QString* errorMessage)
{
    auto fail = [&](const QString& message) {
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    };

    if (restoredPath) {
        restoredPath->clear();
    }
    if (errorMessage) {
        errorMessage->clear();
    }

    if (snapshotPath.isEmpty() ||
        !QDir(snapshotPath).exists() ||
        !QFileInfo::exists(QDir(snapshotPath).filePath(QStringLiteral("document.json")))) {
        return fail(tr("The selected recovery copy is incomplete or invalid."));
    }

    const QString targetPath = recoveryOriginalPath(snapshotPath);
    if (targetPath.isEmpty()) {
        return fail(tr("The recovery copy does not contain its original project path."));
    }

    auto normalized = [](const QString& p) {
        return QDir::cleanPath(QFileInfo(p).absoluteFilePath());
    };

    const QString normalizedTarget = normalized(targetPath);
    for (auto it = m_documentPaths.constBegin(); it != m_documentPaths.constEnd(); ++it) {
        if (it.value().isEmpty()) {
            continue;
        }
#ifdef Q_OS_WIN
        if (normalized(it.value()).compare(normalizedTarget, Qt::CaseInsensitive) == 0) {
#else
        if (normalized(it.value()) == normalizedTarget) {
#endif
            return fail(tr("Close the project before restoring a previous version."));
        }
    }

    QFileInfo targetInfo(targetPath);
    const QString parentPath = targetInfo.absolutePath();
    if (!QDir().mkpath(parentPath)) {
        return fail(tr("Could not create the project folder for restoration."));
    }

    QDir parentDir(parentPath);
    const QString targetName = targetInfo.fileName();
    if (targetName.isEmpty()) {
        return fail(tr("The original project path is invalid."));
    }

    const QString token =
        QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    const QString tempName =
        targetName + QStringLiteral(".3en-restore-tmp-") + token;
    const QString tempPath = parentDir.filePath(tempName);
    QDir(tempPath).removeRecursively();

    if (!copyDirectoryRecursively(snapshotPath, tempPath)) {
        QDir(tempPath).removeRecursively();
        return fail(tr("Could not prepare the recovery copy."));
    }

    // Recovery metadata belongs to the backup system, not the restored notebook.
    QFile::remove(QDir(tempPath).filePath(QStringLiteral("recovery.json")));

    bool hadOriginal = QFileInfo::exists(targetPath);
    QString backupName;
    QString backupPath;

    if (hadOriginal) {
        if (!QFileInfo(targetPath).isDir()) {
            QDir(tempPath).removeRecursively();
            return fail(tr("The original project path is not a notebook folder."));
        }

        backupName =
            targetName + QStringLiteral(".pre-restore-")
            + QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
        backupPath = parentDir.filePath(backupName);

        if (!parentDir.rename(targetName, backupName)) {
            QDir(tempPath).removeRecursively();
            return fail(tr("Could not move the current project to a safety backup."));
        }
    }

    if (!parentDir.rename(tempName, targetName)) {
        if (hadOriginal) {
            parentDir.rename(backupName, targetName);
        }
        QDir(tempPath).removeRecursively();
        return fail(tr("Could not activate the restored project. The original project was kept."));
    }

    // Keep the pre-restore version inside Recovery Center as an additional
    // safety point. If this copy fails, leave the sibling backup in place.
    if (hadOriginal && !backupPath.isEmpty() && QDir(backupPath).exists()) {
        const QString safetyPath =
            QFileInfo(snapshotPath).absolutePath()
            + QStringLiteral("/")
            + QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"))
            + QStringLiteral("-pre-restore.snb");

        if (copyDirectoryRecursively(backupPath, safetyPath)) {
            QJsonObject metadata;
            metadata[QStringLiteral("document_name")] =
                QFileInfo(targetPath).completeBaseName();
            metadata[QStringLiteral("original_path")] = targetPath;
            metadata[QStringLiteral("snapshot_utc")] =
                QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
            metadata[QStringLiteral("recovery_format_version")] = 1;
            metadata[QStringLiteral("snapshot_kind")] =
                QStringLiteral("pre_restore");

            QFile metadataFile(
                QDir(safetyPath).filePath(QStringLiteral("recovery.json")));
            if (metadataFile.open(
                    QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                metadataFile.write(
                    QJsonDocument(metadata).toJson(QJsonDocument::Indented));
                metadataFile.close();
            }

            QDir(backupPath).removeRecursively();

            QSettings recoverySettings(
                QStringLiteral("3E"), QStringLiteral("3ENotes"));
            const int keepCount = qBound(
                1,
                recoverySettings.value(
                    QStringLiteral("recovery/keepCount"), 5).toInt(),
                20);
            pruneRecoverySnapshots(QFileInfo(snapshotPath).absolutePath(), keepCount);
        }
    }

    NotebookLibrary::instance()->addToRecent(targetPath);
    NotebookLibrary::instance()->save();

    if (restoredPath) {
        *restoredPath = targetPath;
    }

    return true;
}
QString DocumentManager::createAutoSavePath(Document* doc)
{
    if (!doc) {
        return QString();
    }
    
    // Create path in app's permanent storage (survives system cleanup)
    // This is used for Android auto-save where we want documents to persist
    QString notebooksDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/notebooks";
    QDir().mkpath(notebooksDir);
    
    // Use document name if available, otherwise generate from UUID
    QString baseName = doc->name;
    if (baseName.isEmpty()) {
        baseName = doc->isEdgeless() ? tr("Untitled Canvas") : tr("Untitled");
    }
    
    // Sanitize filename: remove/replace characters invalid for filenames
    // Invalid chars on various platforms: / \ : * ? " < > |
    baseName.replace(QRegularExpression(R"([/\\:*?"<>|])"), "_");
    baseName = baseName.trimmed();
    if (baseName.isEmpty()) {
        baseName = doc->isEdgeless() ? tr("Untitled Canvas") : tr("Untitled");
    }
    
    // Ensure unique filename
    QString filePath = notebooksDir + "/" + baseName + ".snb";
    if (QDir(filePath).exists()) {
        // File exists - append UUID suffix to make unique
        QString uuid = doc->id.left(8);
        filePath = notebooksDir + "/" + baseName + "_" + uuid + ".snb";
        
        // If UUID-suffixed path also exists (from a previous crashed session),
        // keep appending more UUID characters until we find a unique name
        int uuidLen = 8;
        while (QDir(filePath).exists() && uuidLen < doc->id.length()) {
            uuidLen += 4;
            uuid = doc->id.left(qMin(uuidLen, doc->id.length()));
            filePath = notebooksDir + "/" + baseName + "_" + uuid + ".snb";
        }
        
        // Final fallback: append timestamp if UUID is exhausted
        if (QDir(filePath).exists()) {
            QString timestamp = QString::number(QDateTime::currentMSecsSinceEpoch());
            filePath = notebooksDir + "/" + baseName + "_" + timestamp + ".snb";
        }
    }
    
    return filePath;
}

bool DocumentManager::isUsingTempBundle(Document* doc) const
{
    if (!doc) {
        return false;
    }
    return m_tempBundlePaths.contains(doc) && !m_tempBundlePaths.value(doc).isEmpty();
}

QString DocumentManager::tempBundlePath(Document* doc) const
{
    if (!doc) {
        return QString();
    }
    return m_tempBundlePaths.value(doc);
}

void DocumentManager::cleanupTempBundle(Document* doc)
{
    if (!doc) {
        return;
    }
    
    QString tempPath = m_tempBundlePaths.value(doc);
    if (tempPath.isEmpty()) {
        return;
    }
    
    // Remove from tracking
    m_tempBundlePaths.remove(doc);
    
    // Delete the temp directory recursively
    QDir tempDir(tempPath);
    if (tempDir.exists()) {
        if (tempDir.removeRecursively()) {
#ifdef SPEEDYNOTE_DEBUG
            qDebug() << "DocumentManager: Cleaned up temp bundle:" << tempPath;
#endif
        } else {
            qWarning() << "DocumentManager: Failed to clean up temp bundle:" << tempPath;
        }
    }
}

int DocumentManager::autoSaveModifiedDocuments()
{
    int savedCount = 0;

    for (Document* doc : m_documents) {
        if (!doc) {
            continue;
        }

        // User edits may set Document::modified directly, so always use
        // hasUnsavedChanges() rather than relying only on manager signals.
        if (!hasUnsavedChanges(doc)) {
            continue;
        }

        const QString existingPath = m_documentPaths.value(doc);
        const bool usingTemp = isUsingTempBundle(doc);
        const bool hasPermanentPath = !existingPath.isEmpty() && !usingTemp;

        QString savePath;
        bool isNewDocument = false;

        if (hasPermanentPath) {
            savePath = existingPath;

            // Snapshot the previous good state before overwriting it. Failure to
            // create a backup does not block the user's save.
            if (!createRecoverySnapshot(doc, savePath)) {
                qWarning() << "DocumentManager: Could not create pre-save recovery snapshot for"
                           << savePath;
            }
        } else {
            savePath = createAutoSavePath(doc);
            isNewDocument = true;

            if (savePath.isEmpty()) {
                qWarning() << "DocumentManager: Failed to create auto-save path for"
                           << doc->name;
                continue;
            }
        }

        if (doc->saveBundle(savePath)) {
            if (isNewDocument) {
                m_documentPaths[doc] = savePath;
                cleanupTempBundle(doc);

                // New documents have no pre-save version, so create their first
                // recovery point immediately after the first successful save.
                if (!createRecoverySnapshot(doc, savePath)) {
                    qWarning() << "DocumentManager: Could not create initial recovery snapshot for"
                               << savePath;
                }
            }

            clearModified(doc);
            addToRecent(savePath);
            NotebookLibrary::instance()->addToRecent(savePath);
            savedCount++;

#ifdef SPEEDYNOTE_DEBUG
            qDebug() << "DocumentManager: Auto-saved" << doc->name << "to" << savePath;
#endif
        } else {
            qWarning() << "DocumentManager: Failed to auto-save"
                       << doc->name << "to" << savePath;
        }
    }

    // Flush the launcher library even if this pass had nothing to save.
    NotebookLibrary::instance()->save();

#ifdef SPEEDYNOTE_DEBUG
    qDebug() << "DocumentManager: Auto-save pass completed; saved"
             << savedCount << "document(s)";
#endif

    return savedCount;
}