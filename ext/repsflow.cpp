/**
 * REPS Flow - Simplified implementation based on htsim's REPS algorithm
 * 
 * This implements randomized entropy path selection for load balancing.
 */

#include "repsflow.h"
#include "../coresim/event.h"
#include "../run/params.h"

#include <algorithm>
#include <iostream>
#include <cstdlib>

extern double get_current_time();
extern void add_to_event_queue(Event* ev);
extern DCExpParams params;

// RepsEntropyBuffer implementation
RepsEntropyBuffer::RepsEntropyBuffer(uint16_t size) {
    _max_size = size;
    _buffer.resize(size, 0);
    _valid.resize(size, false);
    _head = 0;
    _tail = 0;
    _count = 0;
    _frozen_mode = false;
    _frozen_entropy = 0;
}

RepsEntropyBuffer::~RepsEntropyBuffer() {}

void RepsEntropyBuffer::add(uint16_t entropy) {
    if (_frozen_mode) return;
    
    _buffer[_head] = entropy;
    _valid[_head] = true;
    _head = (_head + 1) % _max_size;
    
    if (_count < _max_size) {
        _count++;
    } else {
        _tail = (_tail + 1) % _max_size;
    }
}

uint16_t RepsEntropyBuffer::getNextEntropy() {
    if (_frozen_mode && _frozen_entropy != 0) {
        return _frozen_entropy;
    }
    
    // Look for a valid (successful) entropy
    for (uint16_t i = 0; i < _count; i++) {
        uint16_t idx = (_tail + i) % _max_size;
        if (_valid[idx]) {
            return _buffer[idx];
        }
    }
    
    // No valid entropy found, generate new random one
    return (uint16_t)(rand() % 65536);
}

void RepsEntropyBuffer::markSuccess(uint16_t entropy) {
    // Add successful entropy to buffer
    add(entropy);
    
    // In frozen mode, update frozen entropy
    if (_frozen_mode) {
        _frozen_entropy = entropy;
    }
}

void RepsEntropyBuffer::reset() {
    _head = 0;
    _tail = 0;
    _count = 0;
    _frozen_mode = false;
    _frozen_entropy = 0;
    for (uint16_t i = 0; i < _max_size; i++) {
        _valid[i] = false;
    }
}

// RepsPacket implementation
RepsPacket::RepsPacket(double sending_time, Flow *flow, uint32_t seq_no, 
                       uint32_t priority, uint32_t size, Host *src, Host *dst,
                       uint16_t entropy)
    : Packet(sending_time, flow, seq_no, priority, size, src, dst) {
    this->entropy = entropy;
    this->is_ack = false;
}

// RepsFlow implementation
RepsFlow::RepsFlow(uint32_t id, double start_time, uint32_t size, Host* s, Host* d) 
    : Flow(id, start_time, size, s, d), _entropy_buffer(8) {
    
    _consecutive_losses = 0;
    _last_ack_time = 0;
    _in_congestion = false;
    _entropy_reuses = 0;
    _new_entropies = 0;
    
    // Use standard TCP cwnd
    this->cwnd_mss = params.initial_cwnd;
    
    // Use larger retx timeout for path-based CC (10x default to handle congestion)
    this->retx_timeout = params.retx_timeout_value * 10;
}

RepsFlow::~RepsFlow() {
    _seq_to_entropy.clear();
}

void RepsFlow::start_flow() {
    if (params.debug_sns)
        std::cout << "RepsFlow::start_flow for flow " << this->id << "\n";
    
    start_time = get_current_time();
    send_pending_data();
    set_timeout(get_current_time() + retx_timeout);
}

uint16_t RepsFlow::getNextEntropy() {
    uint16_t entropy = _entropy_buffer.getNextEntropy();
    
    // Check if this is a reuse or new
    bool found = false;
    for (auto& pair : _seq_to_entropy) {
        if (pair.second == entropy) {
            found = true;
            break;
        }
    }
    
    if (found) {
        _entropy_reuses++;
    } else {
        _new_entropies++;
    }
    
    return entropy;
}

Packet* RepsFlow::send(uint32_t seq) {
    uint32_t pkt_size;
    if (seq + mss > this->size) {
        pkt_size = this->size - seq + hdr_size;
    } else {
        pkt_size = mss + hdr_size;
    }
    
    uint32_t priority = get_priority(seq);
    uint16_t entropy = getNextEntropy();
    
    RepsPacket* p = new RepsPacket(
        get_current_time(),
        this,
        seq,
        priority,
        pkt_size,
        src,
        dst,
        entropy
    );
    
    // Track entropy for this sequence
    _seq_to_entropy[seq] = entropy;
    
    this->total_pkt_sent++;
    add_to_event_queue(new PacketQueuingEvent(get_current_time(), p, src->queue));
    
    if (params.debug_sns) {
        std::cout << "RepsFlow " << id << ": sent seq=" << seq 
                  << " entropy=" << entropy << "\n";
    }
    
    return p;
}

void RepsFlow::send_pending_data() {
    if (finished) return;
    
    if (params.debug_sns) {
        std::cout << "RepsFlow::send_pending_data flowid " << this->id 
                  << " cwnd=" << cwnd_mss << "\n";
    }
    
    if (received_bytes < size) {
        uint32_t seq = next_seq_no;
        uint32_t window = cwnd_mss * mss + scoreboard_sack_bytes;
        
        while (
            (seq + mss <= last_unacked_seq + window) &&
            ((seq + mss <= size) || (seq != size && (size - seq < mss)))
        ) {
            if (received.count(seq) == 0) {
                send(seq);
            }

            if (seq + mss < size) {
                next_seq_no = seq + mss;
                seq += mss;
            } else {
                next_seq_no = size;
                seq = size;
            }

            if (retx_event == NULL) {
                set_timeout(get_current_time() + retx_timeout);
            }
        }
    }
}

void RepsFlow::processAckWithEntropy(uint32_t ack_seq, uint16_t entropy) {
    // Mark this entropy as successful
    _entropy_buffer.markSuccess(entropy);
    
    // Reset congestion state on success
    _consecutive_losses = 0;
    _last_ack_time = get_current_time();
    
    if (_in_congestion) {
        _in_congestion = false;
        _entropy_buffer.setFrozenMode(false);
    }
    
    // Clean up entropy tracking for ALL acked packets (cumulative ACK)
    // Use efficient range erase since map is ordered by sequence number
    auto it_end = _seq_to_entropy.lower_bound(ack_seq);
    _seq_to_entropy.erase(_seq_to_entropy.begin(), it_end);
}

void RepsFlow::checkCongestion() {
    // Detect congestion based on consecutive losses
    if (_consecutive_losses >= 3 && !_in_congestion) {
        _in_congestion = true;
        _entropy_buffer.setFrozenMode(true);
        
        if (params.debug_sns) {
            std::cout << "RepsFlow " << id << ": entering congestion mode\n";
        }
    }
}

void RepsFlow::receive(Packet* p) {
    if (p->type == ACK_PACKET) {
        Ack* ack = (Ack*)p;
        uint32_t ack_seq = ack->seq_no;
        
        // Get entropy for this ACK (from the original packet)
        uint16_t entropy = 0;
        auto it = _seq_to_entropy.find(ack_seq > mss ? ack_seq - mss : 0);
        if (it != _seq_to_entropy.end()) {
            entropy = it->second;
        }
        
        // Process REPS-specific ACK handling
        if (ack_seq > last_unacked_seq) {
            // New ACK - mark entropy as successful
            processAckWithEntropy(ack_seq, entropy);
        } else {
            // Duplicate ACK - potential loss
            _consecutive_losses++;
            checkCongestion();
        }
        
        // Standard TCP-like processing
        this->scoreboard_sack_bytes = ack->sack_list.size() * mss;

        if (next_seq_no < ack_seq) {
            next_seq_no = ack_seq;
        }

        if (ack_seq > last_unacked_seq) {
            last_unacked_seq = ack_seq;
            
            // Clean up received set for sequences that have been cumulatively ACKed
            received.erase(received.begin(), received.lower_bound(ack_seq));
            
            increase_cwnd();
            send_pending_data();

            if (retx_event != NULL) {
                cancel_retx_event();
                if (last_unacked_seq < size) {
                    set_timeout(get_current_time() + retx_timeout);
                }
            }
        }

        if (ack_seq == size && !finished) {
            finished = true;
            received.clear();
            finish_time = get_current_time();
            flow_completion_time = finish_time - start_time;
            FlowFinishedEvent *ev = new FlowFinishedEvent(get_current_time(), this);
            add_to_event_queue(ev);
            
            if (params.debug_sns) {
                std::cout << "RepsFlow " << id << ": finished, entropy_reuses=" 
                          << _entropy_reuses << " new_entropies=" << _new_entropies << "\n";
            }
        }
    } else if (p->type == NORMAL_PACKET) {
        // Receiving data at destination
        if (finished) {
            delete p;
            return;
        }
        
        if (first_byte_receive_time == -1) {
            first_byte_receive_time = get_current_time();
        }
        
        // Process data packet
        uint32_t seq = p->seq_no;
        if (received.count(seq) == 0) {
            received.insert(seq);
            received_bytes += (p->size - hdr_size);
        }
        
        // Send ACK
        std::vector<uint32_t> sack_list;
        send_ack(received_bytes, sack_list);
        
        // Check completion
        if (received_bytes >= size && !finished) {
            finished = true;
            received.clear();
            finish_time = get_current_time();
            flow_completion_time = finish_time - start_time;
            FlowFinishedEvent *ev = new FlowFinishedEvent(get_current_time(), this);
            add_to_event_queue(ev);
        }
    } else {
        Flow::receive(p);
    }
}

uint32_t RepsFlow::get_priority(uint32_t seq) {
    return 1;
}

void RepsFlow::handle_timeout() {
    if (params.debug_sns) {
        std::cout << "RepsFlow::handle_timeout for flow " << this->id << "\n";
    }
    
    // Reset REPS state on timeout
    _entropy_buffer.reset();
    _entropy_buffer.setFrozenMode(false);
    _consecutive_losses = 0;
    _in_congestion = false;
    
    // Clear sequence to entropy mapping
    _seq_to_entropy.clear();
    
    // Call base class timeout (resets next_seq_no and calls send_pending_data)
    Flow::handle_timeout();
}
