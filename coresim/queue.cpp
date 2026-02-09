#include <climits>
#include <iostream>
#include <iomanip>
#include <stdlib.h>
#include <string>
#include "assert.h"

#include "queue.h"
#include "packet.h"
#include "event.h"
#include "debug.h"

#include "../run/params.h"
#include "../ext/factory.h"

extern double get_current_time();
extern void add_to_event_queue(Event* ev);
extern uint32_t dead_packets;
extern DCExpParams params;

// List of monitored queue IDs for debugging/logging purposes
static const uint32_t MONITORED_QUEUE_IDS[] = {264, 268, 520, 768, 764, 376, 380, 632, 851, 631};
static const size_t MONITORED_QUEUE_COUNT = sizeof(MONITORED_QUEUE_IDS) / sizeof(MONITORED_QUEUE_IDS[0]);

// Helper function to check if a queue ID is being monitored
static bool is_monitored_queue(uint32_t queue_id) {
    for (size_t i = 0; i < MONITORED_QUEUE_COUNT; i++) {
        if (MONITORED_QUEUE_IDS[i] == queue_id) {
            return true;
        }
    }
    return false;
}

uint32_t Queue::instance_count = 0;

/* Queues */
Queue::Queue(uint32_t id, double rate, int limit_bytes, int location) {
    this->id = id;
    this->unique_id = Queue::instance_count++;
    this->rate = rate; // in bps
    this->limit_bytes = limit_bytes;
    this->bytes_in_queue = 0;
    this->busy = false;
    this->queue_proc_event = NULL;
    //this->packet_propagation_event = NULL;
    this->location = location;

    this->max_bytes_in_queue = 0;
    this->total_bytes_in_queue = 0;
    this->record_time = 0;
    if (params.ddc != 0) {
        if (location == 0) {
            this->propagation_delay = 10e-9;
        }
        else if (location == 1 || location == 2) {
            this->propagation_delay = 400e-9;
        }
        else if (location == 3) {
            this->propagation_delay = 210e-9;
        }
        else {
            assert(false);
        }
    }
    else {
       
        this->propagation_delay = params.propagation_delay;
    }
    this->p_arrivals = 0; this->p_departures = 0; 
    this->b_arrivals = 0; this->b_departures = 0;

    this->pkt_drop = 0;
    this->spray_counter=std::rand();
    this->packet_transmitting = NULL;
    this->current_data = 0;
    this->current_sim = 0;
    this->free_tokens = 0;
    this->token_bucket_run = 0;
    
}

void Queue::set_src_dst(Node *src, Node *dst) {
    this->src = src;
    this->dst = dst;
}

bool Queue::check_if_there_is_already_x_simulation(u_int32_t threshold){
    u_int32_t count = 0;
    for (Packet* packet : packets) {
        if (packet->sns_type == SNS_SIM_PACKET) {
            count++;
        }
        if (count > threshold){
            return true;
        }
    }
    return false;

}
void Queue::enque(Packet *packet) {
    
    params.simulation_queue_size = params.queue_size/(params.mss+params.hdr_size) / params.param_b;
    params.simulation_queue_size = params.param_b;
    
    p_arrivals += 1;
    b_arrivals += packet->size;
    if (params.debug_sns){
            std::cout << "Queue::enque queue_id: " << this->unique_id << " bytes_in_queue:" << bytes_in_queue << " packet->size " << packet->size << " limit_bytes " << limit_bytes << "\n";

            std::cout << "Queue::enque bytes_in_queue:" << bytes_in_queue << " packet->size " << packet->size << " limit_bytes " << limit_bytes << "\n";
        }
    bool need_to_drop = false;
    if ((packet->sns_type == SNS_SIM_PACKET) && check_if_there_is_already_x_simulation(params.simulation_queue_size))
        need_to_drop = true;
    need_to_drop = false;
    if (params.flow_type == PFABRIC_FLOW){
        packet->flow->log_drop_reorder(0, 0, 1);
    }
    if (!need_to_drop && 
       (bytes_in_queue + packet->size <= limit_bytes && packet->size != -1)) {
        packet->last_enque_time = get_current_time();
        if (packet->sns_type == SNS_DATA_PACKET){
            
            this->current_data++;
        }
       
        if (is_monitored_queue(this->unique_id)){
        
            record_queueing_entry(packet, 0);
            if (packet->sns_type == SNS_SIM_PACKET){
                packet->sim_enter_q2 = get_current_time();
                
            }
            else if (packet->sns_type == SNS_DATA_PACKET){
                packet->data_enter_q2 = get_current_time();
                
            }
        }
        
        packets.push_back(packet);
        bytes_in_queue += packet->size;
    } else {
        
        if (params.debug_sns){
            std::cout << "Queue::enque DROP: packet sns_type:" << packet->sns_type << " q_id: " << this->unique_id << "\n";
        }
        if (packet->sns_type == SNS_SIM_PACKET)
            params.total_drop_sim_pkts += 1;
        else if (packet->sns_type == SNS_DATA_PACKET){
            params.total_drop_data_pkts += 1;
            packet->flow->log_drop_reorder(1, 0, 0);
        }
        pkt_drop++;
        drop(packet);
        assert(packet);

    }
}

Packet *Queue::deque() {
    if (bytes_in_queue > 0) {
        Packet *p = packets.front();
        p->total_queuing_delay += get_current_time() - p->last_enque_time;
        
        if (is_monitored_queue(this->unique_id)){
            record_queueing_entry(p, 1);
        }
        packets.pop_front();
        bytes_in_queue -= p->size;
        p_departures += 1;
        b_departures += p->size;
        return p;
    }
    return NULL;
}

void Queue::drop(Packet *packet) {

    packet->flow->pkt_drop++;
        if (params.debug_sns){
            std::cout << "Queue::drop DROP: flowid: " << packet->flow->id << " flow->pkt_drop: " << packet->flow->pkt_drop << "\n";
        }
    if(packet->seq_no <= packet->flow->size){
        if (packet->sns_type && packet->sns_type == SNS_DATA_PACKET)
            packet->flow->data_pkt_drop++;
        if (packet->sns_type == 0 && packet->type == NORMAL_PACKET) {
            packet->flow->data_pkt_drop++;
            packet->flow->log_drop_reorder(1, 0, 1);
        }
    }
    if(packet->type == ACK_PACKET)
        packet->flow->ack_pkt_drop++;
    if(packet->sns_type == SNS_SIM_PACKET)
        packet->flow->ack_pkt_drop++;

    if (location != 0 && packet->type == NORMAL_PACKET) {
        dead_packets += 1;
    }
    if(packet->type == RUF_LISTSRCS) {
        std::cout << get_current_time() << " listSRC pkt drop. flow:" << packet->flow->id << " packet size:" << packet->size
            << " type:" << packet->type << " seq:" << packet->seq_no << " src:" << packet->src->id << " num of packets in queue: " << packets.size()
            << " at queue id:" << this->id << " loc:" << this->location << "\n";
    }
    else if(packet->type == RUF_GOSRC) {
        std::cout << get_current_time() << " gosrc pkt drop. flow:" << packet->flow->id << " packet size:" << packet->size
            << " type:" << packet->type << " seq:" << packet->seq_no << " src:" << packet->src->id << " num of packets in queue: " << packets.size()
            << " at queue id:" << this->id << " loc:" << this->location << "\n";
    }
    else if (packet->type == FASTPASS_SCHEDULE) {
        // std::cout << get_current_time() << " fastpass pkt drop. flow:" << packet->flow->id << " packet size:" << packet->size
        //     << " type:" << packet->type << " seq:" << packet->seq_no << " src:" << packet->src->id << " num of packets in queue: " << packets.size()
        //     << " at queue id:" << this->id << " loc:" << this->location << std::endl;
        // assert(false);
        delete ((FastpassSchedulePkt*)packet)->schedule;
    }
    if(debug_flow(packet->flow->id))
        std::cout << get_current_time() << " pkt drop. flow:" << packet->flow->id
            << " type:" << packet->type << " seq:" << packet->seq_no
            << " at queue id:" << this->id << " loc:" << this->location << "\n";

    delete packet;
    packet = nullptr;


}

double Queue::get_transmission_delay(uint32_t size) {
    //std::cout << get_current_time() << "q_id:" << this->unique_id << " size: " << size << " rate: " << rate << " total: " << size * 8.0 / rate << "\n";
    return size * 8.0 / rate;
}

uint32_t Queue::count_sns_sim_packets() {
    uint32_t count = 0;
    for (const Packet* packet : packets) {
        if (packet->sns_type == SNS_SIM_PACKET) {
            count++;
        }
    }
    return count;
}


uint32_t Queue::count_sns_data_packets() {
    uint32_t count = 0;
    for (const Packet* packet : packets) {
        if (packet->sns_type == SNS_DATA_PACKET) {
            count++;
        }
    }
    return count;
}

std::string Queue::get_all_packet_types_string() {
    std::string result = "";
    for (const Packet* packet : packets) {
        if (!result.empty()) {
            result += " ";
        }
        result += std::to_string(packet->sns_type);
    }
    return result.empty() ? "empty" : result;
}

void Queue::print_sns_sim_packet_count() {
    uint32_t count = count_sns_sim_packets();
    std::cout << "Queue " << this->unique_id << " contains " << count << " SNS_SIM_PACKET(s)" << std::endl;
}

void Queue::record_queueing_entry(Packet* packet, int is_deque) {
    std::vector<double> queueing_entry;
    queueing_entry.reserve(6);  // Pre-allocate space for efficiency
    queueing_entry.push_back((get_current_time()-1.0)*1e6); // time in microseconds
    queueing_entry.push_back(this->unique_id);
    queueing_entry.push_back(static_cast<double>(packet->flow->id));
    queueing_entry.push_back(static_cast<double>(packet->sns_msg_id));
    queueing_entry.push_back(2.0); // 2 indicates SIM packet
    queueing_entry.push_back(count_sns_sim_packets()); // queuing delay in microseconds
    // queueing_entry.push_back(p->total_queuing_delay * 1000000.0); // queuing delay in microseconds
    // if (this->queueing_times.size() <  this->size_in_pkt*2 - 2) {
    params.specific_queueing_packets.push_back(queueing_entry);
    std::vector<double> queueing_entry1;

    queueing_entry1.reserve(6);  // Pre-allocate space for efficiency
    queueing_entry1.push_back((get_current_time()-1.0)*1e6); // time in microseconds
    queueing_entry1.push_back(this->unique_id);
    queueing_entry1.push_back(static_cast<double>(packet->flow->id));
    queueing_entry1.push_back(static_cast<double>(packet->sns_msg_id));
    if (is_deque == 0 && packet->sns_type == SNS_DATA_PACKET)
        queueing_entry1.push_back(40.0); // 40 indicates DATA packet
    else
        queueing_entry1.push_back(4.0); // 4 indicates DATA packet
    queueing_entry1.push_back(count_sns_data_packets()); // queuing delay in microseconds
    params.specific_queueing_packets.push_back(queueing_entry1);
    if (is_deque){
        std::vector<double> queueing_entry2;

        queueing_entry2.reserve(6);  // Pre-allocate space for efficiency
        queueing_entry2.push_back((get_current_time()-1.0)*1e6); // time in microseconds
        queueing_entry2.push_back(this->unique_id);
        queueing_entry2.push_back(static_cast<double>(packet->flow->id));
        queueing_entry2.push_back(static_cast<double>(packet->sns_msg_id));
        queueing_entry2.push_back(7.0); // 7 indicates dequeued packet type
        queueing_entry2.push_back(packet->sns_type); // the dequeued packet's sns_type
        
        params.specific_queueing_packets.push_back(queueing_entry2);
 }
    
}
double SnsQueue::get_transmission_delay(Packet *packet) {
    
    double local_rate = rate;
    return packet->size * 8.0 / local_rate;
}

void Queue::preempt_current_transmission() {
    if(params.preemptive_queue && busy){
        this->queue_proc_event->cancelled = true;
        assert(this->packet_transmitting);

        uint delete_index;
        bool found = false;
        for (delete_index = 0; delete_index < packets.size(); delete_index++) {
            if (packets[delete_index] == this->packet_transmitting) {
                found = true;
                break;
            }
        }
        if(found){
            bytes_in_queue -= packet_transmitting->size;
            packets.erase(packets.begin() + delete_index);
        }

        for(uint i = 0; i < busy_events.size(); i++){
            busy_events[i]->cancelled = true;
        }
        busy_events.clear();
        enque(packet_transmitting);
        packet_transmitting = NULL;
        queue_proc_event = NULL;
        busy = false;
    }
}
 
/* Implementation for probabilistically dropping queue */
ProbDropQueue::ProbDropQueue(uint32_t id, double rate, uint32_t limit_bytes,
        double drop_prob, int location)
    : Queue(id, rate, limit_bytes, location) {
        this->drop_prob = drop_prob;
    }

void ProbDropQueue::enque(Packet *packet) {
    p_arrivals += 1;
    b_arrivals += packet->size;

    if (bytes_in_queue + packet->size <= limit_bytes) {
        double r = (1.0 * rand()) / (1.0 * RAND_MAX);
        if (r < drop_prob) {
            return;
        }
        packets.push_back(packet);
        bytes_in_queue += packet->size;
        if (!busy) {
            add_to_event_queue(new QueueProcessingEvent(get_current_time(), this));
            this->busy = true;
            //if(this->id == 7) std::cout << "!!!!!queue.cpp:189\n";
            this->packet_transmitting = packet;
        }
    }
}

SnsQueue::SnsQueue(uint32_t id, double rate, uint32_t limit_bytes, int location)
    : Queue(id, rate, limit_bytes, location) {
        this->sim_turn = 0;
    }

void SnsQueue::enque(Packet *packet) {
    if (params.flow_type == SNS_FLOW && 
        this->token_bucket_run == 0){
            Event* queuing_evt = NULL;
            // std::cout << "Queue::enque queue_id: " << this->unique_id << " bytes_in_queue:" << bytes_in_queue << " packet->size " << packet->size << " limit_bytes " << limit_bytes << "\n";

            queuing_evt = new FillTokenBucketEvent(get_current_time() + INFINITESIMAL_TIME, this);
            add_to_event_queue(queuing_evt);
            this->token_bucket_run = 1;
        }
    int sim_queue_size = params.param_b;
    
    if (packet->sns_type != SNS_SIM_PACKET){
        Queue::enque(packet);
        return;
    }
    p_arrivals += 1;
    b_arrivals += packet->size;
    if (params.debug_sns){
            std::cout << "Queue::enque queue_id: " << this->unique_id << " bytes_in_queue:" << bytes_in_queue << " packet->size " << packet->size << " limit_bytes " << limit_bytes << "\n";

            std::cout << "Queue::enque bytes_in_queue:" << bytes_in_queue << " packet->size " << packet->size << " limit_bytes " << limit_bytes << "\n";
        }
    assert(packet);
    if (sim_packets.size() < sim_queue_size && packet->size != -1 /*|| (this->src == packet->src && packet->sns_type == SNS_SIM_PACKET) /*|| this->dst == packet->dst*/) {
        if (packet->sns_type == SNS_SIM_PACKET){
            this->current_sim++;
        }        
        sim_bytes_in_queue += packet->size;
        double td = (params.hdr_size * 8.0) / (rate/params.sns_simulation_frq_size);
        sim_packets.push_back(packet);
        td *= (sim_packets.size());
        if (is_monitored_queue(this->unique_id)){ 
        
            std::vector<double> queueing_entry;
            queueing_entry.reserve(6);  // Pre-allocate space for efficiency
            queueing_entry.push_back((get_current_time()-1)*1e6); // time in microseconds
            queueing_entry.push_back(this->unique_id);

            queueing_entry.push_back(static_cast<double>(packet->flow->id));
            queueing_entry.push_back(static_cast<double>(packet->sns_msg_id));
            queueing_entry.push_back(0.0); // 0 indicates SIM packet
            packet->sim_enter_q1 = get_current_time();
            queueing_entry.push_back(sim_packets.size()); // queuing delay in microseconds
            params.specific_queueing_packets.push_back(queueing_entry);
            queueing_entry.clear();  // Clear the vector for reuse
            queueing_entry.reserve(6);  // Pre-allocate space for efficiency
            queueing_entry.push_back((get_current_time()-1)*1e6); // time in microseconds
            queueing_entry.push_back(this->unique_id);            
            queueing_entry.push_back(static_cast<double>(packet->flow->id));
            queueing_entry.push_back(static_cast<double>(packet->sns_msg_id));
            queueing_entry.push_back(1.0); // 1 indicates SIM packet
            queueing_entry.push_back(td * 1e6); // queuing delay in microseconds
            params.specific_queueing_packets.push_back(queueing_entry);
        }
        td = 0; // we want token bucket implementation
        add_to_event_queue(new SnsQueueProcessingEvent(get_current_time() + td + INFINITESIMAL_TIME, this));
    } else {
       
        if (params.debug_sns){
            std::cout << "Queue::enque DROP: packet sns_type:" << packet->sns_type << " q_id: " << this->unique_id << "\n";
        }
        if (packet->sns_type == SNS_SIM_PACKET)
            params.total_drop_sim_pkts += 1;
        else if (packet->sns_type == SNS_DATA_PACKET){
            params.total_drop_data_pkts += 1;
            packet->flow->log_drop_reorder(1, 0, 0);
        }
        pkt_drop++;
        drop(packet);
    }
    //queue_detail_file.close();
if(packet && params.debug_queue && (this->unique_id == 256 ||
        this->unique_id == 260 || this->unique_id == 354 || this->unique_id == 264 || this->unique_id == 505 || this->unique_id == 516 ||
        this->unique_id == 739 ||this->unique_id == 775))
        {
            queue_detail_file.flush();  
    }
}

Packet *SnsQueue::deque() {
    static int counter = 0;
    counter++;
    if(params.debug_queue) {
    if(!queue_detail_file.is_open()) {
        queue_detail_file.open("../queue_detail_file.txt", std::ios::app);
    }
    }
    if (params.debug_sns){
            std::cout << "SnsQueue::deque: sim_turn: " << sim_turn << " q_id: " << this->unique_id << " packets.size(): " << packets.size() << "\n";
        }
    
    this->sim_turn = 1;
    if (bytes_in_queue > 0) {
        int pkt_index = -1;
        if (this->sim_turn){
            // Priority 1: SNS_SIM_PACKET
            if (pkt_index == -1) {
                for (uint32_t i = 0; i < packets.size(); i++) {
                    if (packets[i]->sns_type == SNS_SIM_PACKET) {
                        pkt_index = i;
                        break;
                    }
                }
            }
            for (uint32_t i = 0; i < packets.size(); i++) {
                // Priority 2: SNS_SIM_ACK_PACKET (highest priority)
                if (packets[i]->sns_type == SNS_SIM_ACK_PACKET) {
                    pkt_index = i;
                    break;
                }
            }
            // Priority 3: SNS_DATA_ACK_PACKET
            if (pkt_index == -1) {
                for (uint32_t i = 0; i < packets.size(); i++) {
                    if (packets[i]->sns_type == SNS_DATA_ACK_PACKET) {
                        pkt_index = i;
                        break;
                    }
                }
            }
            // Priority 4: SNS_DATA_PACKET (lowest priority)
            if (pkt_index == -1) {
                for (uint32_t i = 0; i < packets.size(); i++) {
                    if (packets[i]->sns_type == SNS_DATA_PACKET) {
                        pkt_index = i;
                        break;
                    }
                }
            }
            
        } else {
            assert(0);
            for (uint32_t i = 0; i < packets.size(); i++) {
                if (params.debug_sns){
            std::cout << "SnsQueue::deque: sim_turn: " << sim_turn << " q_id: " << this->unique_id << " packets[i]->sns_type: " << packets[i]->sns_type << " packets[i]->sns_msg_id: " << packets[i]->sns_msg_id << "\n";
        }
            // Priority 1: SNS_SIM_PACKET
            if (pkt_index == -1) {
                for (uint32_t i = 0; i < packets.size(); i++) {
                    if (packets[i]->sns_type == SNS_SIM_PACKET) {
                        pkt_index = i;
                        break;
                    }
                }
            }
                // Priority 2: SNS_SIM_ACK_PACKET (highest priority)
                if (packets[i]->sns_type == SNS_SIM_ACK_PACKET) {
                    pkt_index = i;
                    break;
                }
            }
            
            // Priority 3: SNS_DATA_ACK_PACKET
            if (pkt_index == -1) {
                for (uint32_t i = 0; i < packets.size(); i++) {
                    if (packets[i]->sns_type == SNS_DATA_ACK_PACKET) {
                        pkt_index = i;
                        break;
                    }
                }
            }
            // Priority 4: SNS_DATA_PACKET (lowest priority)
            if (pkt_index == -1) {
                for (uint32_t i = 0; i < packets.size(); i++) {
                    if (packets[i]->sns_type == SNS_DATA_PACKET) {
                        pkt_index = i;
                        break;
                    }
                }
            }
            
        }
        if (params.debug_sns){
            std::cout << "SnsQueue::deque: pkt_index: " << pkt_index << " q_id: " << this->unique_id << " packets.size(): " << packets.size() << "\n";
        }
        
        if (pkt_index == -1){ // like regular
        
            Packet *p = packets.front();
            if (is_monitored_queue(this->unique_id)){
               record_queueing_entry(p, 1);
               assert(0);
            }
            // if(params.debug_queue && p->flow->id == 0) {
                if(params.debug_queue && (this->unique_id == 256 ||
        this->unique_id == 260 || this->unique_id == 354 || this->unique_id == 264 || this->unique_id == 505 || this->unique_id == 516 ||
            this->unique_id == 739 ||this->unique_id == 775))
                {   
                queue_detail_file << (get_current_time()-1)*1e6 << " " << this->unique_id << " " << p->flow->id << " "
                            << " " << p->sns_type << " " << p->sns_msg_id << " f1\n";
                            //queue_detail_file.close();
                            queue_detail_file.flush();
                }
        
            if (params.debug_sns){
            std::cout << " 1regular deque time: " << (get_current_time()-1)*1e6 << " sns_type: " << p->sns_type << " unique_id: " << this->unique_id << "packet_size: " << p->size << "\n";
         }
            packets.pop_front();
            
            bytes_in_queue -= p->size;
            p_departures += 1;
            b_departures += p->size;

            if(p->sns_type == SNS_DATA_PACKET){
                this->current_data--;
                p->total_queuing_delay += get_current_time() - p->last_enque_time;
            }
        if (is_monitored_queue(this->unique_id)){
            if(p->sns_type == SNS_SIM_PACKET){
                std::vector<double> queueing_entry;
                queueing_entry.reserve(6);  // Pre-allocate space for efficiency
                queueing_entry.push_back((get_current_time()-1)*1e6); // time in microseconds
                queueing_entry.push_back(this->unique_id);

                queueing_entry.push_back(static_cast<double>(p->flow->id));
                queueing_entry.push_back(static_cast<double>(p->sns_msg_id));
                queueing_entry.push_back(3.0); // 3 indicates SIM packet
                queueing_entry.push_back((get_current_time() - p->sim_enter_q2) * 1e6); // queuing delay in microseconds
                params.specific_queueing_packets.push_back(queueing_entry);
            }
            else if (p->sns_type == SNS_DATA_PACKET){
                std::vector<double> queueing_entry;
                queueing_entry.reserve(6);  // Pre-allocate space for efficiency
                queueing_entry.push_back((get_current_time()-1)*1e6); // time in microseconds
                queueing_entry.push_back(this->unique_id);

                queueing_entry.push_back(static_cast<double>(p->flow->id));
                queueing_entry.push_back(static_cast<double>(p->sns_msg_id));
                queueing_entry.push_back(5.0); // 5 indicates DATA packet
                queueing_entry.push_back((get_current_time() - p->data_enter_q2) * 1e6); // queuing delay in microseconds
                params.specific_queueing_packets.push_back(queueing_entry);
            }
        }
            return p;
        } else {
            Packet *p = packets[pkt_index];
            if (is_monitored_queue(this->unique_id)){
                record_queueing_entry(p, 1);
            }
            
            if (params.debug_sns){
                std::cout << " 2regular deque time: " << (get_current_time()-1)*1e6 << " sns_type: " << p->sns_type << " unique_id: " << this->unique_id << "packet_size: " << p->size << "\n";
            }
            if (p->sns_type == SNS_DATA_PACKET)
                p->total_queuing_delay += get_current_time() - p->last_enque_time;
            packets.erase(packets.begin() + pkt_index);
            bytes_in_queue -= p->size;
            p_departures += 1;
            b_departures += p->size;

        if(p->sns_type == SNS_DATA_PACKET){
            this->current_data--;
        }
        if (is_monitored_queue(this->unique_id)){
            if(p->sns_type == SNS_SIM_PACKET){
                std::vector<double> queueing_entry;
                queueing_entry.reserve(6);  // Pre-allocate space for efficiency
                queueing_entry.push_back((get_current_time()-1)*1e6); // time in microseconds
                queueing_entry.push_back(this->unique_id);

                queueing_entry.push_back(static_cast<double>(p->flow->id));
                queueing_entry.push_back(static_cast<double>(p->sns_msg_id));
                queueing_entry.push_back(3.0); // 3 indicates SIM packet
                queueing_entry.push_back((get_current_time() - p->sim_enter_q2) * 1e6); // queuing delay in microseconds
                params.specific_queueing_packets.push_back(queueing_entry);
            }
            else if (p->sns_type == SNS_DATA_PACKET){
                std::vector<double> queueing_entry;
                queueing_entry.reserve(6);  // Pre-allocate space for efficiency
                

                queueing_entry.push_back((get_current_time()-1)*1e6); // time in microseconds
                queueing_entry.push_back(this->unique_id);
                queueing_entry.push_back(static_cast<double>(p->flow->id));
                queueing_entry.push_back(static_cast<double>(p->sns_msg_id));
                queueing_entry.push_back(5.0); // 5 indicates DATA packet
                queueing_entry.push_back((get_current_time() - p->data_enter_q2) * 1e6); // queuing delay in microseconds
                params.specific_queueing_packets.push_back(queueing_entry);
            }
        }
            return p;
        }
    }
    return NULL;
}