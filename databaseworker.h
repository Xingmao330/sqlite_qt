#pragma once

#include <QObject>
#include <QString>

// Lives in the writer QThread. SQLite allows many WAL readers but one writer.
class DatabaseWorker final : public QObject {
    Q_OBJECT

public:
    explicit DatabaseWorker(QString databasePath, QObject *parent = nullptr);

public slots:
    void generateTestData(qint64 recordCount);

    void clearAllCustomers();

    void updateCustomer(qint64 id, const QString &name, const QString &email,
                        const QString &phone, const QString &company, const QString &note);

signals:
    void progress(qint64 completed, qint64 total, qint64 elapsedMs);

    void generationFinished(qint64 inserted, qint64 elapsedMs);

    void cleared();

    void customerUpdated(qint64 id);

    void failed(const QString &message);

private:
    bool openDatabase();

    bool beginTransaction();

    QString m_databasePath;
    QString m_connectionName;
};
