#ifndef SCHEDULING_HOST_H
#define SCHEDULING_HOST_H

#include <set>
#include <queue>

#include "../coresim/node.h"
#include "../coresim/packet.h"
#include "../coresim/event.h"
//#include "../coresim/snshost.h"


class Flow;
class SchedulingHost;
class HostProcessingEvent;
class SnsSimulationHostProcessingEvent;
class SnsSendPacketProcessingEvent;
class SnsAddToPktToBeSentEvt;
class SnsA;
class SnsHost;


class HostFlowComparator {
    public:
        bool operator() (Flow* a, Flow* b);
};


class SchedulingHost : public Host {
    public:
        SchedulingHost(uint32_t id, double rate, uint32_t queue_type);
        virtual void start(Flow* f);
        virtual void send();
        std::priority_queue<Flow*, std::vector<Flow*>, HostFlowComparator> sending_flows;
        HostProcessingEvent* host_proc_event;
        SnsSimulationHostProcessingEvent* host_proc_sns_sim_event;
        SnsSendPacketProcessingEvent* host_proc_sns_send_packet_event;
        SnsAddToPktToBeSentEvt* host_proc_sns_add_to_pkt_to_be_sent_event;
};

#define HOST_PROCESSING 10
class HostProcessingEvent : public Event {
    public:
        HostProcessingEvent(double time, SchedulingHost *host);
        ~HostProcessingEvent();
        void process_event();
        SchedulingHost *host;
        bool is_timeout;
};

class SnsSimulationHostProcessingEvent : public Event {
    public:
        SnsSimulationHostProcessingEvent(double time, SnsHost *host);
        ~SnsSimulationHostProcessingEvent();
        void process_event();
        SnsHost *host;
        bool is_timeout;
};

class SnsSendPacketProcessingEvent : public Event {
    public:
        SnsSendPacketProcessingEvent(double time, SnsHost *h);
        ~SnsSendPacketProcessingEvent();
        void process_event();
        SnsHost *host;
        bool is_timeout;
};

class SnsAddToPktToBeSentEvt : public Event {
    public:
        SnsAddToPktToBeSentEvt(double time, SnsHost *h, SnsPacket *pkt);
        ~SnsAddToPktToBeSentEvt();
        void process_event();
        SnsHost *host;
        SnsPacket *pkt;
        bool is_timeout;
};

#endif
