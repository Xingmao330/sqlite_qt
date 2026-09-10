# 千万级 SQLite 客户管理示例

基于 **Qt Widgets + Qt SQL + SQLite** 的本地客户管理程序，展示如何在桌面端处理千万级记录的批量写入、检索、分页和编辑。

![程序界面截图](screenshot/04.jpg)

## 特性

- **WAL 模式**：写入期间可继续读取已提交数据。
- **后台单写线程**：`DatabaseWorker` 在独立 `QThread` 中执行所有写操作，避免阻塞界面。
- **批量事务导入**：每 5,000 条数据一个事务，使用预编译 SQL 和 `execBatch()`。
- **千万级测试数据**：弹窗下拉选择生成 100 万、500 万或 1,000 万条记录。
- **游标分页**：使用 `id < cursor` 的 Keyset Pagination，避免深层 `OFFSET` 分页性能问题。
- **字段索引**：姓名、邮箱、电话和公司均建立复合索引。
- **模糊查询**：支持按姓名、邮箱、电话或公司进行安全的包含搜索。
- **点击编辑**：点击表格记录后自动回显到上方表单；保存通过后台线程异步更新。

## 架构

```text
MainWindow（GUI 线程）
 ├─ sqlite_gui_reader：查询、分页、表格展示
 └─ DatabaseWorker（QThread）
     └─ sqlite_writer_xxx：批量插入、更新、清空
          ↓
    customers.sqlite（WAL）
```

Qt 的 SQL 连接不可跨线程使用，因此读写使用两个独立的 `QSqlDatabase` 连接。SQLite 只有一个写入者，全部写任务由单个 Worker 串行执行。

## 环境要求

- CMake 3.19 或更高版本
- C++17 编译器
- Qt 6.5 或更高版本，且安装以下模块：
  - Core
  - Widgets
  - Sql
  - SQLite 驱动插件（`QSQLITE`）

## 构建与运行

```bash
cmake -S . -B build
cmake --build build --config Debug
```

Windows Debug 构建后的可执行文件通常位于：

```text
build/Debug/sqLite_qt.exe
```

首次成功启动时，程序会自动创建数据库表和索引。

## 使用说明

1. 点击底部的“后台生成测试数据”。
2. 在下拉框选择 100 万、500 万或 1,000 万条。
3. 点击“开始生成”，观察底部进度与写入速率。
4. 可在写入期间使用搜索、上一页和下一页查询已提交的数据。
5. 点击表格任意一行，客户信息会回显至顶部表单。
6. 修改字段并点击“保存修改”，更新任务会投递到后台写线程。

## 关键 SQL 策略

### WAL 与连接配置

```sql
PRAGMA journal_mode = WAL;
PRAGMA synchronous = NORMAL;
PRAGMA busy_timeout = 8000;
PRAGMA temp_store = MEMORY;
```

### 表与索引

```sql
CREATE TABLE IF NOT EXISTS customers (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    email TEXT NOT NULL UNIQUE,
    phone TEXT,
    company TEXT,
    note TEXT,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_customers_name_id ON customers(name, id);
CREATE INDEX IF NOT EXISTS idx_customers_email_id ON customers(email, id);
CREATE INDEX IF NOT EXISTS idx_customers_phone_id ON customers(phone, id);
CREATE INDEX IF NOT EXISTS idx_customers_company_id ON customers(company, id);
```

### 游标分页

```sql
SELECT id, name, email, phone, company, note, created_at, updated_at
FROM customers
WHERE id < ?
ORDER BY id DESC
LIMIT ?;
```

### 批量写入

```sql
INSERT INTO customers
    (name, email, phone, company, note, created_at, updated_at)
VALUES (?, ?, ?, ?, ?, ?, ?);
```

写入侧每 5,000 条执行一次 `BEGIN → execBatch() → COMMIT`，失败时回滚当前批次。

## 数据库文件位置

数据库不会放在工程目录中，而是由 Qt 写入用户本地数据目录：

```text
%LOCALAPPDATA%\sqLite_qt\customers.sqlite
```

常见实际路径：

```text
C:\Users\<用户名>\AppData\Local\sqLite_qt\customers.sqlite
```

使用 WAL 时，数据库旁可能同时出现以下文件，这是正常现象：

```text
customers.sqlite-wal
customers.sqlite-shm
```

## 注意事项

- `LIKE '%关键词%'` 可实现包含匹配，但普通 B-Tree 索引通常无法充分加速它；需要高性能全文检索时，建议引入 SQLite FTS5。
- SQLite 适合单机、本地、单写多读场景；若需要多机器高并发写入，应选择服务端数据库。
- 千万级数据及多个索引会占用数 GB 磁盘空间，导入前请确认磁盘空间充足。
