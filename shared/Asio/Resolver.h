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
#include <string>

#include <boost/asio/ip/tcp.hpp>

namespace NGemity {
    namespace Net {
        inline std::optional<boost::asio::ip::tcp::endpoint> Resolve(
            boost::asio::ip::tcp::resolver &resolver, boost::asio::ip::tcp const &protocol, std::string const &host, std::string const &service)
        {
            boost::system::error_code ec;
#if BOOST_VERSION >= 106600
            boost::asio::ip::tcp::resolver::results_type results = resolver.resolve(protocol, host, service, ec);
            if (results.empty() || ec)
                return {};

            return results.begin()->endpoint();
#else
            boost::asio::ip::tcp::resolver::query query(std::move(protocol), std::move(host), std::move(service));
            boost::asio::ip::tcp::resolver::iterator itr = resolver.resolve(query, ec);
            boost::asio::ip::tcp::resolver::iterator end;
            if (itr == end || ec)
                return {};

            return itr->endpoint();
#endif
        }
    }
}