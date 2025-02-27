#pragma once
/*
 *  Copyright (C) 2017-2020 NGemity <https://ngemity.org/>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 3 of the License, or (at your
 *  option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include "Common.h"
#include "Pointf.h"

namespace X2D {
    class Boxf {
    public:
        Boxf() = default;
        ~Boxf() = default;

        bool IsInclude(float x, float y);
        void SetLeft(float x);
        void SetTop(float y);
        void SetRight(float x);
        void SetBottom(float y);
        virtual void Set(Pointf _begin, Pointf _end);

        Pointf begin{}, end{};

    protected:
        void normalize();
    };
}