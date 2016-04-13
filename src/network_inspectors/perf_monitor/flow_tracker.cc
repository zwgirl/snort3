//--------------------------------------------------------------------------
// Copyright (C) 2015-2016 Cisco and/or its affiliates. All rights reserved.
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

// flow_tracker.cc author Carter Waxman <cwaxman@cisco.com>

#include "flow_tracker.h"
#include "perf_module.h"

#include "protocols/icmp4.h"
#include "protocols/tcp.h"
#include "protocols/udp.h"
#include "utils/util.h"

#ifdef UNIT_TEST
#include <catch/catch.hpp>
#endif

#define FLOW_FILE (PERF_NAME "_flow.csv")

#define MAX_PKT_LEN  9000

FlowTracker::FlowTracker(PerfConfig* perf) : PerfTracker(perf,
        perf->output == PERF_FILE ? FLOW_FILE : nullptr)
{
    pkt_len_cnt.resize( MAX_PKT_LEN + 1 );
    tcp.src.resize( config->flow_max_port_to_track + 1, 0 );
    tcp.dst.resize( config->flow_max_port_to_track + 1, 0 );
    udp.src.resize( config->flow_max_port_to_track + 1, 0 );
    udp.dst.resize( config->flow_max_port_to_track + 1, 0 );
    type_icmp.resize( UINT8_MAX + 1, 0 );

    formatter->register_section("flow");
    formatter->register_field("byte_total", &byte_total);
    formatter->register_field("packets_by_bytes", &pkt_len_cnt);
    formatter->register_field("oversized_packets", &pkt_len_oversize_cnt);

    formatter->register_section("flow_tcp");
    formatter->register_field("bytes_by_source", &tcp.src);
    formatter->register_field("bytes_by_dest", &tcp.dst);
    formatter->register_field("high_port_bytes", &tcp.high);

    formatter->register_section("flow_udp");
    formatter->register_field("bytes_by_source", &udp.src);
    formatter->register_field("bytes_by_dest", &udp.dst);
    formatter->register_field("high_port_bytes", &udp.high);

    formatter->register_section("flow_icmp");
    formatter->register_field("bytes_by_type", &type_icmp);
}

void FlowTracker::reset()
{
    formatter->finalize_fields(fh);
}

void FlowTracker::update(Packet* p)
{
    if (!p->is_rebuilt())
    {
        auto len = p->pkth->caplen;

        if (p->ptrs.tcph)
            update_transport_flows(p->ptrs.sp, p->ptrs.dp,
                tcp, len);
        
        else if (p->ptrs.udph)
            update_transport_flows(p->ptrs.sp, p->ptrs.dp,
                udp, len);

        else if (p->ptrs.icmph)
            type_icmp[p->ptrs.icmph->type] += len;

        if (len <= MAX_PKT_LEN)
            pkt_len_cnt[len]++;
        else
            pkt_len_oversize_cnt++;

        byte_total += len;
    }
}

void FlowTracker::process(bool)
{
    formatter->write(fh, cur_time);
    clear();
}

void FlowTracker::clear()
{
    byte_total = 0;

    memset(&pkt_len_cnt[0], 0, pkt_len_cnt.size() * sizeof(PegCount));
    pkt_len_oversize_cnt = 0;

    memset(&tcp.src[0], 0, tcp.src.size() * sizeof(PegCount));
    memset(&tcp.dst[0], 0, tcp.dst.size() * sizeof(PegCount));
    tcp.high = 0;

    memset(&udp.src[0], 0, udp.src.size() * sizeof(PegCount));
    memset(&udp.dst[0], 0, udp.dst.size() * sizeof(PegCount));
    udp.high = 0;
    
    memset(&type_icmp[0], 0, type_icmp.size() * sizeof(PegCount));
}

void FlowTracker::update_transport_flows(int sport, int dport,
    FlowProto& proto, int len)
{
    if (sport <= config->flow_max_port_to_track &&
        dport > config->flow_max_port_to_track)
    {
        proto.src[sport] += len;
    }

    else if (dport <= config->flow_max_port_to_track &&
        sport > config->flow_max_port_to_track)
    {
        proto.dst[dport] += len;
    }

    else if (sport <= config->flow_max_port_to_track &&
        dport <= config->flow_max_port_to_track)
    {
        proto.src[sport] += len;
        proto.dst[dport] += len;
    }

    else
    {
        proto.high += len;
    }
}

#ifdef UNIT_TEST
class MockFlowTracker : public FlowTracker
{
public:
    PerfFormatter* output;

    MockFlowTracker(PerfConfig* config) : FlowTracker(config)
    { output = formatter; };

    void clear() override {};

    void real_clear() { FlowTracker::clear(); };
};

TEST_CASE("no protocol", "[FlowTracker]")
{
    Packet p;
    DAQ_PktHdr_t pkth;
    uint32_t* len_ptr = (uint32_t*) &pkth.caplen;

    PerfConfig config;
    config.format = PERF_MOCK;
    config.flow_max_port_to_track = 1024;

    MockFlowTracker tracker(&config);
    MockFormatter *f = (MockFormatter*)tracker.output;

    tracker.reset();

    p.packet_flags = 0;
    p.ptrs.tcph = nullptr;
    p.ptrs.udph = nullptr;
    p.ptrs.icmph = nullptr;
    p.pkth = &pkth;

    *len_ptr = 127;
    tracker.update(&p);

    *len_ptr = 256;
    tracker.update(&p);
    tracker.update(&p);

    *len_ptr = 32000;
    tracker.update(&p);

    tracker.process(false);
    CHECK( *f->public_values["flow.byte_total"].pc == 32639 );
    CHECK( f->public_values["flow.packets_by_bytes"].ipc->at(123) == 0 );
    CHECK( f->public_values["flow.packets_by_bytes"].ipc->at(127) == 1 );
    CHECK( f->public_values["flow.packets_by_bytes"].ipc->at(256) == 2 );
    CHECK( *f->public_values["flow.oversized_packets"].pc == 1 );

    tracker.real_clear();
    CHECK( *f->public_values["flow.byte_total"].pc == 0);
    CHECK( f->public_values["flow.packets_by_bytes"].ipc->at(123) == 0 );
    CHECK( f->public_values["flow.packets_by_bytes"].ipc->at(127) == 0 );
    CHECK( f->public_values["flow.packets_by_bytes"].ipc->at(256) == 0 );
    CHECK( *f->public_values["flow.oversized_packets"].pc == 0 );
}

TEST_CASE("icmp", "[FlowTracker]")
{
    Packet p;
    DAQ_PktHdr_t pkth;
    icmp::ICMPHdr icmp;
    uint32_t* len_ptr = (uint32_t*) &pkth.caplen;
    uint8_t* type_ptr = (uint8_t*) &icmp.type;

    PerfConfig config;
    config.format = PERF_MOCK;
    config.flow_max_port_to_track = 1024;

    MockFlowTracker tracker(&config);
    MockFormatter *f = (MockFormatter*)tracker.output;

    tracker.reset();

    p.packet_flags = 0;
    p.ptrs.tcph = nullptr;
    p.ptrs.udph = nullptr;
    p.ptrs.icmph = &icmp;
    p.pkth = &pkth;

    *len_ptr = 127;
    *type_ptr = 3;
    tracker.update(&p);

    *len_ptr = 256;
    *type_ptr = 9;
    tracker.update(&p);
    tracker.update(&p);

    *len_ptr = 32000;
    *type_ptr = 127;
    tracker.update(&p);

    tracker.process(false);
    CHECK( f->public_values["flow_icmp.bytes_by_type"].ipc->at(3) == 127 );
    CHECK( f->public_values["flow_icmp.bytes_by_type"].ipc->at(9) == 512 );
    CHECK( f->public_values["flow_icmp.bytes_by_type"].ipc->at(127) == 32000 );

    tracker.real_clear();
    CHECK( f->public_values["flow_icmp.bytes_by_type"].ipc->at(3) == 0 );
    CHECK( f->public_values["flow_icmp.bytes_by_type"].ipc->at(9) == 0 );
    CHECK( f->public_values["flow_icmp.bytes_by_type"].ipc->at(127) == 0 );
}

TEST_CASE("tcp", "[FlowTracker]")
{
    Packet p;
    DAQ_PktHdr_t pkth;
    tcp::TCPHdr tcp;
    uint32_t* len_ptr = (uint32_t*) &pkth.caplen;

    PerfConfig config;
    config.format = PERF_MOCK;
    config.flow_max_port_to_track = 1024;

    MockFlowTracker tracker(&config);
    MockFormatter *f = (MockFormatter*)tracker.output;

    p.packet_flags = 0;
    p.ptrs.tcph = &tcp;
    p.ptrs.udph = nullptr;
    p.ptrs.icmph = nullptr;
    p.pkth = &pkth;

    tracker.reset();

    *len_ptr = 127;
    p.ptrs.sp = 1024;
    p.ptrs.dp = 1025;
    tracker.update(&p);

    *len_ptr = 256;
    p.ptrs.dp = 1024;
    p.ptrs.sp = 1025;
    tracker.update(&p);
    tracker.update(&p);

    *len_ptr = 512;
    p.ptrs.dp = 1024;
    p.ptrs.sp = 1024;
    tracker.update(&p);
    tracker.update(&p);
    tracker.update(&p);

    *len_ptr = 32000;
    p.ptrs.dp = 1025;
    p.ptrs.sp = 1025;
    tracker.update(&p);

    tracker.process(false);
    CHECK( f->public_values["flow_tcp.bytes_by_source"].ipc->at(1024) == 1663 );
    CHECK( f->public_values["flow_tcp.bytes_by_dest"].ipc->at(1024) == 2048 );
    CHECK( *f->public_values["flow_tcp.high_port_bytes"].pc == 32000 );

    tracker.real_clear();
    CHECK( f->public_values["flow_tcp.bytes_by_source"].ipc->at(1024) == 0 );
    CHECK( f->public_values["flow_tcp.bytes_by_dest"].ipc->at(1024) == 0 );
    CHECK( *f->public_values["flow_tcp.high_port_bytes"].pc == 0 );
}

TEST_CASE("udp", "[FlowTracker]")
{
    Packet p;
    DAQ_PktHdr_t pkth;
    udp::UDPHdr udp;
    uint32_t* len_ptr = (uint32_t*) &pkth.caplen;

    PerfConfig config;
    config.format = PERF_MOCK;
    config.flow_max_port_to_track = 1024;

    MockFlowTracker tracker(&config);
    MockFormatter *f = (MockFormatter*)tracker.output;

    p.packet_flags = 0;
    p.ptrs.tcph = nullptr;
    p.ptrs.udph = &udp;
    p.ptrs.icmph = nullptr;
    p.pkth = &pkth;

    tracker.reset();

    *len_ptr = 127;
    p.ptrs.sp = 1024;
    p.ptrs.dp = 1025;
    tracker.update(&p);

    *len_ptr = 256;
    p.ptrs.dp = 1024;
    p.ptrs.sp = 1025;
    tracker.update(&p);
    tracker.update(&p);

    *len_ptr = 512;
    p.ptrs.dp = 1024;
    p.ptrs.sp = 1024;
    tracker.update(&p);
    tracker.update(&p);
    tracker.update(&p);

    *len_ptr = 32000;
    p.ptrs.dp = 1025;
    p.ptrs.sp = 1025;
    tracker.update(&p);

    tracker.process(false);
    CHECK( f->public_values["flow_udp.bytes_by_source"].ipc->at(1024) == 1663 );
    CHECK( f->public_values["flow_udp.bytes_by_dest"].ipc->at(1024) == 2048 );
    CHECK( *f->public_values["flow_udp.high_port_bytes"].pc == 32000 );

    tracker.real_clear();
    CHECK( f->public_values["flow_udp.bytes_by_source"].ipc->at(1024) == 0 );
    CHECK( f->public_values["flow_udp.bytes_by_dest"].ipc->at(1024) == 0 );
    CHECK( *f->public_values["flow_udp.high_port_bytes"].pc == 0 );
}
#endif
