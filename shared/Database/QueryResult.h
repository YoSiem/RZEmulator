#pragma once
/*
 * Copyright (C) 2008-2018 TrinityCore <https://www.trinitycore.org/>
 * Copyright (C) 2005-2009 MaNGOS <http://getmangos.com/>
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
#include <vector>

#include "DatabaseEnvFwd.h"
#include "Define.h"

class ResultSet {
public:
    ResultSet(MYSQL_RES *result, MYSQL_FIELD *fields, uint64_t rowCount, uint32_t fieldCount);
    ~ResultSet();

    bool NextRow();

    uint64_t GetRowCount() const { return _rowCount; }

    uint32_t GetFieldCount() const { return _fieldCount; }

    Field *Fetch() const { return _currentRow; }

    Field const &operator[](std::size_t index) const;

protected:
    uint64_t _rowCount;
    Field *_currentRow;
    uint32_t _fieldCount;

private:
    void CleanUp();
    MYSQL_RES *_result;
    MYSQL_FIELD *_fields;

    ResultSet(ResultSet const &right) = delete;
    ResultSet &operator=(ResultSet const &right) = delete;
};

class PreparedResultSet {
public:
    PreparedResultSet(MYSQL_STMT *stmt, MYSQL_RES *result, uint64_t rowCount, uint32_t fieldCount);
    ~PreparedResultSet();

    bool NextRow();

    uint64_t GetRowCount() const { return m_rowCount; }

    uint32_t GetFieldCount() const { return m_fieldCount; }

    Field *Fetch() const;
    Field const &operator[](std::size_t index) const;

protected:
    std::vector<Field> m_rows;
    uint64_t m_rowCount;
    uint64_t m_rowPosition;
    uint32_t m_fieldCount;

private:
    MYSQL_BIND *m_rBind;
    MYSQL_STMT *m_stmt;
    MYSQL_RES *m_metadataResult; ///< Field metadata, returned by mysql_stmt_result_metadata

    void CleanUp();
    bool _NextRow();

    PreparedResultSet(PreparedResultSet const &right) = delete;
    PreparedResultSet &operator=(PreparedResultSet const &right) = delete;
};