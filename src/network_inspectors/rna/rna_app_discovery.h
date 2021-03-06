//--------------------------------------------------------------------------
// Copyright (C) 2020-2020 Cisco and/or its affiliates. All rights reserved.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License Version 2 as published
// by the Free Software Foundation.  You may not use, modify or distribute
// this program under any other version of the GNU General Public License.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
//--------------------------------------------------------------------------

#ifndef RNA_APP_DISCOVERY_H
#define RNA_APP_DISCOVERY_H

#include "rna_fingerprint_tcp.h"
#include "rna_fingerprint_ua.h"
#include "rna_pnd.h"

class RnaAppDiscovery
{
public:
    static void process(AppidEvent*, DiscoveryFilter&, RnaConfig*, RnaLogger&);

    static bool discover_service(const snort::Packet*, RNAFlow*, IpProtocol, RnaConfig*,
        RnaLogger&, uint16_t, AppId service = APP_ID_NONE, bool is_client = false);

    static void discover_payload(const snort::Packet*, RNAFlow*, IpProtocol, RnaConfig*,
        RnaLogger&, AppId service, AppId payload, AppId client);

    static void discover_client(const snort::Packet*, RNAFlow*, RnaConfig*, RnaLogger&,
        const char*, AppId client, AppId service);

    static void discover_user(const snort::Packet*, RNAFlow*, RnaLogger&, const char*,
        AppId, IpProtocol, RnaConfig*, bool);

    static void discover_banner(const snort::Packet*, RNAFlow*, IpProtocol, RnaLogger&, AppId);
private:
    static void update_service_info(const snort::Packet*, RNAFlow*, IpProtocol, uint16_t,
        const char* vendor, const char* version, RnaLogger&, RnaConfig*, AppId service, bool is_client = false);

    static void analyze_user_agent_fingerprint(const snort::Packet*, RNAFlow*, const char* host,
        const char* uagent, RnaLogger&, snort::UaFpProcessor&);

    static RnaTracker get_server_rna_tracker(const snort::Packet*, RNAFlow*);
    static RnaTracker get_client_rna_tracker(const snort::Packet*, RNAFlow*);
};

#endif
