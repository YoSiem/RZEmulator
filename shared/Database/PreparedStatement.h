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
#include <future>
#include <vector>

#include "Define.h"
#include "SQLOperation.h"

#ifdef __APPLE__
#undef TYPE_BOOL
#endif

//- Union for data buffer (upper-level bind -> queue -> lower-level bind)
union PreparedStatementDataUnion {
    bool boolean;
    uint8_t ui8;
    int8_t i8;
    uint16_t ui16;
    int16_t i16;
    uint32_t ui32;
    int32_t i32;
    uint64_t ui64;
    int64_t i64;
    float f;
    double d;
};

//- This enum helps us differ data held in above union
enum PreparedStatementValueType { TYPE_BOOL, TYPE_UI8, TYPE_UI16, TYPE_UI32, TYPE_UI64, TYPE_I8, TYPE_I16, TYPE_I32, TYPE_I64, TYPE_FLOAT, TYPE_DOUBLE, TYPE_STRING, TYPE_BINARY, TYPE_NULL };

struct PreparedStatementData {
    PreparedStatementDataUnion data;
    PreparedStatementValueType type;
    std::vector<uint8_t> binary;
};

//- Forward declare
class MySQLPreparedStatement;

//- Upper-level class that is used in code
class PreparedStatement {
    friend class PreparedStatementTask;
    friend class MySQLPreparedStatement;
    friend class MySQLConnection;

public:
    explicit PreparedStatement(uint32_t index);
    ~PreparedStatement();

    void setBool(const uint8_t index, const bool value);
    void setUInt8(const uint8_t index, const uint8_t value);
    void setUInt16(const uint8_t index, const uint16_t value);
    void setUInt32(const uint8_t index, const uint32_t value);
    void setUInt64(const uint8_t index, const uint64_t value);
    void setInt8(const uint8_t index, const int8_t value);
    void setInt16(const uint8_t index, const int16_t value);
    void setInt32(const uint8_t index, const int32_t value);
    void setInt64(const uint8_t index, const int64_t value);
    void setFloat(const uint8_t index, const float value);
    void setDouble(const uint8_t index, const double value);
    void setString(const uint8_t index, const std::string &value);
    void setBinary(const uint8_t index, const std::vector<uint8_t> &value);
    void setNull(const uint8_t index);

protected:
    void BindParameters();

protected:
    MySQLPreparedStatement *m_stmt;
    uint32_t m_index;
    std::vector<PreparedStatementData> statement_data; //- Buffer of parameters, not tied to MySQL in any way yet

    PreparedStatement(PreparedStatement const &right) = delete;
    PreparedStatement &operator=(PreparedStatement const &right) = delete;
};

//- Class of which the instances are unique per MySQLConnection
//- access to these class objects is only done when a prepared statement task
//- is executed.
class MySQLPreparedStatement {
    friend class MySQLConnection;
    friend class PreparedStatement;

public:
    MySQLPreparedStatement(MYSQL_STMT *stmt);
    ~MySQLPreparedStatement();

    void setNull(const uint8_t index);
    void setBool(const uint8_t index, const bool value);
    void setUInt8(const uint8_t index, const uint8_t value);
    void setUInt16(const uint8_t index, const uint16_t value);
    void setUInt32(const uint8_t index, const uint32_t value);
    void setUInt64(const uint8_t index, const uint64_t value);
    void setInt8(const uint8_t index, const int8_t value);
    void setInt16(const uint8_t index, const int16_t value);
    void setInt32(const uint8_t index, const int32_t value);
    void setInt64(const uint8_t index, const int64_t value);
    void setFloat(const uint8_t index, const float value);
    void setDouble(const uint8_t index, const double value);
    void setBinary(const uint8_t index, const std::vector<uint8_t> &value, bool isString);

protected:
    MYSQL_STMT *GetSTMT() { return m_Mstmt; }

    MYSQL_BIND *GetBind() { return m_bind; }

    PreparedStatement *m_stmt;
    void ClearParameters();
    void CheckValidIndex(uint8_t index);
    std::string getQueryString(std::string const &sqlPattern) const;

private:
    MYSQL_STMT *m_Mstmt;
    uint32_t m_paramCount;
    std::vector<bool> m_paramsSet;
    MYSQL_BIND *m_bind;

    MySQLPreparedStatement(MySQLPreparedStatement const &right) = delete;
    MySQLPreparedStatement &operator=(MySQLPreparedStatement const &right) = delete;
};

//- Lower-level class, enqueuable operation
class PreparedStatementTask : public SQLOperation {
public:
    PreparedStatementTask(PreparedStatement *stmt, bool async = false);
    ~PreparedStatementTask();

    bool Execute() override;

    PreparedQueryResultFuture GetFuture() { return m_result->get_future(); }

protected:
    PreparedStatement *m_stmt;
    bool m_has_result;
    PreparedQueryResultPromise *m_result;
};