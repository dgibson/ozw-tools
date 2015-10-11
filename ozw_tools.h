// Copyright David Gibson 2015 <ozw@gibson.dropbear.id.au>
//
// Based on the MinOZW code shipped with OpenZWave:
//     Copyright (c) 2010 Mal Lansell <mal@openzwave.com>
//
// This program is free software: you can redistribute it and/or
// modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see
// <http://www.gnu.org/licenses/>.
#ifndef _OZW_TOOLS_H
#define _OZW_TOOLS_H

#include <Options.h>
#include <Manager.h>
#include <Driver.h>
#include <Node.h>
#include <Group.h>
#include <Notification.h>
#include <value_classes/ValueStore.h>
#include <value_classes/Value.h>
#include <value_classes/ValueBool.h>
#include <platform/Log.h>

#define OZW_CONFIG_DIR		"/etc/openzwave"
#define OZW_DEFAULT_DEV		"/dev/zwave"

OpenZWave::Manager *ozw_setup(const std::string port,
			      OpenZWave::Manager::pfnOnNotification_t watcher,
			      void *ctx = NULL);
void ozw_cleanup(OpenZWave::Manager *mgr);

std::string stringf(const char *fmt, ...);
std::string format_znode(uint32_t hid, uint8_t nid);
bool parse_znode(const std::string s, uint32_t *hidp, uint8_t *nidp);
std::string format_vid(const OpenZWave::ValueID vid);

#endif /* _OZW_TOOLS_H */
