#ifndef STRACK_FLOW_H
#define STRACK_FLOW_H

/**
 * STRACK Flow - Simplified implementation based on htsim's STrack algorithm
 * 
 * STrack is a delay-based congestion control algorithm that:
 * 1. Uses target delay to control cwnd
 * 2. Increases cwnd when delay < target, decreases when delay > target
 * 3. Uses multiplicative decrease with a max factor
 */

#include "../coresim/flow.h"
#include "../coresim/node.h"
#include "../coresim/packet.h"

#include <map>

class STrackFlow : public Flow {
public:
    STrackFlow(uint32_t id, double start_time, uint32_t size, Host* s, Host* d);
    virtual ~STrackFlow();
    
    virtual void start_flow();
    virtual void receive(Packet *p);
    virtual void send_pending_data();
    virtual void handle_timeout();
    virtual uint32_t get_priority(uint32_t seq);
    
    // STrack-specific congestion control
    void adjust_cwnd(double measured_delay);
    void handle_ack_strack(uint32_t ack_seq, double delay);
    
private:
    // STrack CC parameters (inspired by htsim)
    double _alpha;          // Increase constant (0.25 from paper)
    double _beta;           // Decrease constant (0.8)
    double _max_mdf;        // Max multiplicative decrease factor (0.5)
    double _base_delay;     // Configured base target delay
    double _h;              // Path length scaling constant
    
    // RTT tracking
    double _rtt;            // Smoothed RTT
    double _min_rtt;        // Minimum observed RTT (base RTT)
    double _mdev;           // RTT mean deviation
    
    // Congestion window (in bytes)
    uint32_t _strack_cwnd;
    uint32_t _min_cwnd;
    uint32_t _max_cwnd;
    
    // State tracking
    bool _can_decrease;     // Can we decrease cwnd this RTT?
    double _last_decrease;  // Time of last cwnd decrease
    uint32_t _dupacks;      // Duplicate ACK counter
    bool _in_fast_recovery;
    uint64_t _recoverq;     // Recovery sequence number
    uint32_t _inflate;      // cwnd inflation during fast recovery
    
    // For packet timing
    std::map<uint32_t, double> _send_times;  // Map seq -> send time
};

#endif
