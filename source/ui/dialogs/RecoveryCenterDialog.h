#pragma once

#include <QDialog>

class QLabel;
class QListWidget;
class QPushButton;
class MainWindow;
class DocumentManager;

class RecoveryCenterDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RecoveryCenterDialog(MainWindow* mainWindow,
                                  DocumentManager* documentManager,
                                  QWidget* parent = nullptr);

private:
    QString selectedSnapshotPath() const;
    void refresh();
    void updateButtons();

    MainWindow* m_mainWindow = nullptr;
    DocumentManager* m_documentManager = nullptr;
    QListWidget* m_list = nullptr;
    QLabel* m_emptyLabel = nullptr;
    QPushButton* m_openButton = nullptr;
    QPushButton* m_restoreButton = nullptr;
    QPushButton* m_deleteButton = nullptr;
    QPushButton* m_clearButton = nullptr;
};