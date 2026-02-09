/**
 * STRACK Flow - Simplified implementation based on htsim's STrack algorithm
 * 
 * This is a delay-based congestion control that adjusts cwnd based on
 * measured delay vs target delay.
 */

#include "strackflow.h"
#include "../coresim/event.h"
#include "../run/params.h"

#include <algorithm>
#include <cmath>
#include <iostream>

extern double get_current_time();
extern void add_to_event_queue(Event* ev);
extern DCExpParams params;

STrackFlow::STrackFlow(uint32_t id, double start_time, uint32_t size, Host* s, Host* d) 
    : Flow(id, start_time, size, s, d) {
    
    // Initialize STrack CC parameters (values from paper/htsim)
    _alpha = 0.25;      // Increase constant
    _beta = 0.8;        // Decrease constant
    _max_mdf = 0.5;     // Max multiplicative decrease factor
    
    // Base delay ~ target queuing delay 
    // Keep conservative to reduce congestion
    _base_delay = 0.00002;  // 20 microseconds
    _h = _base_delay / 6.55; // Path length scaling
    
    // RTT tracking
    _rtt = 0;
    _min_rtt = params.rtt;  // Use configured RTT as initial min
    _mdev = 0;
    
    // Congestion window initialization
    _strack_cwnd = params.initial_cwnd * mss;
    _min_cwnd = 2 * mss;    // Minimum 2 packets
    _max_cwnd = params.max_cwnd * mss;
    
    // State
    _can_decrease = true;
    _last_decrease = 0;
    _dupacks = 0;
    _in_fast_recovery = false;
    _recoverq = 0;
    _inflate = 0;
    
    // Use larger retx timeout for delay-based CC (10x default to handle congestion)
    this->retx_timeout = params.retx_timeout_value * 10;
    
    // Override cwnd_mss with strack cwnd
    this->cwnd_mss = _strack_cwnd / mss;
}

STrackFlow::~STrackFlow() {
    _send_times.clear();
}

void STrackFlow::start_flow() {
    if (params.debug_sns)
        std::cout << "STrackFlow::start_flow for flow " << this->id << "\n";
    
    // Record start time
    start_time = get_current_time();
    
    // Start sending data
    send_pending_data();
    
    // Set initial timeout
    set_timeout(get_current_time() + retx_timeout);
}

void STrackFlow::send_pending_data() {
    if (finished) return;
    
    if (params.debug_sns) {
        std::cout << "STrackFlow::send_pending_data flowid " << this->id 
                  << " cwnd=" << _strack_cwnd << " bytes, " 
                  << (_strack_cwnd/mss) << " pkts\n";
    }
    
    if (received_bytes < size) {
        uint32_t seq = next_seq_no;
        // Use strack cwnd for window calculation
        uint32_t window = _strack_cwnd + scoreboard_sack_bytes + _inflate;
        
        while (
            (seq + mss <= last_unacked_seq + window) &&
            ((seq + mss <= size) || (seq != size && (size - seq < mss)))
        ) {
            if (received.count(seq) == 0) {
                // Record send time for RTT calculation
                _send_times[seq] = get_current_time();
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

void STrackFlow::adjust_cwnd(double measured_delay) {
    double now = get_current_time();
    
    // Update smoothed RTT (EWMA)
    if (_rtt > 0) {
        double abs_diff = std::abs(measured_delay - _rtt);
        _mdev = 0.75 * _mdev + 0.25 * abs_diff;
        _rtt = 0.875 * _rtt + 0.125 * measured_delay;
    } else {
        _rtt = measured_delay;
        _mdev = measured_delay / 2;
    }
    
    // Update min RTT
    if (measured_delay < _min_rtt) {
        _min_rtt = measured_delay;
    }
    
    // Can we decrease this RTT?
    _can_decrease = (now - _last_decrease) >= _rtt;
    
    // Calculate target delay based on base delay and path length
    double target_delay = _base_delay + _min_rtt;
    
    // STrack congestion control algorithm
    if (measured_delay < target_delay) {
        // Delay is low, increase cwnd
        double increase = _alpha * (target_delay - measured_delay) / measured_delay;
        _strack_cwnd += (uint32_t)(increase * mss);
        
        if (params.debug_sns) {
            std::cout << "STrack: delay=" << measured_delay*1e6 << "us < target=" 
                      << target_delay*1e6 << "us, increasing cwnd by " << increase << "\n";
        }
    } else if (_can_decrease) {
        // Delay is high, decrease cwnd
        double mdf = _beta * (measured_delay - target_delay) / measured_delay;
        mdf = std::min(mdf, _max_mdf);  // Cap at max decrease
        
        uint32_t old_cwnd = _strack_cwnd;
        _strack_cwnd = (uint32_t)(_strack_cwnd * (1.0 - mdf));
        _last_decrease = now;
        
        if (params.debug_sns) {
            std::cout << "STrack: delay=" << measured_delay*1e6 << "us > target=" 
                      << target_delay*1e6 << "us, decreasing cwnd from " 
                      << old_cwnd << " to " << _strack_cwnd << "\n";
        }
    }
    
    // Apply bounds
    if (_strack_cwnd < _min_cwnd) _strack_cwnd = _min_cwnd;
    if (_strack_cwnd > _max_cwnd) _strack_cwnd = _max_cwnd;
    
    // Update cwnd_mss for base class
    this->cwnd_mss = _strack_cwnd / mss;
}

void STrackFlow::handle_ack_strack(uint32_t ack_seq, double delay) {
    // Adjust cwnd based on measured delay
    adjust_cwnd(delay);
    
    if (ack_seq > last_unacked_seq) {
        // New ACK
        _dupacks = 0;
        
        if (_in_fast_recovery && ack_seq >= _recoverq) {
            // Exit fast recovery
            _in_fast_recovery = false;
            _inflate = 0;
        }
        
        last_unacked_seq = ack_seq;
        
        // Clean up received set for sequences that have been cumulatively ACKed
        // Use efficient range erase since set is ordered by sequence number
        received.erase(received.begin(), received.lower_bound(ack_seq));
        
        // Continue sending
        send_pending_data();
        
        // Update retransmission timer
        if (retx_event != NULL) {
            cancel_retx_event();
            if (last_unacked_seq < size) {
                set_timeout(get_current_time() + retx_timeout);
            }
        }
    } else {
        // Duplicate ACK
        _dupacks++;
        
        if (_dupacks == 3 && !_in_fast_recovery) {
            // Enter fast recovery
            _in_fast_recovery = true;
            _recoverq = next_seq_no;
            
            // Retransmit lost packet
            if (received.count(last_unacked_seq) == 0) {
                _send_times[last_unacked_seq] = get_current_time();
                send(last_unacked_seq);
            }
        } else if (_in_fast_recovery) {
            // Inflate cwnd for each dup ack
            _inflate += mss;
            send_pending_data();
        }
    }
    
    // Check if flow is complete
    if (ack_seq == size && !finished) {
        finished = true;
        received.clear();
        finish_time = get_current_time();
        flow_completion_time = finish_time - start_time;
        FlowFinishedEvent *ev = new FlowFinishedEvent(get_current_time(), this);
        add_to_event_queue(ev);
    }
}

void STrackFlow::receive(Packet* p) {
    if (p->type == ACK_PACKET) {
        Ack* ack = (Ack*)p;
        uint32_t ack_seq = ack->seq_no;
        
        // Calculate delay from send time (use the most recently acked packet)
        double delay = 0;
        auto it = _send_times.find(ack_seq > mss ? ack_seq - mss : 0);
        if (it != _send_times.end()) {
            delay = get_current_time() - it->second;
        } else {
            delay = _rtt > 0 ? _rtt : params.rtt;
        }
        
        // Clean up all send_times entries for sequences that have been ACKed
        // Use efficient range erase since map is ordered by sequence number
        auto it_end = _send_times.lower_bound(ack_seq);
        _send_times.erase(_send_times.begin(), it_end);
        
        handle_ack_strack(ack_seq, delay);
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
        // Handle other packet types via base class
        Flow::receive(p);
    }
}

uint32_t STrackFlow::get_priority(uint32_t seq) {
    return 1;
}

void STrackFlow::handle_timeout() {
    if (params.debug_sns) {
        std::cout << "STrackFlow::handle_timeout for flow " << this->id 
                  << " cwnd was " << _strack_cwnd << " bytes\n";
    }
    
    // Reset STrack cwnd to minimum on timeout
    _strack_cwnd = _min_cwnd;
    this->cwnd_mss = _strack_cwnd / mss;
    
    // Reset fast recovery state
    _in_fast_recovery = false;
    _inflate = 0;
    _dupacks = 0;
    
    // Call base class timeout (resets next_seq_no and calls send_pending_data)
    Flow::handle_timeout();
}
