#include <math.h>
#include <iostream>
#include <assert.h>
#include <set>
#include <deque>

#include "flow.h"
#include "packet.h"
#include "event.h"
#include "topology.h"
#include "SnsFatTreeTopology.h"

#include "../run/params.h"
#include "../ext/factory.h"
#include <iomanip>

extern double get_current_time(); 
extern void add_to_event_queue(Event *);
extern int get_event_queue_size();
extern DCExpParams params;
extern long long num_outstanding_packets;
extern long long max_outstanding_packets;
extern uint32_t duplicated_packets_received;
extern std::deque<Flow*> flows_to_schedule;
extern Topology* topology;

Flow::Flow(uint32_t id, double start_time, uint32_t size, Host *s, Host *d) {
    this->id = id;
    this->start_time = start_time;
    this->finish_time = 0;
    this->size = size;
    this->src = s;
    this->dst = d;

    this->next_seq_no = 0;
    this->last_unacked_seq = 0;
    this->retx_event = NULL;
    this->flow_proc_event = NULL;

    this->received_bytes = 0;
    this->recv_till = 0;
    this->max_seq_no_recv = 0;
    this->cwnd_mss = params.initial_cwnd;
    this->max_cwnd = params.max_cwnd;
    this->finished = false;

    //SACK
    this->scoreboard_sack_bytes = 0;

    this->retx_timeout = params.retx_timeout_value;
    this->mss = params.mss;
    this->hdr_size = params.hdr_size;
    this->total_pkt_sent = 0;
    this->size_in_pkt = (int)ceil((double)size/mss);

    this->pkt_drop = 0;
    this->data_pkt_drop = 0;
    this->ack_pkt_drop = 0;
    this->flow_priority = 0;
    this->collective_id = 0;  // Default value when not set
    this->first_byte_send_time = -1;
    this->first_byte_receive_time = -1;
    this->first_hop_departure = 0;
    this->last_hop_departure = 0;
    this->last_id_received = -1;
    this->num_of_reorders = 0;


}

Flow::~Flow() {
    //  packets.clear();
}

void Flow::start_flow() {
    send_pending_data();
}

void Flow::send_pending_data() {
    if (params.debug_sns){
        std::cout << " send_pending_data flowid " << this->id << " next_seq_no:" << next_seq_no << " received_bytes " << received_bytes << " size: " << size << " mss: " << mss << " last_unacked_seq: " << last_unacked_seq << std::endl;
    }
    
    if (received_bytes < size) {
        uint32_t seq = next_seq_no;
        uint32_t window = cwnd_mss * mss + scoreboard_sack_bytes;
        while (
            (seq + mss <= last_unacked_seq + window) &&
            ((seq + mss <= size) || (seq != size && (size - seq < mss)))
        ) {
            // TODO Make it explicit through the SACK list
            if (received.count(seq) == 0) {
                send(seq);
                //std::cout << "received.count(seq)" << received.count(seq) << std::endl;

            }

            if (seq + mss < size) {
                next_seq_no = seq + mss;
                seq += mss;
            } else {
                next_seq_no = size;
                seq = size;
            }

            if (retx_event == NULL) {
                //cancel_retx_event();
                //std::cout << "TIMEOUT " << "\n" << std::endl;
                set_timeout(get_current_time() + retx_timeout);
            }
        }
    }
}

Packet *Flow::send(uint32_t seq) {
    Packet *p = NULL;

    uint32_t pkt_size;
    if (seq + mss > this->size) {
        pkt_size = this->size - seq + hdr_size;
    } else {
        pkt_size = mss + hdr_size;
    }

    uint32_t priority = get_priority(seq);
    // if (params.flow_type == SNS_FLOW){
    //     SnsPacket *p_sns;
    //     if (params.debug_sns)
    //         std::cout << "flowid: " << this->id << "Creating SNS_SIM_PACKET this->seq_that_send_sns_sim.count(seq)  " <<  this->seq_that_send_sns_sim.count(seq) << " seq " << seq << "\n";
    //     // p_sns = new SnsPacket(SNS_SIM_PACKET, 
    //     //     get_current_time(), 
    //     //     this, seq, priority, pkt_size, 
    //     //     src, dst,
    //     //     -1);
    //     // //this->seq_that_send_sns_sim.insert(seq);
    //     add_to_event_queue(new PacketQueuingEvent(get_current_time(), p_sns, src->queue));
    //     return p_sns;
    // }
    // else {
        p = new Packet(
                get_current_time(), 
                this, 
                seq, 
                priority, 
                pkt_size, 
                src, 
                dst
                );
        this->total_pkt_sent++;

        add_to_event_queue(new PacketQueuingEvent(get_current_time(), p, src->queue));
    //}
    return p;
}

void Flow::send_ack(uint32_t seq, std::vector<uint32_t> sack_list) {
    Packet *p = new Ack(this, seq, sack_list, hdr_size, dst, src); //Acks are dst->src
    add_to_event_queue(new PacketQueuingEvent(get_current_time(), p, dst->queue));
}

void Flow::receive_ack(uint32_t ack, std::vector<uint32_t> sack_list) {
    this->scoreboard_sack_bytes = sack_list.size() * mss;

    // On timeouts; next_seq_no is updated to the last_unacked_seq;
    // In such cases, the ack can be greater than next_seq_no; update it
    if (next_seq_no < ack) {
        next_seq_no = ack;
    }

    // New ack!
    if (ack > last_unacked_seq) {
        // Update the last unacked seq
        last_unacked_seq = ack;

        // Adjust cwnd
        increase_cwnd();

        // Send the remaining data
        send_pending_data();

        // Update the retx timer
        if (retx_event != NULL) { // Try to move
            cancel_retx_event();
            if (last_unacked_seq < size) {
                // Move the timeout to last_unacked_seq
                double timeout = get_current_time() + retx_timeout;
                set_timeout(timeout);
            }
        }

    }

    if (ack == size && !finished) {
        finished = true;
        received.clear();
        finish_time = get_current_time();
        flow_completion_time = finish_time - start_time;
        FlowFinishedEvent *ev = new FlowFinishedEvent(get_current_time(), this);
        add_to_event_queue(ev);
    }
}


void Flow::receive(Packet *p) {
    if (finished) {
        delete p;
        p =nullptr;
        return;
    }

    if (p->type == ACK_PACKET) {
        Ack *a = (Ack *) p;
        receive_ack(a->seq_no, a->sack_list);
    }
    else if(p->type == NORMAL_PACKET) {
        if (this->first_byte_receive_time == -1) {
            this->first_byte_receive_time = get_current_time();
        }
        
        this->receive_data_pkt(p);
        if (params.debug_sns) {
            std::cout << "2 receive_data_pkt "<< p->seq_no << "\n";
        }
    }
    else {
        assert(false);
    }

    delete p;
    p =nullptr;
}

void Flow::receive_data_pkt(Packet* p) {
    if (params.flow_type != CAPABILITY_FLOW)
        received_count++;
    total_queuing_time += p->total_queuing_delay;
    
    if (params.flow_type == PFABRIC_FLOW){
        // assert(0);
        log_drop_reorder(0, 0, 1);
    }
    if (received.count(p->seq_no) == 0) {
        received.insert(p->seq_no);
        if(num_outstanding_packets >= ((p->size - hdr_size) / (mss)))
            num_outstanding_packets -= ((p->size - hdr_size) / (mss));
        else
            num_outstanding_packets = 0;
        received_bytes += (p->size - hdr_size);
        if (params.flow_type == PFABRIC_FLOW){
            this->log_utilization(p->size);
        }
    } else {
        duplicated_packets_received += 1;
    }
    if (p->seq_no > max_seq_no_recv) {
        max_seq_no_recv = p->seq_no;
    }
    else {
        this->num_of_reorders++;
        log_drop_reorder(0, 1, 0);
    }
    // Determing which ack to send
        if (params.debug_sns) {
            std::cout << "1 receive_data_pkt "<< p->seq_no << "\n";
        }
    uint32_t s = recv_till;
    bool in_sequence = true;
    std::vector<uint32_t> sack_list;
    while (s <= max_seq_no_recv) {
        if (received.count(s) > 0) {
            if (in_sequence) {
                if (recv_till + mss > this->size) {
                    recv_till = this->size;
                } else {
                    recv_till += mss;
                }
            } else {
                sack_list.push_back(s);
            }
        } else {
            in_sequence = false;
        }
        s += mss;
    }
    //if (params.flow_type != SNS_FLOW)
        send_ack(recv_till, sack_list); // Cumulative Ack
}

void Flow::set_timeout(double time) {
    if (last_unacked_seq < size) {
        RetxTimeoutEvent *ev = new RetxTimeoutEvent(time, this);
        add_to_event_queue(ev);
        retx_event = ev;
    }
}


void Flow::handle_timeout() {
    next_seq_no = last_unacked_seq;
    //Reset congestion window to 1
    cwnd_mss = 1;
    send_pending_data(); //TODO Send again
    set_timeout(get_current_time() + retx_timeout);  // TODO
}


void Flow::cancel_retx_event() {
    if (retx_event) {
        retx_event->cancelled = true;
    }
    retx_event = NULL;
}


uint32_t Flow::get_priority(uint32_t seq) {
    if (params.flow_type == 1) {
        return 1;
    }
    if(params.deadline && params.schedule_by_deadline)
    {
        return (int)(this->deadline * 1000000);
    }
    else{
        return (size - last_unacked_seq - scoreboard_sack_bytes);
    }
}


void Flow::increase_cwnd() {
    cwnd_mss += 1;
    if (cwnd_mss > max_cwnd) {
        cwnd_mss = max_cwnd;
    }
}

double Flow::get_avg_queuing_delay_in_us()
{
    return total_queuing_time/received_count * 1000000;
}

void Flow::log_utilization(int pkt_size) {
    static double total_recvd = 0;
    static double total_recvd_last = 0;
    static double last_time = 1.0;
    static std::ofstream util_file;
    static double last_utilization = 0;

    total_recvd += pkt_size;
    if(params.util_file != "" &&  get_current_time() - last_time > 0.00002) {
    //if(params.util_file != "" &&  get_current_time() - last_time > 0.0000002) {
        if(!util_file.is_open()) {
            util_file.open(params.util_file);
        }
        
        // Normalize utilization by counting destinations with flows that have >= 10 packets remaining
        std::set<uint32_t> active_destinations;
        double current_time = get_current_time();
        
        // Iterate through all flows in the system
        for (auto flow : flows_to_schedule) {
            // Only consider flows that have started and are not finished
            if (flow->start_time <= current_time && !flow->finished) {
                // Calculate remaining packets based on bytes not yet received at destination
                uint32_t remaining_bytes = (flow->size > flow->received_bytes) ? (flow->size - flow->received_bytes) : 0;
                uint32_t remaining_packets = remaining_bytes / flow->mss;
                
                // Count destinations that have flows with at least 10 packets left and have started receiving
                if (remaining_packets >= 10 && flow->received_bytes > 0) {
                    active_destinations.insert(flow->dst->id);
                }
            }
        }
        
        // Calculate normalized utilization
        double utilization = (total_recvd - total_recvd_last) / (get_current_time() - last_time) / 1000000000 * 8;
        double utilization_2 = (total_recvd - total_recvd_last) / (get_current_time() - last_time) / 1000000000 * 8;
        uint32_t normalization_factor = active_destinations.size();
        // normalization_factor = 1;
        
        // Only normalize if we have active destinations, otherwise keep raw utilization
        if (normalization_factor > 0) {
            utilization = utilization / normalization_factor;
        }
        
        // Only log if we have meaningful activity (either active destinations or significant utilization)
        if (normalization_factor > 0 || utilization > 0.01) {
            util_file << std::setprecision(9) << get_current_time() <<  " " << 
                utilization << " % (normalized by " << normalization_factor << " active destinations) " << utilization_2 << " GB" << std::endl;
            last_utilization = utilization;
        }
        total_recvd_last = total_recvd;
        last_time = get_current_time();
    }

}


void Flow::log_drop_reorder(int is_drop, int is_reorder, int total_pkts1) {
    // return;
    static double total_drops = 0;
    static double total_drops_last = 0;
    static double total_reorders = 0;
    static double total_reorders_last = 0;
    static double total_pkts = 0;
    static double total_pkts_last = 0;
    static double last_time = 1.0;
    static std::ofstream drop_reorder_file;
    static double last_drop = 0;
    static double last_reorder = 0;

    total_drops += is_drop;
    total_reorders += is_reorder;
    total_pkts += total_pkts1;

    if(params.drop_reorder_file != "" &&  get_current_time() - last_time > 0.00002) {
    //if(params.util_file != "" &&  get_current_time() - last_time > 0.0000002) {
        if(!drop_reorder_file.is_open()) {
            drop_reorder_file.open(params.drop_reorder_file);
        }       
       
        double current_time = get_current_time();        
        // Only log if we have meaningful activity (either active destinations or significant utilization)
            drop_reorder_file << std::setprecision(9) << get_current_time() <<  " " << 
                total_drops << " " << total_reorders << " " << total_pkts << std::endl;
            last_drop = total_drops;
            last_reorder = total_reorders;
            total_reorders_last = total_reorders;
            total_drops_last = total_drops;
            last_time = get_current_time();
        }
   
    

}
void Flow::log_queuing() {
    static double total_recvd = 0;
    static double total_recvd_last = 0;
    static double last_time = 1.0;
    static std::ofstream queuing_file;
    if (params.debug_sns)
        std::cout << "Writing Queuing file total_recvd " << total_recvd << "\n";

    
    if(params.queuing_file != "" &&  get_current_time() - last_time > 0.00002) {
        if(!queuing_file.is_open()) {
            queuing_file.open(params.sw_queuing_file);
        }
        // queuing_file << get_current_time() <<  " " << 
        //     (total_recvd - total_recvd_last)  / (get_current_time() - last_time) / 1000000000 * 8 << " Gbps" << std::endl;
        // // total_recvd_last = total_recvd;
        last_time = get_current_time();
    

    // Print current_sim and current_data for specific queue unique IDs
    static std::set<uint32_t> target_queue_ids = {256, 354, 260, 264, 505, 516, 739, 775};
    
    // Log queue information every 0.00002 seconds
        SnsFatTreeTopology* sns_topology = dynamic_cast<SnsFatTreeTopology*>(topology);
        if (sns_topology != nullptr) {
            for (int i = 0; i < sns_topology->sns_switches.size(); i++) {
                for (int j = 0; j < sns_topology->sns_switches[i]->queues.size(); j++) {
                    Queue* queue = sns_topology->sns_switches[i]->queues[j];
                    if (target_queue_ids.find(queue->unique_id) != target_queue_ids.end()) {
                        // std::cout << "Time: " << get_current_time() 
                        //           << " Queue ID: " << queue->unique_id
                        //           << " current_sim: " << queue->current_sim
                        //           << " current_data: " << queue->current_data << std::endl;
                        queuing_file << std::setprecision(9) << get_current_time() <<  " " 
                                    << queue->unique_id << " "
                                    << queue->current_sim << " "
                                    << queue->current_data << std::endl;
                        queuing_file.flush();
                    }
                }
            }
        }
    }
}



void Flow::log_utilization_include_sim(int pkt_size) {
    static double total_recvd = 0;
    static double total_recvd_last = 0;
    static double last_time = 1.0;
    static std::ofstream util_all_file;

    total_recvd += pkt_size;
    if(params.util_all_file != "" &&  get_current_time() - last_time > 0.00002) {
    //if(params.util_file != "" &&  get_current_time() - last_time > 0.0000002) {
        if(!util_all_file.is_open()) {
            util_all_file.open(params.util_all_file);
        }
        util_all_file << std::setprecision(9) << get_current_time() <<  " " << 
            (total_recvd - total_recvd_last)  / (get_current_time() - last_time) / 1000000000 * 8 << " Gbps" << std::endl;
        total_recvd_last = total_recvd;
        last_time = get_current_time();
    }

}