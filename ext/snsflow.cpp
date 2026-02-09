#include <assert.h>
#include <deque>
#include <algorithm>

#include "snsflow.h"

#include "../coresim/event.h"
#include "../coresim/snshost.h"
#include "../ext/schedulinghost.h"
#include "../coresim/flow.h"
#include "../coresim/topology.h"
#include "../coresim/SnsFatTreeTopology.h"
#include "../coresim/SnsleafSpineTopology.h"

#include "../run/params.h"
extern DCExpParams params;
extern long long num_outstanding_packets;
extern long long max_outstanding_packets;
extern uint32_t duplicated_packets_received;
extern Topology* topology;
extern std::deque<Flow*> flows_to_schedule;

extern double get_current_time();
extern void add_to_event_queue(Event* ev);
void RollingAverage::addValue(double value) {
    times[index] = value;  // Overwrite the oldest value
    index = (index + 1) % 3; // Move index in circular manner
    if (count < 3) count++; // Ensure count doesn't exceed 3
}

double RollingAverage::getAverage() const {
    if (count == 0) return 0; // Avoid division by zero
    double sum = 0;
    for (int i = 0; i < count; i++) sum += times[i];
    return sum / count;
}

SnsFlow::SnsFlow(uint32_t id, double start_time, uint32_t size, Host* s, Host* d) : Flow(id, start_time, size, s, d) {
    this->window_size = this->size_in_pkt;
    this->sim_window = new WindowMsgElement[this->window_size];
    this->dst_current_active_flows = 1;
    for (int i = 0; i<this->window_size; i++){
        this->sim_window[i].time = -1; //new ready to send sims
        this->sim_window[i].id = -1;
        this->sim_window[i].status = INIT;
       
    }
    
    // Initialize default values for path history matrix
    this->k_value = 0;  // Will be set by init_path_matrix
    this->time_steps = 0;  // Will be set by init_path_matrix
    this->current_time_step = 0;  // Start at the first time step
    this->enable_path_saving = 0;  // Initialize immediately to prevent corruption
    
    this->flow_rtt = params.rtt;
    this->current_avg_rtt = params.rtt;  // Initialize current_avg_rtt with the default RTT
    sns_sim_pkt_drop = 0;
    msg_id = 0;
    data_msg_id = 0;
    stop_sending_sim = false;
    sim_turn = 0;
    last_sim_sent_time = 1;
    total_queuing_time = 0;
    received_count = 0;
    
    // Pre-allocate space for queueing data to improve performance
    this->queueing_times.reserve(this->size_in_pkt * 4);  // Reserve space for SIM + DATA packets
    
    // std::cout << "Constructor: enable_path_saving=" << this->enable_path_saving << " (should be 0)\n";
    // this->dst->id = 135;
}

SnsFlow::~SnsFlow() {
    // Clean up the dynamically allocated sim_window array
    if (sim_window != nullptr) {
        delete[] sim_window;
        sim_window = nullptr;
    }
}

void SnsFlow::start_flow() {
    if (params.debug_sns)
        std::cout << "SnsFlow::start_flow for flow " << this->id << "\n";
    
    // Initialize enable_path_saving to 0 if not set elsewhere
    this->enable_path_saving = 0;
    
    // Enhanced initialization of the path history matrix
    bool matrix_initialized = false;
    
    // For Fat Tree Topology, get the k parameter from the topology
    if (dynamic_cast<SnsFatTreeTopology*>(topology) != nullptr) {
        SnsFatTreeTopology* fat_tree = dynamic_cast<SnsFatTreeTopology*>(topology);
        // Get k value from the topology
        int k = fat_tree->_k;
        if (k > 0 && enable_path_saving) {
            // Use a reasonable number of time steps (e.g., 50)
            init_path_matrix(k, 50);
            matrix_initialized = true;
            if (params.debug_sns)
                std::cout << "Initialized path matrix with k=" << k << " and time_steps=50 for flow " << this->id << "\n";
        } else {
            if (params.debug_sns)
                std::cout << "Warning: Invalid k value " << k << " from topology for flow " << this->id << "\n";
        }
    }
    // For Leaf-Spine Topology, use a reasonable default
    else if (dynamic_cast<SnsLeafSpineTopology*>(topology) != nullptr) {
        // For Leaf-Spine, we can use the number of core switches as k
        SnsLeafSpineTopology* leaf_spine = dynamic_cast<SnsLeafSpineTopology*>(topology);
        int k = 2 * leaf_spine->num_core_switches; // Use 2x num_core_switches as an approximation
        if (k > 0 && enable_path_saving) {
            init_path_matrix(k, 50);
            matrix_initialized = true;
            if (params.debug_sns)
                std::cout << "Initialized path matrix with k=" << k << " and time_steps=10 for flow " << this->id << "\n";
        } else {
            if (params.debug_sns)
                std::cout << "Warning: Invalid k value " << k << " from topology for flow " << this->id << "\n";
        }
    }
    
    // If initialization failed, use a safe default
    if (!matrix_initialized && enable_path_saving) {
        // Use a reasonable default of k=4 (port_count=2)
        init_path_matrix(4, 50);
        if (params.debug_sns)
            std::cout << "Using default path matrix with k=4 and time_steps=10 for flow " << this->id << "\n";
    }
    
    ((SnsHost*) this->src)->start_flow(this);
    return;    
    
}

int SnsFlow::get_free_slot_in_window(){
    bool need_to_stop = true;
    for (int i = 0; i<this->window_size; i++){
        if (this->sim_window[i].status == INIT)
            return i;
        if (this->sim_window[i].status != DATA_ACK_RECEIVED)
            need_to_stop = false;
    }
    if (need_to_stop){
        stop_sending_sim = true;
        return -2;
    }

    drop_sim_if_timeout_pass();

    for (int i = 0; i<this->window_size; i++)
        if (this->sim_window[i].status == INIT)
            return i;
    return -1;
}


int SnsFlow::get_slot_by_id(int id){
    for (int i = 0; i<this->window_size; i++)
        if (this->sim_window[i].id == id)
            return i;
    return -1;
}

void SnsFlow::drop_sim_if_timeout_pass(){
    double current_time = get_current_time();
    for (int i = 0; i < this->window_size; i++){
        if (this->sim_window[i].status == INIT || this->sim_window[i].status == DATA_ACK_RECEIVED)
            continue;
        float time_to_wait = this->current_avg_rtt * 1.5;
        time_to_wait = fmin(time_to_wait, 10*this->flow_rtt);
        if ((current_time - this->sim_window[i].time) > time_to_wait){

            this->sim_window[i].time = -1;
            this->sim_window[i].id = -1;
            this->sim_window[i].status = INIT;
            sns_sim_pkt_drop++; // maybe need handle in queue.cpp
        }
        
    }
}
void SnsFlow::check_and_set_to_finished() {
    if (params.debug_sns)
            std::cout << "check_and_set_to_finished flow_id:  " << this->id << " received_bytes " <<  received_bytes << " size "<< size << " start_time " << start_time << " received_bytes " << received_bytes << "\n";
        
    if (!finished && received_bytes >= size - 1) {        
        finished = true;
        stop_sending_sim = true;
        received.clear();
        finish_time = get_current_time();
        flow_completion_time = finish_time - start_time;
        if (params.debug_sns)
            std::cout << "2flowid: " << this->id << " flow_rtt4 " <<  flow_rtt << " finish_time "<< finish_time << " start_time " << start_time << " received_bytes " << received_bytes << "\n";
        FlowFinishedEvent *ev = new FlowFinishedEvent(get_current_time(), this);
        add_to_event_queue(ev);
        cancel_retx_event();
    }
}

int SnsFlow::get_number_of_flows_in_host(Host *dst_host){
    std::set<int> uniq_flows_id;
    
    // Iterate through all flows in the system
    // Instead of checking packets in queues, directly check each flow
    // This is a more reliable approach to count active flows
    for (auto it = flows_to_schedule.begin(); it != flows_to_schedule.end(); ++it) {
        Flow* flow = *it;
        
        // Skip this flow (the current one)
        if (flow->id == this->id)
            continue;
        
        // Count the flow if it:
        // 1. Has the specified destination host
        // 2. Has started (start_time <= current_time)
        // 3. Hasn't finished yet
        if (flow->dst->id == dst_host->id && 
            flow->start_time <= get_current_time() && 
            !flow->finished) {
            uniq_flows_id.insert(flow->id);
        }
    }
    
    // Always count at least 1 flow (this one)
    if (uniq_flows_id.size() > 0) {
        if (params.debug_sns) {
            std::cout << "Active flows to host " << dst_host->id << ": ";
            for (const auto& elem : uniq_flows_id) {
                std::cout << elem << " ";
            }
            std::cout << this->id << " (current flow)\n";
        }
        return uniq_flows_id.size() + 1;
    }
    else {
        return 1;
    }
}

void SnsFlow::send_simulation() {
    uint32_t pkt_size = params.hdr_size;
    int free_slot = get_free_slot_in_window();
     if (stop_sending_sim){
        cancel_retx_event();
        return;
    }
    double td_sim = (params.hdr_size*8.0)/(params.bandwidth/params.sns_simulation_frq_size);

    if (free_slot != -1){
        double current_time = get_current_time();
        //log_utilization_include_sim(40);
        SnsPacket *p;
        
        // Sample port pair based on history
        std::pair<int, int> port_pair = sample_port_pair_from_history(2.0);
        int sampled_hash_port = port_pair.first;
        int sampled_hash_port_l2 = port_pair.second;
        // std::cout << "sampled_hash_port: " << sampled_hash_port << " sampled_hash_port_l2: " << sampled_hash_port_l2 << std::endl;
        
        // Enhanced validation for sampled ports
        // Check if topology is properly initialized
        if (k_value <= 0) {
            if (params.debug_sns)
                std::cout << "Warning: k_value not properly initialized (" << k_value 
                          << "), using default port pair (0,0)" << std::endl;
            sampled_hash_port = 0;
            sampled_hash_port_l2 = 0;
        } else {
            // Ensure sampled ports are valid for the current topology
            int port_count = k_value / 2;
            if (port_count <= 0) {
                if (params.debug_sns)
                    std::cout << "Warning: Invalid port_count (" << port_count 
                              << "), using default port pair (0,0)" << std::endl;
                sampled_hash_port = 0;
                sampled_hash_port_l2 = 0;
            } else if (sampled_hash_port < 0 || sampled_hash_port >= port_count || 
                sampled_hash_port_l2 < 0 || sampled_hash_port_l2 >= port_count) {
                // Invalid values, set to default
                if (params.debug_sns)
                    std::cout << "Warning: Invalid port pair after sampling (" 
                              << sampled_hash_port << "," << sampled_hash_port_l2 
                              << "), using default (0,0)" << std::endl;
                sampled_hash_port = 0;
                sampled_hash_port_l2 = 0;
            }
        }
        
        p = new SnsPacket(SNS_SIM_PACKET, get_current_time(), this, 0, 0, pkt_size, this->src, this->dst, -1, this->msg_id);
        p->saved_hash_port = sampled_hash_port;
        p->saved_hash_port_l2 = sampled_hash_port_l2;
        
        double packet_sent_time = get_current_time();
        double td_regular = ((SnsHost*)src)->queue->get_transmission_delay(p);
        // if (last_sim_sent_time + td_sim > get_current_time())
        //     packet_sent_time = last_sim_sent_time + td_sim;
        packet_sent_time = get_current_time();
        p->sending_time = packet_sent_time;
        if (params.debug_sns2)
            std::cout << "send_pending_data time: " << (packet_sent_time-1)*1e6 << " flowid " << this->id << " send sim_msg_id: " << this->msg_id << "\n";
        ((SnsHost*)this->src)->sims_to_be_sent.push_back(p);
        //add_to_event_queue(new PacketQueuingEvent(packet_sent_time, p,((SnsHost*)this->src)->queue));
        params.total_sim_pkts += 1;
        static std::ofstream drop_pkt_file;

        // if(params.drop_pkt_file != "") {
        //     if(!drop_pkt_file.is_open()) {
        //         drop_pkt_file.open(params.drop_pkt_file);
        //     }
        //     drop_pkt_file << (current_time-1)*1000000 << " " << params.total_sim_pkts << " " << params.total_drop_sim_pkts << " " << params.total_data_pkts << " " << params.total_drop_data_pkts << std::endl;
        // }
        // add_to_event_queue(new PacketQueuingEvent(current_time, new SnsPacket(SNS_SIM_PACKET, get_current_time(), this, 0, 0, pkt_size, this->src, this->dst, -1, this->msg_id), (this->src)->queue));
        this->sim_window[free_slot].time = current_time;
        this->sim_ids_sent.insert(msg_id);
        this->sim_window[free_slot].id = msg_id++;
        this->sim_window[free_slot].status = SIM_SENT;;

        if (params.debug_sns) {
            // if (this->id == 0){
            std::cout << "SnsFlow::Send SNS_SIM_PACKET msg_id: " << msg_id << " free_slot " << free_slot << "\n";
            std::cout << "SnsFlow::Send SNS_SIM_PACKET current_time: " << (current_time-1)*1000000  << " free_slot " << free_slot << "\n";
            std::cout << "SnsFlow::Send SNS_SIM_PACKET params.mss: " << params.mss << " params.bandwidth " << params.bandwidth << "\n";
            std::cout << "SnsFlow::Send SNS_SIM_PACKET data_pkt_drop: " << this->data_pkt_drop << " sns_sim_pkt_drop " << this->sns_sim_pkt_drop << "\n";
            // }
        }
        double td = 0;
        

        if (params.debug_sns) {
            std::cout << "SnsFlow::Send SNS_SIM_PACKET msg_id: " << msg_id << " td: " << td << "\n";
        }
        //assert(((SnsHost*) src)->host_proc_sns_sim_event == NULL);
        double need_to_wait = ((SnsHost*) src)->sims_to_be_sent.size()*td_sim;
        need_to_wait = std::max(td_sim, need_to_wait);
        ((SnsHost*) src)->host_proc_sns_sim_event = new SnsSimulationHostProcessingEvent(packet_sent_time + need_to_wait + INFINITESIMAL_TIME, (SnsHost*) src);
        last_sim_sent_time = packet_sent_time;
        add_to_event_queue(((SnsHost*) src)->host_proc_sns_sim_event);
   
    } else {
        if (params.debug_sns2)
            std::cout << "send_pending_data no slot time: " << (get_current_time()-1)*1e6 << " flowid " << this->id << " send sim_msg_id: " << this->msg_id << "\n";        
        double need_to_wait = ((SnsHost*) src)->sims_to_be_sent.size()*td_sim;
        need_to_wait = std::max(td_sim, need_to_wait);
        ((SnsHost*) src)->host_proc_sns_sim_event = new SnsSimulationHostProcessingEvent(get_current_time() + need_to_wait + INFINITESIMAL_TIME, (SnsHost*) src);
        add_to_event_queue(((SnsHost*) src)->host_proc_sns_sim_event);          
    }    
}

void SnsFlow::send_pending_data() {
    check_and_set_to_finished(); 
    
    if (this->collective_id == 99){
        SnsPacket *new_p;
        uint32_t pkt_size = params.mss + params.hdr_size;
        static int collective_msg_id = 0;
        new_p = new SnsPacket(SNS_DATA_PACKET, get_current_time(), this, collective_msg_id++, 0, pkt_size, this->src, this->dst, -1, collective_msg_id);
        ((SnsHost*)this->src)->host_proc_sns_add_to_pkt_to_be_sent_event = new SnsAddToPktToBeSentEvt(get_current_time(), ((SnsHost*)this->src), new_p);
        add_to_event_queue(((SnsHost*)this->src)->host_proc_sns_add_to_pkt_to_be_sent_event);
    }
    else
        send_simulation();
    double td_sim = (params.hdr_size*8.0)/(params.bandwidth/params.sns_simulation_frq_size);
    double td_wait = td_sim;
    td_wait *= params.num_data_packets_per_sim_ack;
    ((SnsHost*) src)->host_proc_event = new HostProcessingEvent(get_current_time() + td_wait + INFINITESIMAL_TIME, (SnsHost*) src);
    add_to_event_queue(((SnsHost*) src)->host_proc_event);
}
void SnsFlow::set_timeout(double time) {
    if (received_bytes < size) {
        RetxTimeoutEvent *ev = new RetxTimeoutEvent(time, this);
        add_to_event_queue(ev);
        retx_event = ev;
    }
}

void SnsFlow::handle_timeout() {
    next_seq_no = last_unacked_seq;
    //Reset congestion window to 1
    cwnd_mss = 1;
    assert(1);
    send_pending_data();}

void SnsFlow::receive(Packet *_p) {
    SnsPacket *p = (SnsPacket*) _p;
    check_and_set_to_finished();
    if (params.debug_sns) {
        std::cout << "3flowid: " << this->id << "p->sns_type " <<  p->sns_type << "\n";
    }
    uint32_t pkt_size = params.mss + params.hdr_size;
    log_queuing();
    
    if (p->sns_type == SNS_SIM_PACKET) {
        // send SYNACK
        if (params.debug_sns) {
            std::cout << "SnsFlow::receive SNS_SIM_PACKET p->saved_hash_port: " << p->saved_hash_port << "p->type " <<  p->type << "p->sns_type " <<  p->sns_type << "\n";
        }
        
      
        if (this->enable_path_saving != 0 && p->saved_hash_port >= 0 && p->saved_hash_port_l2 >= 0 && k_value > 0 && time_steps > 0) {
           reset_current_time_step();
            
            // Record successful path in the history matrix
            add_path_to_matrix(p->saved_hash_port, p->saved_hash_port_l2, current_time_step, 1);
            
            // Advance to the next time step for future updates - CHECK FOR DIVISION BY ZERO
            if (time_steps > 0) {
                current_time_step = (current_time_step + 1) % time_steps;
            } else {
                std::cout << "ERROR: time_steps is 0, cannot perform modulo operation!\n";
            }
        }
        
        
        if (params.debug_sns)
            std::cout << "flowid: " << this->id << "Creating SNS_SIM_ACK_PACKET  seq: " <<  p->seq_no << "\n";
        if (params.debug_sns2)
            std::cout << "receive sim time: " << (get_current_time()-1)*1000000 << " flowid " << this->id << " got sim_msg_id: " << p->sns_msg_id << " sending_time: " << (p->sending_time-1)*1000000 << " time_taken: " << (get_current_time()-p->sending_time)*1000000 << " size: " << p->size << "\n";
        
        // Save queueing delay information for SNS_SIM_PACKET
        // Format: [sns_msg_id, packet_type (0=SIM), queuing_delay_microseconds]
        if (!this->finished) {
            std::vector<double> queueing_entry;
            queueing_entry.reserve(4);  // Pre-allocate space for efficiency
            queueing_entry.push_back((get_current_time() - 1)*1e6); // current time in microseconds
            queueing_entry.push_back(static_cast<double>(p->sns_msg_id));
            queueing_entry.push_back(0.0); // 0 indicates SIM packet
            double time_taken = (get_current_time() - p->sending_time) * 1000000.0; // in microseconds
            queueing_entry.push_back(time_taken); // queuing delay in microseconds
            this->queueing_times.push_back(queueing_entry);
        }

        SnsPacket *new_p;

        new_p = new SnsPacket(SNS_SIM_ACK_PACKET, get_current_time(), this, p->seq_no, 0, pkt_size, this->dst, this->src, p->saved_hash_port, p->sns_msg_id);
        new_p->saved_hash_port_l2 = p->saved_hash_port_l2;
        new_p->sns_current_active_flows = 1;
        ((SnsHost*)this->dst)->packets_to_be_sent.push_back(new_p);
        
        if  (((SnsHost*)this->dst)->host_proc_sns_send_packet_event == NULL){
            // assert(0);

            ((SnsHost*)this->dst)->host_proc_sns_send_packet_event = new SnsSendPacketProcessingEvent(get_current_time(), ((SnsHost*)this->dst));
            add_to_event_queue(((SnsHost*)this->dst)->host_proc_sns_send_packet_event);
        }

        //
        this->sim_ack_ids_sent.insert(p->sns_msg_id);
        this->sim_times.addValue((get_current_time()-p->sending_time)*1e6);
        return;
    }
    else if (p->sns_type == SNS_SIM_ACK_PACKET){

        if (params.debug_sns) {
            std::cout << "flowid: " << this->id << " SnsFlow::receive SNS_SIM_ACK_PACKET seq: " <<  p->seq_no << "\n";
            std::cout << "SnsFlow::receive SNS_SIM_ACK_PACKET p->saved_hash_port: " << p->saved_hash_port << "p->type " <<  p->type << "p->sns_type " <<  p->sns_type << "\n";
        } 
        if (params.debug_sns)
            std::cout << "flowid: " << this->id << "Creating SNS_DATA_PACKET  seq: " <<  p->seq_no << "\n";
        int slot_id = get_slot_by_id(p->sns_msg_id);
        if (slot_id == -1){
            if (params.debug_sns)
                std::cout << "flowid: " << this->id << " Got SIM_ACK from deleted SIM for this msg_id " <<  p->sns_msg_id << "\n";
        }
        if (params.debug_sns){
            std::cout << "flowid: " << this->id << " time_taken: " <<  get_current_time() - this->sim_window[slot_id].time << "\n";
                        std::cout << "SnsFlow::Send SNS_DATA_PACKET current_time: " << (get_current_time()-1)*1000000 << "\n";

        }

        // Update current_avg_rtt with the measured RTT from SIM to SIM_ACK
        if (slot_id != -1) {
            double measured_rtt = get_current_time() - this->sim_window[slot_id].time;
            // Use exponential weighted moving average (EWMA) with alpha = 0.125 (1/8)
            this->current_avg_rtt = 0.875 * this->current_avg_rtt + 0.125 * measured_rtt;
            
            if (params.debug_sns) {
                std::cout << "flowid: " << this->id << " measured_rtt: " << measured_rtt * 1000000 << " us, current_avg_rtt: " << this->current_avg_rtt * 1000000 << " us\n";
            }
        }

        //packet_complete_time_file << (get_current_time()-this->sim_window[slot_id].time)*1000000 << " 1 \n";
        this->dst_current_active_flows = p->sns_current_active_flows;
        double time_to_be_sent = get_current_time();
        if (slot_id != -1){
            if (params.topology == "FatTree"){
                SnsFatTreeTopology* fat_tree = dynamic_cast<SnsFatTreeTopology*>(topology);
                time_to_be_sent = this->sim_window[slot_id].time + fat_tree->get_worst_rtt(this); // 06.09.25
                if (params.debug_sns){
                    std::cout << "flowid: " << this->id << " time_to_be_sent: " <<  time_to_be_sent  << "\n";
                    std::cout << "get_worst_rtt: " << fat_tree->get_worst_rtt(this) << "\n";
                }
            } else {
                SnsLeafSpineTopology* leaf_spine = dynamic_cast<SnsLeafSpineTopology*>(topology);
                time_to_be_sent = this->sim_window[slot_id].time + leaf_spine->get_worst_rtt(this); // 06.09.25
                if (params.debug_sns){
                    std::cout << "flowid: " << this->id << " time_to_be_sent: " <<  time_to_be_sent  << "\n";
                    std::cout << "get_worst_rtt: " << leaf_spine->get_worst_rtt(this) << "\n";
                }
            }
            this->sim_window[slot_id].status = SIM_ACK_RECEIVED;
        }
            //time_to_be_sent = this->sim_window[slot_id].time + this->flow_rtt*3;
        double sim_plus_sim_ack = get_current_time() - this->sim_window[slot_id].time;

        if (time_to_be_sent < get_current_time()) {
            assert(time_to_be_sent < get_current_time());
            
            time_to_be_sent = get_current_time();
            if (params.debug_sns){
                std::cout << "flowid: " << this->id << " time_to_be_sent: " <<  time_to_be_sent  << " shouldn't get here at all" << "\n";
            }
        }
        if (!params.synced_mode)
            time_to_be_sent = get_current_time(); 
        this->data_ids_sent.insert(p->sns_msg_id);
        this->sim_window[slot_id].time = time_to_be_sent;
        if (params.debug_sns2)
            std::cout << "receive sim_ack time: " << (get_current_time()-1)*1000000 << " flowid " << this->id << " got sim_msg_id: " << p->sns_msg_id  << " sending_time: " << (p->sending_time-1)*1000000 << " time_taken: " << (get_current_time()-p->sending_time)*1000000 << " size: " << p->size << "\n";
        
        // Send multiple data packets per SIM ACK based on configuration
        uint32_t num_data_packets = params.num_data_packets_per_sim_ack;
        params.total_data_pkts += num_data_packets;
        
        if (params.debug_sns && num_data_packets > 1) {
            std::cout << "Sending " << num_data_packets << " data packets for SIM ACK " << p->sns_msg_id << std::endl;
        }
        for (uint32_t i = 0; i < num_data_packets; i++) {
            SnsPacket *new_p;
            double td_regular = ((params.mss + params.hdr_size)*8.0)/(params.bandwidth);
            double packet_time = time_to_be_sent + i * 0.000000001; // 1ns offset per packet
            new_p = new SnsPacket(SNS_DATA_PACKET, time_to_be_sent, this, p->seq_no + i, 0, pkt_size, this->src, this->dst, p->saved_hash_port, p->sns_msg_id);
            new_p->saved_hash_port_l2 = p->saved_hash_port_l2;
            if (params.debug_sns2)
                std::cout << "receive sim_ack time: " << (get_current_time()-1)*1000000 << " flowid " << this->id << " got sim_msg_id: " << p->sns_msg_id  << " sending_time: " << (p->sending_time-1)*1000000 << " time_taken: " << (get_current_time()-p->sending_time)*1000000 << " saved_hash: " << p->saved_hash_port << " packet " << (i+1) << "/" << num_data_packets << "\n";
            ((SnsHost*)this->src)->host_proc_sns_add_to_pkt_to_be_sent_event = new SnsAddToPktToBeSentEvt(packet_time, ((SnsHost*)this->src), new_p);
            add_to_event_queue(((SnsHost*)this->src)->host_proc_sns_add_to_pkt_to_be_sent_event);
        }
        if (((SnsHost*)this->src)->host_proc_sns_send_packet_event == NULL){
            ((SnsHost*)this->src)->host_proc_sns_send_packet_event = new SnsSendPacketProcessingEvent(get_current_time(), ((SnsHost*)this->src));
            add_to_event_queue(((SnsHost*)this->src)->host_proc_sns_send_packet_event);
        }

        this->total_pkt_sent += params.num_data_packets_per_sim_ack;  // Account for multiple packets sent
        
        return;
    }
    else if (p->sns_type == SNS_DATA_ACK_PACKET){
        for (uint32_t slot_id : p->sns_ack_data_ids) {
            if (slot_id >= this->size_in_pkt)
                continue;
            if (acked_msg_ids.find(slot_id) != acked_msg_ids.end())
                continue;
            acked_msg_ids.insert(slot_id);           
            this->sim_window[slot_id].status = DATA_ACK_RECEIVED;
            
        }

    }
    else{  //receiving data packet
        if (params.debug_sns) {
            std::cout << "flowid: " << this->id << " SnsFlow::receive SNS_DATA_PACKET received_bytes: " <<  received_bytes << "\n";
            std::cout << "SnsFlow::receive SNS_DATA_PACKET p->saved_hash_port: " << p->saved_hash_port << "p->type " <<  p->type << "p->sns_type " <<  p->sns_type << "\n";
        }
        if (params.debug_sns2)
            std::cout << "receive data time: " << (get_current_time()-1)*1000000 << " flowid " << this->id << " got sim_msg_id: " << p->sns_msg_id << " sending_time: " << (p->sending_time-1)*1000000 << " time_taken: " << (get_current_time()-p->sending_time)*1000000 << " size: " << p->size  << "\n";
        
        // Save queueing delay information for SNS_DATA_PACKET
        // Format: [sns_msg_id, packet_type (1=DATA), queuing_delay_microseconds]
        if (!this->finished) {
            std::vector<double> queueing_entry;
            queueing_entry.reserve(4);  // Pre-allocate space for efficiency
            queueing_entry.push_back((get_current_time() - 1)*1e6); // current time in microseconds
            queueing_entry.push_back(static_cast<double>(p->sns_msg_id));
            queueing_entry.push_back(1.0); // 1 indicates DATA packet
            // queueing_entry.push_back(p->total_queuing_delay * 1000000.0); // queuing delay in microseconds
            double time_taken = (get_current_time() - p->sending_time) * 1000000.0; // in microseconds
            queueing_entry.push_back(time_taken); // queuing delay in microseconds
            this->queueing_times.push_back(queueing_entry);
        }
        
        this->data_ids_recived.insert(p->sns_msg_id);
        //assert(false);
        if (finished) {
            delete p;
            p =nullptr;
            return;
        }
        if (this->first_byte_receive_time == -1) {
            this->first_byte_receive_time = get_current_time();
        }
        
        if (this->data_ids_recived.size() % 16 == 0 ){
            send_data_ack();
        }
        
        if (this->last_id_received == -1) {
            // First packet received, just initialize
            this->last_id_received = p->sns_msg_id;
        }
        else if (p->sns_msg_id > this->last_id_received){
            this->last_id_received = p->sns_msg_id;
        }
        else if (p->sns_msg_id < this->last_id_received){
            this->num_of_reorders += 1;
            log_drop_reorder(0, 1, 0);
        }
        log_drop_reorder(0, 0, 1);
        //received_bytes += (p->size - hdr_size);
        if (received.count(p->sns_msg_id) == 0) {
            received.insert(p->sns_msg_id);
            if(num_outstanding_packets >= ((p->size - hdr_size) / (mss)))
                num_outstanding_packets -= ((p->size - hdr_size) / (mss));
            else
                num_outstanding_packets = 0;
            received_bytes += (p->size - hdr_size);
        } else {
                duplicated_packets_received += 1;
        }
        this->received_count++;
        this->total_queuing_time += p->total_queuing_delay;
        check_and_set_to_finished();
        this->data_times.addValue((get_current_time()-p->sending_time)*1e6);
        log_utilization(p->size);
        

    }
    
}
void SnsFlow::send_data_ack(){
    SnsPacket *new_p;
    new_p = new SnsPacket(SNS_DATA_ACK_PACKET, get_current_time(), this, 0, 0, params.hdr_size, this->dst, this->src, -1, 0);
    new_p->sns_ack_data_ids = this->data_ids_recived;
    ((SnsHost*)this->dst)->packets_to_be_sent.push_back(new_p);
    if (this->finished)
        return;
    
}

SnsFlowSendDataAckEvent::SnsFlowSendDataAckEvent(double time, SnsFlow *f) : Event(HOST_PROCESSING, time) {
    this->flow = f;
    this->is_timeout = false;
}

SnsFlowSendDataAckEvent::~SnsFlowSendDataAckEvent() {
    if ((this->flow)->send_data_ack_event == this) {
        (this->flow)->send_data_ack_event = NULL;
    }
}

void SnsFlowSendDataAckEvent::process_event() {
    (this->flow)->send_data_ack_event = NULL;
    (this->flow)->send_data_ack();
}

/**
 * Initialize the path history matrix with the given parameters.
 * 
 * @param k The k parameter of the topology (maximum port index is k/2)
 * @param time_steps Number of time steps to track
 */
void SnsFlow::init_path_matrix(int k, int time_steps) {
    // Enhanced validation
    if (k <= 0) {
        if (params.debug_sns)
            std::cout << "Warning: Invalid k value " << k << " in init_path_matrix, using default k=4" << std::endl;
        k = 4; // Use a safe default
    }
    
    if (time_steps <= 0) {
        if (params.debug_sns)
            std::cout << "Warning: Invalid time_steps value " << time_steps << " in init_path_matrix, using default 10" << std::endl;
        time_steps = 10; // Use a safe default
    }
    
    this->k_value = k;
    this->time_steps = time_steps;
    this->current_time_step = 0;  // Reset time step when initializing the matrix
    
    // Calculate total number of possible port pairs: (k/2) * (k/2)
    int port_count = k / 2;
    if (port_count <= 0) {
        if (params.debug_sns)
            std::cout << "Warning: Invalid port_count " << port_count << " in init_path_matrix, using default 2" << std::endl;
        port_count = 2; // Use a safe default
        this->k_value = 4; // Update k_value to match
    }
    
    int total_pairs = port_count * port_count;
    
    // Initialize the matrix with 'total_pairs' rows (one for each port pair)
    // and 'time_steps' columns (one for each time step)
    hist_path_matrix.clear();
    hist_path_matrix.resize(total_pairs);
    
    // Initialize each row with 'time_steps' elements, all set to 0
    for (int i = 0; i < total_pairs; i++) {
        hist_path_matrix[i].resize(time_steps, 0);
    }
    
    if (params.debug_sns)
        std::cout << "Matrix initialized with dimensions: " << total_pairs << "x" << time_steps 
                  << " (port_count=" << port_count << ", first time step set to 1)" << std::endl;
    
    // Print the matrix after initialization
    // print_path_matrix();
}

/**
 * Calculate the index in the matrix for a given port pair.
 * 
 * @param saved_hash_port First port hash value (0 to k/2-1)
 * @param saved_hash_port_l2 Second port hash value (0 to k/2-1)
 * @return The index in the matrix for this port pair
 */
int SnsFlow::get_port_pair_index(int saved_hash_port, int saved_hash_port_l2) {
    // Ensure we have a valid k value
    if (k_value <= 0) {
        return -1; // Error: matrix not initialized
    }
    
    int port_count = k_value / 2;
    
    // Check if port values are valid
    if (saved_hash_port < 0 || saved_hash_port >= port_count ||
        saved_hash_port_l2 < 0 || saved_hash_port_l2 >= port_count) {
        return -1; // Error: invalid port values
    }
    
    // Calculate the index in the matrix: row = saved_hash_port * port_count + saved_hash_port_l2
    return saved_hash_port * port_count + saved_hash_port_l2;
}

/**
 * Add a path entry to the matrix.
 * 
 * @param saved_hash_port First port hash value (0 to k/2-1)
 * @param saved_hash_port_l2 Second port hash value (0 to k/2-1)
 * @param time_step Time step index (0 to time_steps-1)
 * @param value Value to store at this position (0 or 1)
 */
void SnsFlow::add_path_to_matrix(int saved_hash_port, int saved_hash_port_l2, int time_step, int value) {
    // Get the index for this port pair
    int pair_index = get_port_pair_index(saved_hash_port, saved_hash_port_l2);
    
    // Check if index and time step are valid
    if (pair_index < 0 || pair_index >= (int)hist_path_matrix.size() ||
        time_step < 0 || time_step >= time_steps) {
        return; // Error: invalid index or time step
    }
    
    // If value is 1, set the matrix value to 1 (not increment)
    if (value > 0) {
        hist_path_matrix[pair_index][time_step] = 1;
        if (params.debug_sns) {
            std::cout << "Set path (" << saved_hash_port << "," << saved_hash_port_l2 
                      << ") at time step " << time_step << " to 1" << std::endl;
        }
    } else {
        // For value 0, just set it (this is typically not used but included for completeness)
        hist_path_matrix[pair_index][time_step] = value;
    }
    
    // Print the matrix after updating it
    // if (params.debug_sns && value > 0) {
    //     print_path_matrix();
    // }
}

/**
 * Get a path entry from the matrix.
 * 
 * @param saved_hash_port First port hash value (0 to k/2-1)
 * @param saved_hash_port_l2 Second port hash value (0 to k/2-1)
 * @param time_step Time step index (0 to time_steps-1)
 * @return The value at this position (0 or 1), or -1 if invalid
 */
int SnsFlow::get_path_from_matrix(int saved_hash_port, int saved_hash_port_l2, int time_step) {
    // Get the index for this port pair
    int pair_index = get_port_pair_index(saved_hash_port, saved_hash_port_l2);
    
    // Check if index and time step are valid
    if (pair_index < 0 || pair_index >= (int)hist_path_matrix.size() ||
        time_step < 0 || time_step >= time_steps) {
        return -1; // Error: invalid index or time step
    }
    
    // Return the value from the matrix
    return hist_path_matrix[pair_index][time_step];
}

/**
 * Sample a port pair based on historical usage using softmax probabilities.
 * 
 * @param temperature Temperature parameter for softmax (higher = more uniform distribution)
 * @return A pair containing the sampled saved_hash_port and saved_hash_port_l2 values
 */
std::pair<int, int> SnsFlow::sample_port_pair_from_history(double temperature) {
    // Enhanced validation - check if matrix is initialized and has valid dimensions
    if (k_value <= 0 || hist_path_matrix.empty() || time_steps <= 0) {
        if (params.debug_sns)
            std::cout << "Warning: Path matrix not properly initialized (k=" << k_value 
                      << ", matrix_size=" << hist_path_matrix.size() 
                      << ", time_steps=" << time_steps << "), using default port pair (0,0)" << std::endl;
        return std::make_pair(0, 0);
    }
    
    // Enhanced validation for k_value
    int port_count = k_value / 2;
    if (port_count <= 0) {
        if (params.debug_sns)
            std::cout << "Warning: Invalid k_value " << k_value << ", using default port pair (0,0)" << std::endl;
        return std::make_pair(0, 0);
    }
    
    int total_pairs = port_count * port_count;
    
    // Validate matrix dimensions
    if ((int)hist_path_matrix.size() < total_pairs) {
        if (params.debug_sns)
            std::cout << "Warning: Path matrix size (" << hist_path_matrix.size() 
                      << ") smaller than expected (" << total_pairs 
                      << "), using default port pair (0,0)" << std::endl;
        return std::make_pair(0, 0);
    }
    
    // Calculate scores for each port pair by summing over all time steps
    std::vector<double> scores(total_pairs, 1.0);
    
    // Print the matrix before sampling
    // print_path_matrix();
    
    for (int pair_idx = 0; pair_idx < total_pairs; pair_idx++) {
        // Validate time_steps dimension for this row
        if ((int)hist_path_matrix[pair_idx].size() < time_steps) {
            if (params.debug_sns)
                std::cout << "Warning: Path matrix row " << pair_idx << " has invalid size (" 
                          << hist_path_matrix[pair_idx].size() << " vs expected " << time_steps 
                          << "), using default port pair (0,0)" << std::endl;
            return std::make_pair(0, 0);
        }
        
        for (int t = 0; t < time_steps; t++) {
            scores[pair_idx] += hist_path_matrix[pair_idx][t];
        }
    }
    
    // Check if we have any history data or not
    bool all_zeros = true;
    for (int i = 0; i < total_pairs; i++) {
        if (scores[i] > 0) {
            all_zeros = false;
            break;
        }
    }
    
    if (all_zeros) {
        // If no history data, just use port pair (0, 0) for safety
        if (params.debug_sns)
            std::cout << "No history data available yet, using default port pair (0,0)" << std::endl;
        return std::make_pair(0, 0);
    }
    
    // Find max score for numerical stability
    double max_score = *std::max_element(scores.begin(), scores.end());
    
    // Apply softmax to convert scores to probabilities
    std::vector<double> probabilities(total_pairs, 0.0);
    
    // Calculate softmax: exp(score/temperature) / sum(exp(score/temperature))
    double sum_exp = 0.0;
    for (int i = 0; i < total_pairs; i++) {
        // Use temperature to control exploration vs. exploitation
        probabilities[i] = std::exp((scores[i] - max_score) / temperature);
        sum_exp += probabilities[i];
    }
    
    // Normalize to get probabilities
    if (sum_exp > 0) {
        for (int i = 0; i < total_pairs; i++) {
            probabilities[i] /= sum_exp;
        }
    } else {
        // If all scores are 0, use uniform distribution
        for (int i = 0; i < total_pairs; i++) {
            probabilities[i] = 1.0 / total_pairs;
        }
    }
    
    // Sample a port pair based on the calculated probabilities
    double random_value = (double)rand() / RAND_MAX;
    double cumulative_prob = 0.0;
    
    for (int i = 0; i < total_pairs; i++) {
        cumulative_prob += probabilities[i];
        if (random_value <= cumulative_prob) {
            // Convert pair index back to port values
            int saved_hash_port = i / port_count;
            int saved_hash_port_l2 = i % port_count;
            
            // Double-check validation of the values before returning
            if (saved_hash_port >= 0 && saved_hash_port < port_count && 
                saved_hash_port_l2 >= 0 && saved_hash_port_l2 < port_count) {
                return std::make_pair(saved_hash_port, saved_hash_port_l2);
            } else {
                if (params.debug_sns)
                    std::cout << "Warning: Invalid sampled port pair (" << saved_hash_port << "," 
                              << saved_hash_port_l2 << "), using default (0,0)" << std::endl;
                return std::make_pair(0, 0);
            }
        }
    }
    
    // Fallback (should not reach here unless there's numerical precision issues)
    if (params.debug_sns)
        std::cout << "Warning: Fallback in sampling, using default port pair (0,0)" << std::endl;
    return std::make_pair(0, 0);
}

/**
 * Print the current state of the path history matrix for debugging.
 */
void SnsFlow::print_path_matrix() {
    if (params.debug_sns) {
        std::cout << "=== Path Matrix for Flow " << this->id << " ===" << std::endl;
        
        if (hist_path_matrix.empty()) {
            std::cout << "  Matrix is empty or not initialized!" << std::endl;
            return;
        }
        
        int port_count = k_value / 2;
        if (port_count <= 0) {
            std::cout << "  Invalid port_count: " << port_count << std::endl;
            return;
        }
        
        std::cout << "  Matrix dimensions: " << hist_path_matrix.size() << "x" 
                  << (hist_path_matrix.empty() ? 0 : hist_path_matrix[0].size()) << std::endl;
        std::cout << "  Current time step: " << current_time_step << "/" << time_steps << std::endl;
        std::cout << "  Port count: " << port_count << " (k=" << k_value << ")" << std::endl;
        
        // Print header
        std::cout << "  Port Pair | ";
        for (int t = 0; t < time_steps; t++) {
            std::cout << "T" << t << " ";
        }
        std::cout << "| Sum" << std::endl;
        
        // Print matrix data
        for (int i = 0; i < (int)hist_path_matrix.size(); i++) {
            int p1 = i / port_count;
            int p2 = i % port_count;
            std::cout << "  (" << p1 << "," << p2 << ")     | ";
            
            int row_sum = 1;
            for (int t = 0; t < (int)hist_path_matrix[i].size(); t++) {
                std::cout << hist_path_matrix[i][t] << " ";
                row_sum += hist_path_matrix[i][t];
            }
            std::cout << "| " << row_sum << std::endl;
        }
        std::cout << "===============================" << std::endl;
    }
}

/**
 * Reset all port pairs for the current time step to 0.
 */
void SnsFlow::reset_current_time_step() {
    if (k_value <= 0 || hist_path_matrix.empty() || current_time_step < 0 || current_time_step >= time_steps) {
        return; // Invalid state, nothing to reset
    }
    
    int port_count = k_value / 2;
    if (port_count <= 0) {
        return; // Invalid port count
    }
    
    int total_pairs = port_count * port_count;
    
    // Reset all pairs for the current time step to 0
    for (int i = 0; i < total_pairs && i < (int)hist_path_matrix.size(); i++) {
        if (current_time_step < (int)hist_path_matrix[i].size()) {
            hist_path_matrix[i][current_time_step] = 0;
        }
    }
    
    if (params.debug_sns) {
        std::cout << "Reset all port pairs for time step " << current_time_step << " to 0" << std::endl;
    }
}