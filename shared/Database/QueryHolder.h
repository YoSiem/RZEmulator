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
#include "SQLOperation.h"
#include <vector>

class SQLQueryHolder {
    friend class SQLQueryHolderTask;

private:
    std::vector<std::pair<PreparedStatement *, PreparedQueryResult>> m_queries;

public:
    SQLQueryHolder() {}

    virtual ~SQLQueryHolder();
    bool SetPreparedQuery(size_t index, PreparedStatement *stmt);
    void SetSize(size_t size);
    PreparedQueryResult GetPreparedResult(size_t index);
    void SetPreparedResult(size_t index, PreparedResultSet *result);
};

class SQLQueryHolderTask : public SQLOperation {
private:
    SQLQueryHolder *m_holder;
    QueryResultHolderPromise m_result;
    bool m_executed;

public:
    SQLQueryHolderTask(SQLQueryHolder *holder)
        : m_holder(holder)
        , m_executed(false)
    {
    }

    ~SQLQueryHolderTask();

    bool Execute() override;

    QueryResultHolderFuture GetFuture() { return m_result.get_future(); }
};