#pragma once
/*
 * Copyright (C) 2011-2017 Project SkyFire <http://www.projectskyfire.org/>
 * Copyright (C) 2008-2017 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2005-2017 MaNGOS <https://www.getmangos.eu/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
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

#include "Define.h"

// Note. All times are in milliseconds here.

class BasicEvent {
public:
    BasicEvent() { to_Abort = false; }

    virtual ~BasicEvent() {} // override destructor to perform some actions on event removal

    // this method executes when the event is triggered
    // return false if event does not want to be deleted
    // e_time is execution time, p_time is update interval
    virtual bool Execute(uint64_t /*e_time*/, uint32_t /*p_time*/) { return true; }

    virtual bool IsDeletable() const { return true; } // this event can be safely deleted

    virtual void Abort(uint64_t /*e_time*/) {} // this method executes when the event is aborted

    bool to_Abort; // set by externals when the event is aborted, aborted events don't execute
    // and get Abort call when deleted

    // these can be used for time offset control
    uint64_t m_addTime; // time when the event was added to queue, filled by event handler
    uint64_t m_execTime; // planned time of next execution, filled by event handler
};

typedef std::multimap<uint64_t, BasicEvent *> EventList;

class EventProcessor {
public:
    EventProcessor();
    ~EventProcessor();

    void Update(uint32_t p_time);
    void KillAllEvents(bool force);
    void AddEvent(BasicEvent *Event, uint64_t e_time, bool set_addtime = true);
    uint64_t CalculateTime(uint64_t t_offset) const;

protected:
    uint64_t m_time;
    EventList m_events;
    bool m_aborting;
};
