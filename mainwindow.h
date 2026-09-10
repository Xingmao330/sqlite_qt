#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QVector>

class DatabaseWorker;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSqlQueryModel;
class QTableView;
class QThread;
class QModelIndex;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    ~MainWindow() override;

private slots:
    void reloadFirstPage();

    void loadNextPage();

    void loadPreviousPage();

    void generateData();

    void clearData();

    void updateProgress(qint64 completed, qint64 total, qint64 elapsedMs);

    void refreshAfterWrite();

    void showDatabaseError(const QString &message);

    void loadSelectedCustomer(const QModelIndex &index);

    void saveSelectedCustomer();

private:
    bool openReadDatabase();

    bool initializeSchema();

    void loadPage();

    void updatePager();

    QString activeColumn() const;

    QString m_databasePath;
    QString m_readConnectionName = QStringLiteral("sqlite_gui_reader");
    QThread *m_writerThread = nullptr;
    DatabaseWorker *m_writer = nullptr;
    QSqlQueryModel *m_model = nullptr;
    QTableView *m_table = nullptr;
    QComboBox *m_searchColumn = nullptr;
    QLineEdit *m_searchText = nullptr;
    QComboBox *m_pageSize = nullptr;
    QLabel *m_pageLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QProgressBar *m_progress = nullptr;
    QPushButton *m_previousButton = nullptr;
    QPushButton *m_nextButton = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QLineEdit *m_emailEdit = nullptr;
    QLineEdit *m_phoneEdit = nullptr;
    QLineEdit *m_companyEdit = nullptr;
    QLineEdit *m_noteEdit = nullptr;
    QVector<qint64> m_cursors{0};
    int m_pageIndex = 0;
    qint64 m_lastId = 0;
    qint64 m_selectedId = 0;
};
#endif // MAINWINDOW_H
