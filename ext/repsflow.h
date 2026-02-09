#ifndef REPS_FLOW_H
#define REPS_FLOW_H

/**
 * REPS Flow - Simplified implementation based on htsim's REPS algorithm
 * 
 * REPS (Randomized Entropy Path Selection) is a load balancing algorithm that:
 * 1. Maintains a circular buffer of path entropies
 * 2. Reuses successful paths based on ACK feedback
 * 3. Has a "frozen mode" for stable path selection during congestion
 */

#include "../coresim/flow.h"
#include "../coresim/node.h"
#include "../coresim/packet.h"

#include <vector>
#include <map>

// REPS circular buffer for entropy values
class RepsEntropyBuffer {
public:
    RepsEntropyBuffer(uint16_t size = 8);
    ~RepsEntropyBuffer();
    
    void add(uint16_t entropy);          // Add new entropy value
    uint16_t getNextEntropy();           // Get next entropy for sending
    void markSuccess(uint16_t entropy);  // Mark entropy as successful
    void reset();
    
    void setFrozenMode(bool mode) { _frozen_mode = mode; }
    bool isFrozenMode() const { return _frozen_mode; }
    
private:
    std::vector<uint16_t> _buffer;
    std::vector<bool> _valid;
    uint16_t _max_size;
    uint16_t _head;
    uint16_t _tail;
    uint16_t _count;
    bool _frozen_mode;
    uint16_t _frozen_entropy;  // Entropy to use in frozen mode
};

// REPS-enabled packet with entropy field
class RepsPacket : public Packet {
public:
    uint16_t entropy;  // Path entropy for ECMP hashing
    bool is_ack;
    
    RepsPacket(double sending_time, Flow *flow, uint32_t seq_no, 
               uint32_t priority, uint32_t size, Host *src, Host *dst,
               uint16_t entropy);
};

class RepsFlow : public Flow {
public:
    RepsFlow(uint32_t id, double start_time, uint32_t size, Host* s, Host* d);
    virtual ~RepsFlow();
    
    virtual void start_flow();
    virtual void receive(Packet *p);
    virtual void send_pending_data();
    virtual Packet* send(uint32_t seq);
    virtual void handle_timeout();
    virtual uint32_t get_priority(uint32_t seq);
    
    // REPS-specific methods
    uint16_t getNextEntropy();
    void processAckWithEntropy(uint32_t ack_seq, uint16_t entropy);
    void checkCongestion();
    
private:
    RepsEntropyBuffer _entropy_buffer;
    
    // Track entropy per packet for ACK processing
    std::map<uint32_t, uint16_t> _seq_to_entropy;
    
    // Congestion detection
    uint32_t _consecutive_losses;
    double _last_ack_time;
    bool _in_congestion;
    
    // Stats
    uint32_t _entropy_reuses;
    uint32_t _new_entropies;
};

#endif
