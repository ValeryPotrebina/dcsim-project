#ifndef NDP_FLOW_H
#define NDP_FLOW_H

/**
 * NDP Flow - Simplified implementation based on htsim's NDP algorithm
 * 
 * NDP (New Data Protocol) is a receiver-driven transport protocol that:
 * 1. Sender sends initial window of packets
 * 2. Receiver sends PULLs to request retransmissions or new data
 * 3. Uses packet trimming at switches (simplified here as drops)
 * 4. Uses NACKs for lost packets
 */

#include "../coresim/flow.h"
#include "../coresim/node.h"
#include "../coresim/packet.h"

#include <set>
#include <map>
#include <queue>

// NDP-specific packet types
#define NDP_DATA_PACKET   50
#define NDP_ACK_PACKET    51
#define NDP_NACK_PACKET   52
#define NDP_PULL_PACKET   53

class NdpPacketSim : public Packet {
public:
    int ndp_type;  // NDP_DATA_PACKET, NDP_ACK_PACKET, NDP_NACK_PACKET, NDP_PULL_PACKET
    uint32_t pull_seq;  // For PULL packets: which seq to send
    bool is_header;     // If this is a trimmed packet (header only)
    
    NdpPacketSim(int ndp_type, double sending_time, Flow *flow, uint32_t seq_no, 
                 uint32_t priority, uint32_t size, Host *src, Host *dst);
};

class NdpFlow : public Flow {
public:
    NdpFlow(uint32_t id, double start_time, uint32_t size, Host* s, Host* d);
    virtual ~NdpFlow();
    
    virtual void start_flow();
    virtual void receive(Packet *p);
    virtual void send_pending_data();
    virtual void handle_timeout();
    virtual uint32_t get_priority(uint32_t seq);
    
    // NDP-specific methods
    void send_data_packet(uint32_t seq);
    void send_ack_or_nack(uint32_t seq, bool is_nack);
    void send_pull(uint32_t pull_seq);
    void process_ack(uint32_t seq);
    void process_nack(uint32_t seq);
    void process_pull(uint32_t pull_seq);
    
private:
    // NDP state
    uint32_t _cwnd;              // Initial window (packets)
    uint32_t _flight_size;       // Packets in flight
    
    // Sender-side
    std::set<uint32_t> _sent_seqs;       // Sequences that have been sent
    std::set<uint32_t> _acked_seqs;      // Sequences that have been acked
    std::queue<uint32_t> _rtx_queue;     // Retransmission queue (from NACKs)
    uint32_t _highest_sent;              // Highest sequence sent
    int _pull_window;                    // Expected pulls
    
    // Receiver-side
    std::set<uint32_t> _received_seqs;   // Sequences received
    uint32_t _cumulative_ack;            // Cumulative ACK number
    int _pulls_to_send;                  // Pending pulls to send
    
    // RTT tracking
    double _rtt;
    double _rto;
    std::map<uint32_t, double> _send_times;
};

#endif
