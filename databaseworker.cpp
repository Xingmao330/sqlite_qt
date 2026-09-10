#include "databaseworker.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QVariant>

namespace {
    constexpr int kTransactionBatchSize = 5000;
}

DatabaseWorker::DatabaseWorker(QString databasePath, QObject *parent)
    : QObject(parent), m_databasePath(std::move(databasePath)) {
    m_connectionName = QStringLiteral("sqlite_writer_%1")
            .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
}

bool DatabaseWorker::openDatabase() {
    if (QSqlDatabase::contains(m_connectionName)) return true;
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(m_databasePath);
    if (!db.open()) {
        emit failed(tr("无法打开 SQLite 数据库：%1").arg(db.lastError().text()));
        return false;
    }
    QSqlQuery pragma(db);
    if (!pragma.exec("PRAGMA journal_mode=WAL") || !pragma.exec("PRAGMA synchronous=NORMAL") ||
        !pragma.exec("PRAGMA busy_timeout=8000") || !pragma.exec("PRAGMA temp_store=MEMORY")) {
        emit failed(tr("SQLite 初始化失败：%1").arg(pragma.lastError().text()));
        return false;
    }
    return true;
}

bool DatabaseWorker::beginTransaction() {
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (db.transaction()) return true;
    emit failed(tr("无法开始事务：%1").arg(db.lastError().text()));
    return false;
}

void DatabaseWorker::generateTestData(qint64 recordCount) {
    if (recordCount <= 0 || !openDatabase()) return;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery maxIdQuery(db);
    qint64 firstNumber = 1;
    if (maxIdQuery.exec("SELECT COALESCE(MAX(id), 0) FROM customers") && maxIdQuery.next())
        firstNumber = maxIdQuery.value(0).toLongLong() + 1;
    QElapsedTimer timer;
    timer.start();
    qint64 inserted = 0;
    const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
    while (inserted < recordCount) {
        const int count = static_cast<int>(qMin<qint64>(kTransactionBatchSize, recordCount - inserted));
        if (!beginTransaction()) return;
        QSqlQuery query(db);
        query.prepare(
            "INSERT INTO customers (name, email, phone, company, note, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?)");
        QVariantList names, emails, phones, companies, notes, times;
        for (int i = 0; i < count; ++i) {
            const qint64 n = firstNumber + inserted + i;
            names << QStringLiteral("测试用户%1").arg(n);
            emails << QStringLiteral("user%1@example.com").arg(n);
            phones << QStringLiteral("1%1").arg(3000000000LL + (n % 699999999LL));
            companies << QStringLiteral("公司%1").arg(n % 1000);
            notes << QStringLiteral("自动生成数据 #%1").arg(n);
            times << now;
        }
        query.addBindValue(names);
        query.addBindValue(emails);
        query.addBindValue(phones);
        query.addBindValue(companies);
        query.addBindValue(notes);
        query.addBindValue(times);
        query.addBindValue(times);
        if (!query.execBatch()) {
            db.rollback();
            emit failed(tr("批量插入失败：%1").arg(query.lastError().text()));
            return;
        }
        if (!db.commit()) {
            db.rollback();
            emit failed(tr("提交事务失败：%1").arg(db.lastError().text()));
            return;
        }
        inserted += count;
        emit progress(inserted, recordCount, timer.elapsed());
    }
    emit generationFinished(inserted, timer.elapsed());
}

void DatabaseWorker::clearAllCustomers() {
    if (!openDatabase() || !beginTransaction()) return;
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    if (!query.exec("DELETE FROM customers") || !QSqlDatabase::database(m_connectionName).commit()) {
        QSqlDatabase::database(m_connectionName).rollback();
        emit failed(tr("清空数据失败：%1").arg(query.lastError().text()));
        return;
    }
    emit cleared();
}

void DatabaseWorker::updateCustomer(qint64 id, const QString &name, const QString &email,
                                    const QString &phone, const QString &company, const QString &note) {
    if (id <= 0 || !openDatabase()) return;
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    query.prepare("UPDATE customers SET name=?, email=?, phone=?, company=?, note=?, "
        "updated_at=? WHERE id=?");
    query.addBindValue(name);
    query.addBindValue(email);
    query.addBindValue(phone);
    query.addBindValue(company);
    query.addBindValue(note);
    query.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    query.addBindValue(id);
    if (!query.exec()) {
        emit failed(tr("更新客户失败：%1").arg(query.lastError().text()));
        return;
    }
    if (query.numRowsAffected() != 1) {
        emit failed(tr("要更新的客户不存在或已被删除。"));
        return;
    }
    emit customerUpdated(id);
}
