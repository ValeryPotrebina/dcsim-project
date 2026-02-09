#include "assert.h"
 
//originated by host.cpp
#include "packet.h"
#include "flow.h"

#include "../ext/factory.h"

#include "../run/params.h"

#include "../coresim/snshost.h"

#include <algorithm> // For std::min
#include <iomanip>
#include <set> // For std::set
#include "snshost.h"
//#include "snshost.h"

extern DCExpParams params;
extern double get_current_time();
extern void add_to_event_queue(Event*);

SnsSwitch::SnsSwitch(uint32_t id, uint32_t switch_type) : Switch(id, switch_type) {

}
SnsHost::SnsHost(uint32_t id, double rate, uint32_t queue_type) 
                    : SchedulingHost(id, rate, queue_type){
    double sns_simu_fraq = params.sns_simulation_frq_size;
    double regular_rate = rate - 1*rate/sns_simu_fraq;
    double simulation_rate = rate/sns_simu_fraq;
    double simulation_ack_rate = rate/sns_simu_fraq;
    queue = (SnsQueue*)Factory::get_queue(id, rate, params.queue_size, SNS_QUEUE, 0, 0);
     
    // Pre-calculate clock drift factor once at host creation
    if (params.clock_drift > 0.0) {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_real_distribution<> dis(-1*params.clock_drift, params.clock_drift);
        clock_drift_factor = 1.0*dis(gen);
    } else {
        clock_drift_factor = 1.0;
    }
    clock_drift_factor_dynamic = 0.0;
    
    host_proc_event = NULL;
    host_proc_sns_send_packet_event = NULL;
    
    // Initialize control variables
    enable_parallel_receiver_limit = false;
    enable_collective_round_robin = true;
    auto_fallback_single_collective = true;
    current_collective_index = 0;
    collective_flow_index.clear();
}

void SnsHost::update_dynamic_clock_drift_factor() {
    if (params.clock_drift > 0.0) {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_real_distribution<> dis(-1*params.clock_drift, params.clock_drift);
        clock_drift_factor_dynamic += dis(gen);
    } else {
        clock_drift_factor_dynamic = 0.0;
    }
}

void SnsHost::start_flow(SnsFlow* f){
    this->active_sending_flows.push_back(f);
    if (((SnsHost*) this)->host_proc_event == NULL) {
        this->schedule_host_proc_evt();
    }

}
void SnsHost::schedule_host_proc_evt(){
    //assert(this->host_proc_event == NULL);

    double qpe_time = 0;
    double td_time = 0;
    if(this->queue->busy){
        qpe_time = this->queue->queue_proc_event->time;
    }
    else{
        qpe_time = get_current_time();
    }
    qpe_time = get_current_time();

    uint32_t queue_size = this->queue->bytes_in_queue;
    if (params.debug_sns) {
            std::cout << "SnsHost::schedule_host_proc_evt time this->queue->packets.size()" << this->queue->packets.size() << " qpe_time " << qpe_time << " " << (get_current_time()-1)*1e6  << "\n";
        }
    if (this->queue->packets.size() > 1)
        td_time = ((SnsQueue*)(this->queue))->get_transmission_delay(this->queue->packets[0]);

    this->host_proc_event = new HostProcessingEvent(qpe_time + td_time + INFINITESIMAL_TIME, this);
    add_to_event_queue(this->host_proc_event);
}

void SnsHost::move_sim_to_packets(){
    if (params.debug_sns2)
        std::cout << "move_sim_to_packets time: " << (get_current_time()-1)*1e6 << "\n";
      
    if (sims_to_be_sent.size() == 0){
        if (params.debug_sns2)
        std::cout << "1move_sim_to_packets time: " << (get_current_time()-1)*1e6 << "\n";
        
        return;
    }
    SnsPacket* p = this->sims_to_be_sent.front();
    this->sims_to_be_sent.pop_front();
    this->packets_to_be_sent.push_back(p);
    if (params.debug_sns2)
        std::cout << "2move_sim_to_packets time: " << (get_current_time()-1)*1e6 << "\n";
    if (this->host_proc_sns_send_packet_event == NULL){
        this->host_proc_sns_send_packet_event = new SnsSendPacketProcessingEvent(get_current_time(), this);
        add_to_event_queue(this->host_proc_sns_send_packet_event);
    }
    if (params.debug_sns2)
    std::cout << "3move_sim_to_packets time: " << (get_current_time()-1)*1e6 << "\n";
    //}
    //schedule_host_proc_evt();  // for creating new simulation
}

SnsPacket* SnsHost::get_sim_or_sim_ack(){
    static int counter = 0;
    static bool sim_turn = true;
    counter++;
   
    if (sim_turn) {

        for(uint i = 0; i < this->packets_to_be_sent.size(); i++){
            if (this->packets_to_be_sent[i]->sns_type == SNS_SIM_PACKET) {
                SnsPacket* p = this->packets_to_be_sent[i];
                packets_to_be_sent.erase(packets_to_be_sent.begin() + i);
                return p;
            }
        }
        // Priority 2: SNS_SIM_ACK_PACKET (highest priority)
        for(uint i = 0; i < this->packets_to_be_sent.size(); i++){
            if (this->packets_to_be_sent[i]->sns_type == SNS_SIM_ACK_PACKET) {
                SnsPacket* p = this->packets_to_be_sent[i];
                packets_to_be_sent.erase(packets_to_be_sent.begin() + i);
                return p;
            }
        }         
        
        // Priority 3: SNS_DATA_ACK_PACKET
        for(uint i = 0; i < this->packets_to_be_sent.size(); i++){
            if (this->packets_to_be_sent[i]->sns_type == SNS_DATA_ACK_PACKET) {
                SnsPacket* p = this->packets_to_be_sent[i];
                packets_to_be_sent.erase(packets_to_be_sent.begin() + i);
                return p;
            }
        }
        for(uint i = 0; i < this->packets_to_be_sent.size(); i++){
            if (this->packets_to_be_sent[i]->sns_type == SNS_DATA_PACKET) {
                SnsPacket* p = this->packets_to_be_sent[i];
                packets_to_be_sent.erase(packets_to_be_sent.begin() + i);
                return p;
            }
        }
        
        
    } else {
        // sim_turn = false priority order:
        // 1) SNS_SIM_ACK_PACKET, 2) SNS_DATA_ACK_PACKET, 3) SNS_DATA_PACKET, 4) SNS_SIM_PACKET
        
        // Priority 1: SNS_SIM_ACK_PACKET (highest priority)
        for(uint i = 0; i < this->packets_to_be_sent.size(); i++){
            if (this->packets_to_be_sent[i]->sns_type == SNS_SIM_ACK_PACKET) {
                SnsPacket* p = this->packets_to_be_sent[i];
                packets_to_be_sent.erase(packets_to_be_sent.begin() + i);
                return p;
            }
        }
        
        // Priority 2: SNS_DATA_ACK_PACKET
        for(uint i = 0; i < this->packets_to_be_sent.size(); i++){
            if (this->packets_to_be_sent[i]->sns_type == SNS_DATA_ACK_PACKET) {
                SnsPacket* p = this->packets_to_be_sent[i];
                packets_to_be_sent.erase(packets_to_be_sent.begin() + i);
                return p;
            }
        }
        
        // Priority 3: SNS_DATA_PACKET
        for(uint i = 0; i < this->packets_to_be_sent.size(); i++){
            if (this->packets_to_be_sent[i]->sns_type == SNS_DATA_PACKET) {
                SnsPacket* p = this->packets_to_be_sent[i];
                packets_to_be_sent.erase(packets_to_be_sent.begin() + i);
                return p;
            }
        }
        
        // Priority 4: SNS_SIM_PACKET (lowest priority when sim_turn = false)
        for(uint i = 0; i < this->packets_to_be_sent.size(); i++){
            if (this->packets_to_be_sent[i]->sns_type == SNS_SIM_PACKET) {
                SnsPacket* p = this->packets_to_be_sent[i];
                packets_to_be_sent.erase(packets_to_be_sent.begin() + i);
                return p;
            }
        }
    }
    SnsPacket* p = this->packets_to_be_sent.front();
    this->packets_to_be_sent.pop_front();
    return p;

}
void SnsHost::move_packets_to_queue(){
    if (params.debug_sns2)
        std::cout << std::setprecision(8) << "move_packets_to_queue time: " << (get_current_time()-1)*1e6 << "\n";
    double td_time = 0;
    if (packets_to_be_sent.size()>0){
        SnsPacket* p = get_sim_or_sim_ack();
        p->flow->log_utilization_include_sim(p->size);
        p->sending_time = get_current_time();    
        add_to_event_queue(new PacketQueuingEvent(p->sending_time, p , this->queue));
        td_time = ((SnsQueue*)(this->queue))->get_transmission_delay(p);
        if (params.debug_sns2){
            std::cout << std::setprecision(8) << "1move_packets_to_queue q_id: " << this->queue->unique_id << " time: " << (get_current_time()-1)*1e6 << " td: " << td_time << " type: " << p->sns_type << "\n";
            std::cout << std::setprecision(8) << "1move_packets_to_queue packets_to_be_sent.size(): " << packets_to_be_sent.size() << "\n";
        }
        this->host_proc_sns_send_packet_event = new SnsSendPacketProcessingEvent(get_current_time() + td_time, this);
        add_to_event_queue(this->host_proc_sns_send_packet_event);
    }
    if (params.debug_sns2)
        std::cout << std::setprecision(8) << "2move_packets_to_queue time: " << (get_current_time()-1)*1e6 << " td: " << td_time << "\n";
    
    
}

void SnsHost::send(){
    if (params.debug_sns) {
        std::cout << "SnsHost::send time" << (get_current_time()-1)*1e6  << "\n";
    }
    assert(this->host_proc_event == NULL);
    
    if (enable_parallel_receiver_limit) {
        send_with_parallel_receiver_limit();
        return;
    }
    
    if (enable_collective_round_robin) {
        send_with_collective_round_robin();
        return;
    }
        std::queue<SnsFlow*> flows_tried;
        
        while(!this->active_sending_flows.empty()) {
            SnsFlow* flow = this->active_sending_flows.front();
            this->active_sending_flows.pop_front();
            
            if(flow->finished) {
                if (params.debug_sns) {
                    std::cout << "SnsHost::send time flow->finished" << (get_current_time()-1)*1e6  << "\n";
                }
                continue;
            }
            
            flows_tried.push(flow);
            
            if(!flow->finished) {
                if (params.debug_sns) {
                    std::cout << "SnsHost::send sending to receiver " << flow->dst->id << "\n";
                }
                
                flow->send_pending_data();
                break;
            }
        }
        while(!flows_tried.empty()) {
            this->active_sending_flows.push_back(flows_tried.front());
            flows_tried.pop();
        }
    // }
}

void SnsHost::send_with_parallel_receiver_limit(){
    if (params.debug_sns) {
        std::cout << "SnsHost::send_with_parallel_receiver_limit time" << (get_current_time()-1)*1e6  << "\n";
    }
    
    if(0 && this->queue->busy)
    {
        if (params.debug_sns) {
            std::cout << "SnsHost::send_with_parallel_receiver_limit time queue->busy" << (get_current_time()-1)*1e6  << "\n";
        }
        schedule_host_proc_evt();
    }
    else
    {
        std::queue<SnsFlow*> flows_tried;
        
        // Clean up finished flows from active_receivers set
        std::set<uint32_t> receivers_to_remove;
        for (uint32_t receiver_id : this->active_receivers) {
            bool has_active_flow = false;
            for (auto it = this->active_sending_flows.begin(); it != this->active_sending_flows.end(); ++it) {
                SnsFlow* flow = *it;
                if (!flow->finished && flow->dst->id == receiver_id) {
                    has_active_flow = true;
                    break;
                }
            }
            if (!has_active_flow) {
                receivers_to_remove.insert(receiver_id);
            }
        }
        
        // Remove finished receivers from active set
        for (uint32_t receiver_id : receivers_to_remove) {
            this->active_receivers.erase(receiver_id);
            if (params.debug_sns) {
                std::cout << "SnsHost::send_with_parallel_receiver_limit removed finished receiver " << receiver_id 
                          << " (active receivers: " << this->active_receivers.size() << ")" << "\n";
            }
        }
        
        while(!this->active_sending_flows.empty()) {
            SnsFlow* flow = this->active_sending_flows.front();
            this->active_sending_flows.pop_front();
            
            if(flow->finished) {
                if (params.debug_sns) {
                    std::cout << "SnsHost::send_with_parallel_receiver_limit time flow->finished" << (get_current_time()-1)*1e6  << "\n";
                }
                continue;
            }
            
            // Check if we can send to this receiver
            bool can_send_to_receiver = (this->active_receivers.count(flow->dst->id) > 0) || 
                                       (this->active_receivers.size() < max_parallel_receivers);
            
            flows_tried.push(flow);
            
            if(!flow->finished && can_send_to_receiver) {
                // Add this receiver to active set if not already there
                this->active_receivers.insert(flow->dst->id);
                
                if (params.debug_sns) {
                    std::cout << "SnsHost::send_with_parallel_receiver_limit sending to receiver " << flow->dst->id 
                              << " (active receivers: " << this->active_receivers.size() << ")" << "\n";
                }
                
                flow->send_pending_data();
                break;
            } else if (!can_send_to_receiver) {
                if (params.debug_sns) {
                    std::cout << "SnsHost::send_with_parallel_receiver_limit skipping receiver " << flow->dst->id 
                              << " (max parallel limit reached: " << this->active_receivers.size() << ")" << "\n";
                }
            }
        }
        while(!flows_tried.empty()) {
            this->active_sending_flows.push_back(flows_tried.front());
            flows_tried.pop();
        }
    }
}

void SnsHost::organize_flows_by_collective() {
    // Clear existing data structures
    collective_flows.clear();
    collective_order.clear();
    //collective_flow_index.clear();
    
    // Group flows by collective_id
    for (SnsFlow* flow : active_sending_flows) {
        if (!flow->finished) {
            uint32_t collective_id = flow->collective_id;
            collective_flows[collective_id].push_back(flow);
        }
    }
    
    // Create ordered list of collective IDs for round-robin
    for (const auto& pair : collective_flows) {
        uint32_t collective_id = pair.first;
        collective_order.push_back(collective_id);
        if (collective_flow_index.find(collective_id) == collective_flow_index.end()) {
            collective_flow_index[collective_id] = 0; // Initialize flow index only if not present
        }
    }
    
    // Reset collective round-robin index
    current_collective_index = 0;
    if (collective_order.size() > 0)
        current_collective_index = current_collective_index % collective_order.size();
    
    if (params.debug_sns) {
        std::cout << "SnsHost::organize_flows_by_collective: Found " << collective_order.size() 
                  << " collectives with flows\n";
        for (const auto& pair : collective_flows) {
            std::cout << "  Collective " << pair.first << ": " << pair.second.size() << " flows\n";
        }
    }
}

SnsFlow* SnsHost::get_next_flow_round_robin() {
    if (collective_order.empty()) {
        return nullptr;
    }
    
    // Try each collective in round-robin order to find a flow that can send
    size_t attempts = 0;
    while (attempts < collective_order.size()) {
        // Get current collective
        uint32_t collective_id = collective_order[current_collective_index];
        auto& flows_in_collective = collective_flows[collective_id];
        size_t& flow_index = collective_flow_index[collective_id];
        
        if (!flows_in_collective.empty()) {
            // Try each flow in this collective starting from current flow index
            size_t flow_attempts = 0;
            while (flow_attempts < flows_in_collective.size()) {
                flow_index = flow_index % flows_in_collective.size();
                SnsFlow* flow = flows_in_collective[flow_index];
                
                // Move to next flow within this collective for next time
                flow_index = (flow_index + 1) % flows_in_collective.size();
                
                if (!flow->finished) {
                    if (params.debug_sns) {
                        std::cout << "SnsHost::get_next_flow_round_robin: Selected flow " << flow->id 
                                  << " from collective " << collective_id << "\n";
                    }
                    
                    // Move to next collective for next SIM packet
                    current_collective_index = (current_collective_index + 1) % collective_order.size();
                    return flow;
                }
                
                flow_attempts++;
            }
        }
        
        // Move to next collective if current one has no available flows
        current_collective_index = (current_collective_index + 1) % collective_order.size();
        attempts++;
    }
    
    if (params.debug_sns) {
        std::cout << "SnsHost::get_next_flow_round_robin: No available flows found\n";
    }
    return nullptr;
}

void SnsHost::send_with_collective_round_robin(){
    if (params.debug_sns) {
        std::cout << "SnsHost::send_with_collective_round_robin time" << (get_current_time()-1)*1e6  << "\n";
    }
    
    // Organize flows by collective if needed
    organize_flows_by_collective();
    if (auto_fallback_single_collective && collective_order.size() <= 1) {
        if (params.debug_sns) {
            std::cout << "SnsHost::send_with_collective_round_robin: Single collective detected, using simple round-robin\n";
        }
        
        // Use simple round-robin logic (same as enable_collective_round_robin = false)
        std::queue<SnsFlow*> flows_tried;
        
        while(!this->active_sending_flows.empty()) {
            SnsFlow* flow = this->active_sending_flows.front();
            this->active_sending_flows.pop_front();
            
            if(flow->finished) {
                if (params.debug_sns) {
                    std::cout << "SnsHost::send_with_collective_round_robin time flow->finished" << (get_current_time()-1)*1e6  << "\n";
                }
                continue;
            }
            
            flows_tried.push(flow);
            
            if(!flow->finished) {
                if (params.debug_sns) {
                    std::cout << "SnsHost::send_with_collective_round_robin sending to receiver " << flow->dst->id 
                              << " (single collective mode)" << "\n";
                }
                
                flow->send_pending_data();
                break;
            }
        }
        while(!flows_tried.empty()) {
            this->active_sending_flows.push_back(flows_tried.front());
            flows_tried.pop();
        }
        return;
    }
    
        std::queue<SnsFlow*> flows_tried;
        
        while(!this->active_sending_flows.empty()) {
            // Get the next flow using collective round-robin
            SnsFlow* flow = get_next_flow_round_robin();
            
            if (flow == nullptr) {
                // No flows available - this means active_sending_flows is empty or all flows are finished
                if (params.debug_sns) {
                    std::cout << "SnsHost::send_with_collective_round_robin no flows available to send\n";
                }
                break;
            }
            
            // Remove the selected flow from active_sending_flows (similar to pop_front in original)
            auto it = std::find(this->active_sending_flows.begin(), this->active_sending_flows.end(), flow);
            if (it != this->active_sending_flows.end()) {
                this->active_sending_flows.erase(it);
            }
            
            if(flow->finished) {
                if (params.debug_sns) {
                    std::cout << "SnsHost::send_with_collective_round_robin time flow->finished" << (get_current_time()-1)*1e6  << "\n";
                }
                // Don't add finished flows back to the queue
                continue;
            }
            
            flows_tried.push(flow);
            
            if(!flow->finished) {
                if (params.debug_sns) {
                    std::cout << "SnsHost::send_with_collective_round_robin sending to receiver " << flow->dst->id 
                              << " from collective " << flow->collective_id << "\n";
                }
                
                flow->send_pending_data();
                break;
            }
        }
        
        // Put back the flows that were tried but not sent from (similar to original send logic)
        while(!flows_tried.empty()) {
            this->active_sending_flows.push_back(flows_tried.front());
            flows_tried.pop();
        }
    // }
}

// Leaf Spine Switches
SnsCoreSwitch::SnsCoreSwitch(uint32_t id, uint32_t nq, 
double rate, uint32_t type) : SnsSwitch(id, CORE_SWITCH) {
    double sns_simu_fraq = params.sns_simulation_frq_size;
    double regular_rate = rate - 1*rate/sns_simu_fraq;
    double simulation_rate = rate/sns_simu_fraq;
    double simulation_ack_rate = rate/sns_simu_fraq;
    params.sns_min_rate = std::min({params.sns_min_rate, regular_rate});
    for (uint32_t i = 0; i < nq; i++) {
        double rate_i = rate;
        if (params.core_div_2) {
            if (i % 2 == 0){
                rate_i /= 100;
            }
        }
        queues.push_back(Factory::get_queue(i, rate_i, params.queue_size, SNS_QUEUE, 0, 2));
    }

}

//nq1: # host switch, nq2: # core switch
SnsAggSwitch::SnsAggSwitch(
        uint32_t id, 
        uint32_t nq1, 
        double r1,
        uint32_t nq2, 
        double r2, 
        uint32_t type
        ) : SnsSwitch(id, AGG_SWITCH) {
    double sns_simu_fraq = params.sns_simulation_frq_size;
    double r1_regular_rate = r1 - 1*r1/sns_simu_fraq;
    double r1_simulation_rate = r1/sns_simu_fraq;
    double r1_simulation_ack_rate = r1/sns_simu_fraq;
    double r2_regular_rate = r2 - 1*r2/sns_simu_fraq;
    double r2_simulation_rate = r2/sns_simu_fraq;
    double r2_simulation_ack_rate = r2/sns_simu_fraq;
    params.sns_min_rate = std::min({params.sns_min_rate, r1_regular_rate, r2_regular_rate});
    for (uint32_t i = 0; i < nq1; i++) {

        queues.push_back(Factory::get_queue(i, r1, params.queue_size, SNS_QUEUE, 0, 3));
        int queue_id = queues[queues.size() - 1]->unique_id;
        }
    for (uint32_t i = 0; i < nq2; i++) {
        queues.push_back(Factory::get_queue(i, r2, params.queue_size, SNS_QUEUE, 0, 1));
        int queue_id = queues[queues.size() - 1]->unique_id;
        }
}
