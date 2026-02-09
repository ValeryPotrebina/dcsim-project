/**
 * NDP Flow - Simplified implementation based on htsim's NDP algorithm
 * 
 * This implements a receiver-driven protocol where:
 * - Sender sends initial window
 * - Receiver pulls data and sends ACKs/NACKs
 */

#include "ndpflow.h"
#include "../coresim/event.h"
#include "../run/params.h"

#include <algorithm>
#include <iostream>

extern double get_current_time();
extern void add_to_event_queue(Event* ev);
extern DCExpParams params;

// NdpPacketSim implementation
NdpPacketSim::NdpPacketSim(int ndp_type, double sending_time, Flow *flow, 
                           uint32_t seq_no, uint32_t priority, uint32_t size, 
                           Host *src, Host *dst)
    : Packet(sending_time, flow, seq_no, priority, size, src, dst) {
    this->ndp_type = ndp_type;
    this->pull_seq = 0;
    this->is_header = false;
    this->type = NORMAL_PACKET;  // For queue processing
}

// NdpFlow implementation
NdpFlow::NdpFlow(uint32_t id, double start_time, uint32_t size, Host* s, Host* d) 
    : Flow(id, start_time, size, s, d) {
    
    // NDP uses a fixed initial window (15 packets is common)
    _cwnd = 15;
    _flight_size = 0;
    _highest_sent = 0;
    _pull_window = 0;
    
    // Receiver state
    _cumulative_ack = 0;
    _pulls_to_send = 0;
    
    // RTT
    _rtt = params.rtt;
    _rto = 0.020;  // 20ms default RTO
    
    // Use larger retx timeout (10x default to handle congestion)
    this->retx_timeout = params.retx_timeout_value * 10;
}

NdpFlow::~NdpFlow() {
    _send_times.clear();
}

void NdpFlow::start_flow() {
    if (params.debug_sns)
        std::cout << "NdpFlow::start_flow for flow " << this->id << "\n";
    
    start_time = get_current_time();
    
    // Send initial window
    send_pending_data();
    
    set_timeout(get_current_time() + _rto);
}

void NdpFlow::send_data_packet(uint32_t seq) {
    if (seq >= size) return;
    
    uint32_t pkt_size;
    if (seq + mss > size) {
        pkt_size = size - seq + hdr_size;
    } else {
        pkt_size = mss + hdr_size;
    }
    
    NdpPacketSim* p = new NdpPacketSim(
        NDP_DATA_PACKET,
        get_current_time(),
        this,
        seq,
        get_priority(seq),
        pkt_size,
        src,
        dst
    );
    
    _send_times[seq] = get_current_time();
    _sent_seqs.insert(seq);
    _flight_size++;
    _pull_window++;
    
    if (seq > _highest_sent) {
        _highest_sent = seq;
    }
    
    add_to_event_queue(new PacketQueuingEvent(get_current_time(), p, src->queue));
    
    if (params.debug_sns) {
        std::cout << "NdpFlow " << id << ": sent data seq=" << seq 
                  << " flight=" << _flight_size << "\n";
    }
}

void NdpFlow::send_pending_data() {
    if (finished) return;
    
    // First, send any retransmissions
    while (!_rtx_queue.empty() && _flight_size < _cwnd) {
        uint32_t rtx_seq = _rtx_queue.front();
        _rtx_queue.pop();
        
        if (_acked_seqs.find(rtx_seq) == _acked_seqs.end()) {
            send_data_packet(rtx_seq);
        }
    }
    
    // Then send new data up to cwnd
    while (_flight_size < _cwnd && next_seq_no < size) {
        send_data_packet(next_seq_no);
        
        if (next_seq_no + mss < size) {
            next_seq_no += mss;
        } else {
            next_seq_no = size;
        }
    }
}

void NdpFlow::send_ack_or_nack(uint32_t seq, bool is_nack) {
    NdpPacketSim* p = new NdpPacketSim(
        is_nack ? NDP_NACK_PACKET : NDP_ACK_PACKET,
        get_current_time(),
        this,
        seq,
        1,
        hdr_size,
        dst,  // ACK/NACK goes from dst to src
        src
    );
    
    add_to_event_queue(new PacketQueuingEvent(get_current_time(), p, dst->queue));
    
    // Also send a PULL to request more data
    send_pull(seq);
}

void NdpFlow::send_pull(uint32_t pull_seq) {
    NdpPacketSim* p = new NdpPacketSim(
        NDP_PULL_PACKET,
        get_current_time(),
        this,
        _cumulative_ack,  // Use cumulative ack as seq
        1,
        hdr_size,
        dst,
        src
    );
    p->pull_seq = pull_seq;
    
    add_to_event_queue(new PacketQueuingEvent(get_current_time(), p, dst->queue));
}

void NdpFlow::process_ack(uint32_t seq) {
    _acked_seqs.insert(seq);
    
    // Update cumulative ack
    while (_acked_seqs.find(_cumulative_ack) != _acked_seqs.end() ||
           _cumulative_ack < seq) {
        if (_cumulative_ack + mss <= size) {
            _cumulative_ack += mss;
        } else {
            _cumulative_ack = size;
            break;
        }
    }
    
    last_unacked_seq = _cumulative_ack;
    
    // Decrease flight size
    if (_flight_size > 0) _flight_size--;
    
    // Clean up send times for this packet
    _send_times.erase(seq);
    
    // Clean up acked_seqs entries that are no longer needed (below cumulative ack)
    // Use efficient range erase since set is ordered
    auto it_end = _acked_seqs.lower_bound(_cumulative_ack);
    _acked_seqs.erase(_acked_seqs.begin(), it_end);
    
    if (params.debug_sns) {
        std::cout << "NdpFlow " << id << ": got ACK seq=" << seq 
                  << " cumulative=" << _cumulative_ack << "\n";
    }
}

void NdpFlow::process_nack(uint32_t seq) {
    // Add to retransmission queue
    if (_acked_seqs.find(seq) == _acked_seqs.end()) {
        _rtx_queue.push(seq);
    }
    
    // Decrease flight size
    if (_flight_size > 0) _flight_size--;
    
    if (params.debug_sns) {
        std::cout << "NdpFlow " << id << ": got NACK seq=" << seq << "\n";
    }
}

void NdpFlow::process_pull(uint32_t pull_seq) {
    if (_pull_window > 0) _pull_window--;
    
    // Send more data in response to pull
    send_pending_data();
}

void NdpFlow::receive(Packet* p) {
    NdpPacketSim* ndp_p = dynamic_cast<NdpPacketSim*>(p);
    
    if (ndp_p == nullptr) {
        // Regular packet handling
        Flow::receive(p);
        return;
    }
    
    switch (ndp_p->ndp_type) {
        case NDP_DATA_PACKET: {
            // Receiving data at destination
            if (finished) {
                delete p;
                return;
            }
            
            if (first_byte_receive_time == -1) {
                first_byte_receive_time = get_current_time();
            }
            
            uint32_t seq = p->seq_no;
            bool is_new = (_received_seqs.find(seq) == _received_seqs.end());
            
            if (is_new) {
                _received_seqs.insert(seq);
                received_bytes += (p->size - hdr_size);
                
                // Update cumulative ACK
                while (_received_seqs.find(_cumulative_ack) != _received_seqs.end()) {
                    if (_cumulative_ack + mss <= size) {
                        _cumulative_ack += mss;
                    } else {
                        _cumulative_ack = size;
                        break;
                    }
                }
                
                // Send ACK
                send_ack_or_nack(seq, false);
            } else {
                // Duplicate - still send ACK
                send_ack_or_nack(seq, false);
            }
            
            // Check completion
            if (received_bytes >= size && !finished) {
                finished = true;
                _received_seqs.clear();
                finish_time = get_current_time();
                flow_completion_time = finish_time - start_time;
                FlowFinishedEvent *ev = new FlowFinishedEvent(get_current_time(), this);
                add_to_event_queue(ev);
            }
            break;
        }
        
        case NDP_ACK_PACKET: {
            process_ack(p->seq_no);
            
            // Check completion at sender side
            if (_cumulative_ack >= size && !finished) {
                finished = true;
                finish_time = get_current_time();
                flow_completion_time = finish_time - start_time;
                FlowFinishedEvent *ev = new FlowFinishedEvent(get_current_time(), this);
                add_to_event_queue(ev);
            }
            break;
        }
        
        case NDP_NACK_PACKET: {
            process_nack(p->seq_no);
            send_pending_data();
            break;
        }
        
        case NDP_PULL_PACKET: {
            process_pull(ndp_p->pull_seq);
            break;
        }
        
        default:
            Flow::receive(p);
            break;
    }
}

uint32_t NdpFlow::get_priority(uint32_t seq) {
    return 1;
}

void NdpFlow::handle_timeout() {
    if (params.debug_sns) {
        std::cout << "NdpFlow::handle_timeout for flow " << this->id 
                  << " cwnd was " << _cwnd << " pkts, flight_size=" << _flight_size << "\n";
    }
    
    // Reset NDP state on timeout
    _cwnd = params.initial_cwnd;
    _flight_size = 0;
    _pull_window = _cwnd;
    
    // Clear retransmission queue and re-add unacked packets
    while (!_rtx_queue.empty()) {
        _rtx_queue.pop();
    }
    
    // Call base class timeout (resets next_seq_no and calls send_pending_data)
    Flow::handle_timeout();
}
