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
#include <mutex>
#include <vector>

#include "DatabaseEnvFwd.h"
#include "Define.h"
#include "SQLOperation.h"
#include "StringFormat.h"

/*! Transactions, high level class.*/
class Transaction {
    friend class TransactionTask;
    friend class MySQLConnection;

    template<typename T>
    friend class DatabaseWorkerPool;

public:
    Transaction()
        : _cleanedUp(false)
    {
    }

    ~Transaction() { Cleanup(); }

    void Append(PreparedStatement *statement);
    void Append(const char *sql);

    template<typename Format, typename... Args>
    void PAppend(Format &&sql, Args &&...args)
    {
        Append(NGemity::StringFormatTC(std::forward<Format>(sql), std::forward<Args>(args)...).c_str());
    }

    std::size_t GetSize() const { return m_queries.size(); }

protected:
    void Cleanup();
    std::vector<SQLElementData> m_queries;

private:
    bool _cleanedUp;
};

/*! Low level class*/
class TransactionTask : public SQLOperation {
    template<class T>
    friend class DatabaseWorkerPool;
    friend class DatabaseWorker;

public:
    TransactionTask(SQLTransaction trans)
        : m_trans(trans)
    {
    }

    ~TransactionTask() {}

protected:
    bool Execute() override;

    SQLTransaction m_trans;
    static std::mutex _deadlockLock;
};