#pragma once
/*
 * Copyright (C) 2008-2018 TrinityCore <https://www.trinitycore.org/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "DatabaseEnvFwd.h"
#include "Define.h"

template<typename T>
class ProducerConsumerQueue;

class DatabaseWorker;
class MySQLPreparedStatement;
class SQLOperation;

enum ConnectionFlags { CONNECTION_ASYNC = 0x1, CONNECTION_SYNCH = 0x2, CONNECTION_BOTH = CONNECTION_ASYNC | CONNECTION_SYNCH };

struct MySQLConnectionInfo {
    explicit MySQLConnectionInfo(std::string const &infoString);

    std::string user;
    std::string password;
    std::string database;
    std::string host;
    std::string port_or_socket;
};

typedef std::map<uint32_t /*index*/, std::pair<std::string /*query*/, ConnectionFlags /*sync/async*/>> PreparedStatementMap;

class MySQLConnection {
    template<class T>
    friend class DatabaseWorkerPool;
    friend class PingOperation;

public:
    MySQLConnection(MySQLConnectionInfo &connInfo); //! Constructor for synchronous connections.
    MySQLConnection(ProducerConsumerQueue<SQLOperation *> *queue, MySQLConnectionInfo &connInfo); //! Constructor for asynchronous connections.
    virtual ~MySQLConnection();

    virtual uint32_t Open();
    void Close();

    bool PrepareStatements();

public:
    bool Execute(const char *sql);
    bool Execute(PreparedStatement *stmt);
    ResultSet *Query(const char *sql);
    PreparedResultSet *Query(PreparedStatement *stmt);
    bool _Query(const char *sql, MYSQL_RES **pResult, MYSQL_FIELD **pFields, uint64_t *pRowCount, uint32_t *pFieldCount);
    bool _Query(PreparedStatement *stmt, MYSQL_RES **pResult, uint64_t *pRowCount, uint32_t *pFieldCount);

    void BeginTransaction();
    void RollbackTransaction();
    void CommitTransaction();
    int ExecuteTransaction(SQLTransaction &transaction);

    void Ping();

    uint32_t GetLastError();

protected:
    /// Tries to acquire lock. If lock is acquired by another thread
    /// the calling parent will just try another connection
    bool LockIfReady();

    /// Called by parent databasepool. Will let other threads access this connection
    void Unlock();

    MYSQL *GetHandle() { return m_Mysql; }

    MySQLPreparedStatement *GetPreparedStatement(uint32_t index);
    void PrepareStatement(uint32_t index, const char *sql, ConnectionFlags flags);

    virtual void DoPrepareStatements() = 0;

protected:
    std::vector<std::unique_ptr<MySQLPreparedStatement>> m_stmts; //! PreparedStatements storage
    PreparedStatementMap m_queries; //! Query storage
    bool m_reconnecting; //! Are we reconnecting?
    bool m_prepareError; //! Was there any error while preparing statements?

private:
    bool _HandleMySQLErrno(uint32_t errNo, uint8_t attempts = 5);

private:
    ProducerConsumerQueue<SQLOperation *> *m_queue; //! Queue shared with other asynchronous connections.
    std::unique_ptr<DatabaseWorker> m_worker; //! Core worker task.
    MYSQL *m_Mysql; //! MySQL Handle.
    MySQLConnectionInfo &m_connectionInfo; //! Connection info (used for logging)
    ConnectionFlags m_connectionFlags; //! Connection flags (for preparing relevant statements)
    std::mutex m_Mutex;

    MySQLConnection(MySQLConnection const &right) = delete;
    MySQLConnection &operator=(MySQLConnection const &right) = delete;
};