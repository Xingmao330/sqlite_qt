#include "mainwindow.h"
#include "databaseworker.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlQueryModel>
#include <QStandardPaths>
#include <QTableView>
#include <QThread>
#include <QVBoxLayout>
#include <QWidget>
#include <limits>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(tr("千万级数据管理"));
    resize(1400, 850);
    m_databasePath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/customers.sqlite";
    QDir().mkpath(QFileInfo(m_databasePath).absolutePath());
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    auto *detailBox = new QGroupBox(tr("客户信息"), central);
    auto *form = new QFormLayout(detailBox);
    m_nameEdit = new QLineEdit(detailBox);
    m_emailEdit = new QLineEdit(detailBox);
    m_phoneEdit = new QLineEdit(detailBox);
    m_companyEdit = new QLineEdit(detailBox);
    m_noteEdit = new QLineEdit(detailBox);
    form->addRow(tr("姓名："), m_nameEdit);
    form->addRow(tr("邮箱："), m_emailEdit);
    form->addRow(tr("电话："), m_phoneEdit);
    form->addRow(tr("公司："), m_companyEdit);
    auto *formActions = new QHBoxLayout;
    auto *saveButton = new QPushButton(tr("保存修改"), detailBox);
    formActions->addWidget(saveButton);
    formActions->addStretch();
    form->addRow(tr("备注："), m_noteEdit);
    form->addRow(formActions);
    layout->addWidget(detailBox);
    auto *toolbar = new QHBoxLayout;
    toolbar->addWidget(new QLabel(tr("搜索字段："), central));
    m_searchColumn = new QComboBox(central);
    m_searchColumn->addItem(tr("按姓名"), "name");
    m_searchColumn->addItem(tr("按邮箱"), "email");
    m_searchColumn->addItem(tr("按电话"), "phone");
    m_searchColumn->addItem(tr("按公司"), "company");
    toolbar->addWidget(m_searchColumn);
    m_searchText = new QLineEdit(central);
    m_searchText->setPlaceholderText(tr("模糊搜索（例如：张三、example、公司12）"));
    toolbar->addWidget(m_searchText, 1);
    auto *searchButton = new QPushButton(tr("查询"), central);
    toolbar->addWidget(searchButton);
    toolbar->addSpacing(20);
    toolbar->addWidget(new QLabel(tr("每页："), central));
    m_pageSize = new QComboBox(central);
    m_pageSize->addItems({"100", "500", "1000", "5000", "20000"});
    m_pageSize->setCurrentText("1000");
    toolbar->addWidget(m_pageSize);
    m_previousButton = new QPushButton(tr("上一页"), central);
    m_nextButton = new QPushButton(tr("下一页"), central);
    toolbar->addWidget(m_previousButton);
    toolbar->addWidget(m_nextButton);
    m_pageLabel = new QLabel(central);
    toolbar->addWidget(m_pageLabel);
    layout->addLayout(toolbar);
    m_model = new QSqlQueryModel(this);
    m_table = new QTableView(central);
    m_table->setModel(m_model);
    m_table->setAlternatingRowColors(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(m_table, 1);
    auto *bottom = new QHBoxLayout;
    auto *generateButton = new QPushButton(tr("后台生成测试数据"), central);
    auto *clearButton = new QPushButton(tr("一键清空数据"), central);
    bottom->addWidget(generateButton);
    bottom->addWidget(clearButton);
    m_progress = new QProgressBar(central);
    m_progress->setVisible(false);
    bottom->addWidget(m_progress, 1);
    m_statusLabel = new QLabel(tr("准备就绪"), central);
    bottom->addWidget(m_statusLabel);
    layout->addLayout(bottom);
    setCentralWidget(central);
    connect(searchButton, &QPushButton::clicked, this, &MainWindow::reloadFirstPage);
    connect(m_searchText, &QLineEdit::returnPressed, this, &MainWindow::reloadFirstPage);
    connect(m_pageSize, &QComboBox::currentTextChanged, this, &MainWindow::reloadFirstPage);
    connect(m_previousButton, &QPushButton::clicked, this, &MainWindow::loadPreviousPage);
    connect(m_nextButton, &QPushButton::clicked, this, &MainWindow::loadNextPage);
    connect(generateButton, &QPushButton::clicked, this, &MainWindow::generateData);
    connect(clearButton, &QPushButton::clicked, this, &MainWindow::clearData);
    connect(m_table, &QTableView::clicked, this, &MainWindow::loadSelectedCustomer);
    connect(saveButton, &QPushButton::clicked, this, &MainWindow::saveSelectedCustomer);
    if (openReadDatabase() && initializeSchema())
        reloadFirstPage();
    m_writerThread = new QThread(this);
    m_writer = new DatabaseWorker(m_databasePath);
    m_writer->moveToThread(m_writerThread);
    connect(m_writerThread, &QThread::finished, m_writer, &QObject::deleteLater);
    connect(m_writer, &DatabaseWorker::progress, this, &MainWindow::updateProgress);
    connect(m_writer, &DatabaseWorker::generationFinished, this, &MainWindow::refreshAfterWrite);
    connect(m_writer, &DatabaseWorker::cleared, this, &MainWindow::refreshAfterWrite);
    connect(m_writer, &DatabaseWorker::customerUpdated, this, &MainWindow::refreshAfterWrite);
    connect(m_writer, &DatabaseWorker::failed, this, &MainWindow::showDatabaseError);
    m_writerThread->start();
}

MainWindow::~MainWindow() {
    m_writerThread->quit();
    m_writerThread->wait();
    QSqlDatabase::removeDatabase(m_readConnectionName);
}

bool MainWindow::openReadDatabase() {
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", m_readConnectionName);
    db.setDatabaseName(m_databasePath);
    if (!db.open()) {
        showDatabaseError(tr("无法打开数据库：%1").arg(db.lastError().text()));
        return false;
    }
    QSqlQuery query(db);
    query.exec("PRAGMA journal_mode=WAL");
    query.exec("PRAGMA busy_timeout=8000");
    return true;
}

bool MainWindow::initializeSchema() {
    QSqlQuery q(QSqlDatabase::database(m_readConnectionName));
    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS customers (id INTEGER PRIMARY KEY, name TEXT NOT NULL, email TEXT NOT NULL UNIQUE, phone TEXT, company TEXT, note TEXT, created_at TEXT NOT NULL, updated_at TEXT NOT NULL)")
        ||
        !q.exec("CREATE INDEX IF NOT EXISTS idx_customers_name_id ON customers(name, id)") || !q.exec(
            "CREATE INDEX IF NOT EXISTS idx_customers_email_id ON customers(email, id)") ||
        !q.exec("CREATE INDEX IF NOT EXISTS idx_customers_phone_id ON customers(phone, id)") || !q.exec(
            "CREATE INDEX IF NOT EXISTS idx_customers_company_id ON customers(company, id)")) {
        showDatabaseError(tr("创建表或索引失败：%1").arg(q.lastError().text()));
        return false;
    }
    return true;
}

QString MainWindow::activeColumn() const { return m_searchColumn->currentData().toString(); }

void MainWindow::reloadFirstPage() {
    m_cursors = {0};
    m_pageIndex = 0;
    loadPage();
}

void MainWindow::loadNextPage() {
    if (!m_lastId) return;
    if (m_cursors.size() == m_pageIndex + 1) m_cursors.append(m_lastId);
    ++m_pageIndex;
    loadPage();
}

void MainWindow::loadPreviousPage() {
    if (!m_pageIndex) return;
    --m_pageIndex;
    loadPage();
}

void MainWindow::loadPage() {
    const int pageSize = m_pageSize->currentText().toInt();
    const qint64 cursor = m_cursors.value(m_pageIndex);
    const QString term = m_searchText->text().trimmed();
    QSqlQuery query(QSqlDatabase::database(m_readConnectionName));
    QString sql =
            "SELECT id AS 'ID', name AS '姓名', email AS '邮箱', phone AS '电话', company AS '公司', note AS '备注', created_at AS '创建时间', updated_at AS '更新时间' FROM customers WHERE id < ?";
    if (!term.isEmpty())
        sql += QStringLiteral(" AND %1 LIKE ? ESCAPE '\\'").arg(activeColumn());
    sql += " ORDER BY id DESC LIMIT ?";
    query.prepare(sql);
    query.addBindValue(cursor == 0 ? std::numeric_limits<qint64>::max() : cursor);
    if (!term.isEmpty()) {
        QString escaped = term;
        escaped.replace('\\', "\\\\").replace('%', "\\%").replace('_', "\\_");
        query.addBindValue('%' + escaped + '%');
    }
    query.addBindValue(pageSize);
    if (!query.exec()) {
        showDatabaseError(tr("查询失败：%1").arg(query.lastError().text()));
        return;
    }
    m_model->setQuery(std::move(query));
    // QSqlQueryModel fetches in small blocks by default.  Materialize only the
    // requested LIMIT page so the cursor is always based on its real last row.
    while (m_model->canFetchMore()) m_model->fetchMore();
    m_lastId = 0;
    const int rows = m_model->rowCount();
    if (rows) m_lastId = m_model->data(m_model->index(rows - 1, 0)).toLongLong();
    updatePager();
}

void MainWindow::updatePager() {
    m_previousButton->setEnabled(m_pageIndex > 0);
    m_nextButton->setEnabled(m_model->rowCount() == m_pageSize->currentText().toInt());
    m_pageLabel->setText(tr("第 %1 页 · 当前 %2 条 · 游标 %3").arg(m_pageIndex + 1).arg(m_model->rowCount()).arg(m_lastId));
}

void MainWindow::loadSelectedCustomer(const QModelIndex &index) {
    const int row = index.row();
    m_selectedId = m_model->data(m_model->index(row, 0)).toLongLong();
    m_nameEdit->setText(m_model->data(m_model->index(row, 1)).toString());
    m_emailEdit->setText(m_model->data(m_model->index(row, 2)).toString());
    m_phoneEdit->setText(m_model->data(m_model->index(row, 3)).toString());
    m_companyEdit->setText(m_model->data(m_model->index(row, 4)).toString());
    m_noteEdit->setText(m_model->data(m_model->index(row, 5)).toString());
    m_statusLabel->setText(tr("已载入客户 ID %1，可在上方编辑后保存。").arg(m_selectedId));
}

void MainWindow::saveSelectedCustomer() {
    if (m_selectedId <= 0) {
        QMessageBox::information(this, tr("请选择客户"), tr("请先点击下方表格中的一条客户记录。"));
        return;
    }
    if (m_nameEdit->text().trimmed().isEmpty() || m_emailEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("数据不完整"), tr("姓名和邮箱不能为空。"));
        return;
    }
    m_statusLabel->setText(tr("正在后台保存客户 ID %1…").arg(m_selectedId));
    QMetaObject::invokeMethod(m_writer, "updateCustomer", Qt::QueuedConnection,
                              Q_ARG(qint64, m_selectedId), Q_ARG(QString, m_nameEdit->text().trimmed()),
                              Q_ARG(QString, m_emailEdit->text().trimmed()),
                              Q_ARG(QString, m_phoneEdit->text().trimmed()),
                              Q_ARG(QString, m_companyEdit->text().trimmed()),
                              Q_ARG(QString, m_noteEdit->text().trimmed()));
}

void MainWindow::generateData() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("生成测试数据"));
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("请选择要在后台批量生成的客户记录数量："), &dialog));
    auto *countCombo = new QComboBox(&dialog);
    countCombo->addItem(tr("100 万条"), 1000000LL);
    countCombo->addItem(tr("500 万条"), 5000000LL);
    countCombo->addItem(tr("1000 万条"), 10000000LL);
    layout->addWidget(countCombo);
    layout->addWidget(new QLabel(tr("每 5000 条提交一次事务；生成期间仍可查询。"), &dialog));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("开始生成"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return;
    const qint64 count = countCombo->currentData().toLongLong();
    m_progress->setRange(0, static_cast<int>(count / 1000));
    m_progress->setValue(0);
    m_progress->setVisible(true);
    m_statusLabel->setText(tr("后台线程按 5000 条/事务写入…"));
    QMetaObject::invokeMethod(m_writer, "generateTestData", Qt::QueuedConnection, Q_ARG(qint64, count));
}

void MainWindow::clearData() {
    if (QMessageBox::warning(this, tr("确认清空"), tr("将删除全部客户记录。是否继续？"), QMessageBox::Yes | QMessageBox::No) ==
        QMessageBox::Yes)
        QMetaObject::invokeMethod(m_writer, "clearAllCustomers", Qt::QueuedConnection);
}

void MainWindow::updateProgress(qint64 completed, qint64 total, qint64 elapsedMs) {
    m_progress->setValue(static_cast<int>(completed / 1000));
    const double seconds = qMax<qint64>(1, elapsedMs) / 1000.0;
    m_statusLabel->setText(
        tr("已写入 %1 / %2（%3 条/秒）").arg(completed).arg(total).arg(static_cast<qint64>(completed / seconds)));
}

void MainWindow::refreshAfterWrite() {
    m_progress->setVisible(false);
    m_statusLabel->setText(tr("后台写入完成，可继续查询。"));
    reloadFirstPage();
}

void MainWindow::showDatabaseError(const QString &message) {
    m_statusLabel->setText(message);
    QMessageBox::critical(this, tr("数据库错误"), message);
}
