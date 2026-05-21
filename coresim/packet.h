#ifndef PACKET_H
#define PACKET_H

#include "flow.h"
#include "node.h"
#include <vector>
#include <list>
#include <stdint.h>
// TODO: Change to Enum
#define NORMAL_PACKET 0
#define ACK_PACKET 1

#define RTS_PACKET 3
#define CTS_PACKET 4
#define OFFER_PACKET 5
#define DECISION_PACKET 6
#define CAPABILITY_PACKET 7
#define STATUS_PACKET 8
#define FASTPASS_RTS 9
#define FASTPASS_SCHEDULE 10

// SNS - not need it now, will use var sns_is_sim
#define SNS_SIM_PACKET 23
#define SNS_SIM_ACK_PACKET 24
#define SNS_DATA_PACKET 25
#define SNS_DATA_ACK_PACKET 26
// 1540 / 40 = 38.5
//#define SNS_SIMULATION_FRAQ_SIZE 38.5
// 1580/80 = 39.5
// #define SNS_SIMULATION_FRAQ_SIZE 37.5
// 4000 / 40 = 100
// #define SNS_SIMULATION_FRAQ_SIZE 100
// 9000 / 40 = 225 Jumbo frame
#define SNS_SIMULATION_FRAQ_SIZE 225
// 38.5* 4.5 (4.5 = 1400/324)
//#define SNS_SIMULATION_QUEUE_DIV_SIZE 173
// 324/30 (we want only 30 packets in simulation)
// 324/30*38.5 (the 38.5 give us the 324)
//#define SNS_SIMULATION_QUEUE_DIV_SIZE 415.8
//#define SNS_SIMULATION_QUEUE_DIV_SIZE 831.6
//Next line is 8 simulation limit queue
//#define SNS_SIMULATION_QUEUE_DIV_SIZE 1559.25

//#define SNS_SIMULATION_QUEUE_DIV_SIZE 38.5
//#define SNS_SIMULATION_QUEUE_DIV_SIZE 38.5


// RUF
#define RUF_RTS 11
#define RUF_LISTSRCS 12
#define RUF_NRTS 13
#define RUF_GOSRC 14
#define RUF_TOKEN 15

// PIM
#define PIM_GRANTS_PACKET 16
#define PIM_ACK 17
#define GRANTSR_PACKET 18
#define PIM_REQ_PACKET 19
#define ACCEPT_PACKET 20
#define FLOW_RTS 21
#define PIM_TOKEN 22

class FastpassEpochSchedule;

class Packet {

    public:
        Packet(double sending_time, Flow *flow, uint32_t seq_no, uint32_t pf_priority,
                uint32_t size, Host *src, Host *dst);
        virtual ~Packet() = default;
        double sending_time;
        Flow *flow;
        uint32_t seq_no;
        uint32_t pf_priority;
        uint32_t size;
        Host *src;
        Host *dst;
        uint32_t unique_id;
        static uint32_t instance_count;
        int remaining_pkts_in_batch;
        int capability_seq_num_in_data;

        uint32_t type; // Normal or Ack packet
        uint32_t sns_type; // Is data packet or simulation packet
        double total_queuing_delay;
        double last_enque_time;

        int capa_data_seq;
        int hop;

        int sns_msg_id;
        int saved_hash_port; //for sns
        int saved_hash_port_l2; //for sns
        int sns_current_active_flows;
        std::multiset<uint32_t> sns_ack_data_ids;
        double sim_enter_q1;
        double sim_enter_q2;
        double data_enter_q2;
};

class PlainAck : public Packet {
    public:
        PlainAck(Flow *flow, uint32_t seq_no_acked, uint32_t size, Host* src, Host* dst);
};

class Ack : public Packet {
    public:
        Ack(Flow *flow, uint32_t seq_no_acked, std::vector<uint32_t> sack_list,
                uint32_t size,
                Host* src, Host *dst);
        Ack(Flow *flow, uint32_t seq_no_acked, std::vector<uint32_t> sack_list,
                uint32_t size,
                Host* src, Host *dst, uint32_t seq_for_sns);
        uint32_t sack_bytes;
        std::vector<uint32_t> sack_list;
};

class RTSCTS : public Packet {
    public:
        //type: true if RTS, false if CTS
        RTSCTS(bool type, double sending_time, Flow *f, uint32_t size, Host *src, Host *dst);
};

class SnsPacket : public Packet {
    public:
        SnsPacket(int type, double sending_time, Flow *f, uint32_t seq, uint32_t priority, uint32_t size, Host *src, Host *dst, int saved_hash_port, int msg_id);
        

        // Packet SnsSimOrData(bool type, double sending_time, Flow *f,
        //                 uint32_t size, Host *src,
        //                 Host *dst, int saved_hash_port);
};


class RTS : public Packet{
    public:
        RTS(Flow *flow, Host *src, Host *dst, double delay, int iter);
        double delay;
        int iter;
};

class OfferPkt : public Packet{
    public:
        OfferPkt(Flow *flow, Host *src, Host *dst, bool is_free, int iter, int epoch);
        bool is_free;
        int iter;
        int epoch;
};

class DecisionPkt : public Packet{
    public:
        DecisionPkt(Flow *flow, Host *src, Host *dst, bool accept, int iter, int epoch);
        bool accept;
        int iter;
        int epoch;
};


class CTS : public Packet{
    public:
        CTS(Flow *flow, Host *src, Host *dst);
};

// For Multi-Round algorithm (PIM)
class FlowRTS : public Packet
{
    public:
        FlowRTS(Flow *flow, Host *src, Host *dst, int size_in_pkt);
        int size_in_pkt;
};

class PIMREQ : public Packet{
    public:
        PIMREQ(Flow *flow, Host *src, Host *dst, int iter, int epoch, int remaining, int total_links, int prompt_links);
        int iter;
        int epoch;
        int remaining_sz;
        int total_links;
        int prompt_links;
};

class GrantsR : public Packet{
    public:
        GrantsR(Flow *flow, Host *src, Host *dst, int iter, int epoch, int total_links, int prompt_links);
        int iter;
        int epoch;
        int total_links;
        int prompt_links;
};

class AcceptPkt : public Packet{
    public:
        AcceptPkt(Flow *flow, Host *src, Host *dst, int iter, int epoch, int total_links, int prompt_links);
        int iter;
        int epoch;
        int total_links;
        int prompt_links;
};

class PIMGrants : public Packet{
    public:
        PIMGrants(Flow *flow, Host *src, Host *dst, int iter, int epoch, int remaining_sz, int total_links, int prompt_links);
        int iter;
        int epoch;
        int remaining_sz;
        int total_links;
        int prompt_links;
        // bool prompt;
};

class PIMAck : public Packet {
    public:
        PIMAck(Flow *flow, uint32_t seq_no_acked, uint32_t data_seq_num, uint32_t size, Host* src, Host* dst);
        uint32_t data_seq_no_acked;
};

class PIMToken : public Packet
{
    public:
        PIMToken(Flow *flow, Host *src, Host *dst, double ttl, int remaining, int token_seq_num, int data_seq_num, int priority);
        double ttl;
        int remaining_sz;
        int token_seq_num;
        int data_seq_num;
        int priority;
};

class CapabilityPkt : public Packet{
    public:
        CapabilityPkt(Flow *flow, Host *src, Host *dst, double ttl, int remaining, int cap_seq_num, int data_seq_num);
        double ttl;
        int remaining_sz;
        int cap_seq_num;
        int data_seq_num;
};

class StatusPkt : public Packet{
    public:
        StatusPkt(Flow *flow, Host *src, Host *dst, int num_flows_at_sender);
        double ttl;
        bool num_flows_at_sender;
};


class FastpassRTS : public Packet
{
    public:
        FastpassRTS(Flow *flow, Host *src, Host *dst, int remaining_pkt);
        int remaining_num_pkts;
};

class FastpassSchedulePkt : public Packet
{
    public:
        FastpassSchedulePkt(Flow *flow, Host *src, Host *dst, FastpassEpochSchedule* schd);
        FastpassEpochSchedule* schedule;
};
// Ruf Algorithm
class RufRTS : public Packet
{
    public:
        RufRTS(Flow *flow, Host *src, Host *dst, int size_in_pkt);
        int size_in_pkt;
};

class RufListSrcs : public Packet
{
    public:
        RufListSrcs(Flow *flow, Host *src, Host *dst, Host* rts_dst, std::list<uint32_t> listSrcs);
        ~RufListSrcs();
        std::list<uint32_t> listSrcs;
        std::list<uint32_t> flowSizes;
        Host* rts_dst;
        bool has_nrts;
        int nrts_src_id;
        int nrts_dst_id;
        int round;
};

class RufNRTS : public Packet
{
    public:
        RufNRTS(Flow *flow, Host *src, Host *dst, uint32_t src_id, uint32_t dst_id);
        uint32_t src_id;
        uint32_t dst_id;

};

class RufGoSrc : public Packet
{
    public:
        RufGoSrc(Flow *flow, Host *src, Host *dst, uint32_t src_id, uint32_t max_tokens, int round);
        uint32_t src_id;
        uint32_t max_tokens;
        int round;
};

class RufToken : public Packet
{
    public:
        RufToken(Flow *flow, Host *src, Host *dst, double ttl, int remaining, int token_seq_num, int data_seq_num);
        double ttl;
        int remaining_sz;
        int token_seq_num;
        int data_seq_num;
};

#endif

