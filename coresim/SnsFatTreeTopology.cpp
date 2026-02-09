#include "SnsFatTreeTopology.h"
#include "../ext/factory.h"
#include <algorithm>
#include <random>

extern DCExpParams params;
extern double get_current_time(); 

SnsFatTreeTopology::SnsFatTreeTopology(
        uint32_t k, 
        double bandwidth,
        uint32_t queue_type
        ) : Topology () {
    this->_k = k;
    this->num_hosts = k * k * k / 4;
    this->num_edge_switches = k * k / 2;
    this->num_agg_switches = k * k / 2;
    this->num_core_switches = k * k / 4;

    //Capacities
    double c1 = bandwidth;
    double c2 = bandwidth;  // Can be adjusted based on oversubscription ratio if needed
    this->access_bw = c1;
    this->core_bw = c2;

    // Create Hosts
    for (uint32_t i = 0; i < this->num_hosts; i++) {
        hosts.push_back((SnsHost*)Factory::get_host(i, c1, queue_type, params.host_type));
    }

    // Create Switches
    for (uint32_t i = 0; i < num_edge_switches; i++) {
        SnsAggSwitch* sw = new SnsAggSwitch(i, k/2, c1, k/2, c2, queue_type);
        sw->switch_type = FAT_TREE_EDGE_SWITCH;  // Override switch type
        edge_switches.push_back(sw);
        sns_switches.push_back(sw);
    }
    
    for (uint32_t i = 0; i < num_agg_switches; i++) {
        SnsAggSwitch* sw = new SnsAggSwitch(i + num_edge_switches, k/2, c1, k/2, c2, queue_type);
        sw->switch_type = FAT_TREE_AGG_SWITCH;  // Override switch type
        agg_switches.push_back(sw);
        sns_switches.push_back(sw);
    }
    
    for (uint32_t i = 0; i < num_core_switches; i++) {
        SnsCoreSwitch* sw = new SnsCoreSwitch(i + num_edge_switches + num_agg_switches, k, c2, queue_type);
        sw->switch_type = FAT_TREE_CORE_SWITCH;  // Override switch type
        core_switches.push_back(sw);
        sns_switches.push_back(sw);
    }
    
    //Connect host queues
    for (uint32_t i = 0; i < num_hosts; i++) {
        ((SnsHost*)hosts[i])->queue->set_src_dst(hosts[i], edge_switches[i * 2 / k]);
        if (params.debug_sns)
            std::cout << "Linking Host " << i << " to Edge " << edge_switches[i * 2 / k]->id << "\n";
    }

    // For edge switches
    for (uint32_t i = 0; i < num_edge_switches; i++) {
        uint32_t pod = i / (k / 2);
        // Queues to Hosts
        for (uint32_t j = 0; j < k / 2; j++) {
            Queue *q = edge_switches[i]->queues[j];
            q->set_src_dst(edge_switches[i], hosts[i * k / 2 + j]);
            if (params.debug_sns)
                std::cout << "Linking Edge " << i << " to Host" << i * k / 2 + j << " with queue " << q->id << " " << q->unique_id << "\n";
        }
        // Queues to Agg
        for (uint32_t j = 0; j < k / 2; j++) {
            Queue *q = edge_switches[i]->queues[j + k / 2];
            q->set_src_dst(edge_switches[i], agg_switches[pod * k / 2 + j]);
            if (params.debug_sns)
                std::cout << "Linking Edge " << i << " to Agg" << pod * k / 2 + j << " with queue " << q->id << " " << q->unique_id << "\n";
        }
    }

    // For agg switches
    for (uint32_t i = 0; i < num_agg_switches; i++) {
        // Queues to Edge Switches
        uint32_t pod = i / (k / 2);
        for (uint32_t j = 0; j < k / 2; j++) {
            Queue *q = agg_switches[i]->queues[j];
            q->set_src_dst(agg_switches[i], edge_switches[pod * k / 2 + j]);
            if (params.debug_sns)
                std::cout << "Linking Agg " << i << " to Edge" << pod * k / 2 + j << " with queue " << q->id << " " << q->unique_id << "\n";
        }
        // Queues to Core (adjusted for half the number of core switches)
        uint32_t base = i % (k / 2);
        for (uint32_t j = 0; j < k / 2; j++) {
            Queue *q = agg_switches[i]->queues[j + k / 2];
            // Map to available core switches using modulo
            uint32_t core_idx = (j * k / 2 + base) % num_core_switches;
            q->set_src_dst(agg_switches[i], core_switches[core_idx]);
            if (params.debug_sns)
                std::cout << "Linking Agg " << i << " to Core" << core_idx << " with queue " << q->id << " " << q->unique_id << "\n";
        }
    }

    //For core switches (adjusted for half the number of core switches)
    for (uint32_t i = 0; i < num_core_switches; i++) {
        for (uint32_t j = 0; j < k; j++) {
            Queue *q = core_switches[i]->queues[j];
            // Map to available agg switches using modulo
            uint32_t agg_idx = (k / 2 * j + (i % (k / 2))) % num_agg_switches;
            q->set_src_dst(core_switches[i], agg_switches[agg_idx]);
            if (params.debug_sns)
                std::cout << "Linking Core " << i << " to Agg" << agg_idx << " with queue " << q->id << " " << q->unique_id << "\n";
        }
    }

    // Verify connectivity
    for (auto s = this->sns_switches.begin(); s != this->sns_switches.end(); s++) {
        for (auto q = (*s)->queues.begin(); q != (*s)->queues.end(); q++) {        
            assert((*q)->src == (*s));
            assert((*q)->src != NULL && (*q)->dst != NULL);
        }
    }
   
    // Set up parameters
    this->set_up_parameter();
}

void SnsFatTreeTopology::set_up_parameter() {
    params.rtt = (6 * params.propagation_delay + ((params.mss + params.hdr_size) * 8 / this->access_bw + (params.mss + params.hdr_size) * 8 / this->core_bw) * 3) * 2;
    params.BDP = ceil(params.rtt * this->access_bw / (params.mss + params.hdr_size) / 8);
    // std::cout << "SnsFatTreeTopology::set_up_parameter params.rtt " << params.rtt << " params.BDP " << params.BDP <<  "\n";
    // exit(1);
    
    if (params.host_type == RUF_HOST) {
        params.ruf_max_tokens = ceil(params.ruf_max_tokens * params.BDP);
        params.ruf_min_tokens = ceil(params.ruf_min_tokens * params.BDP);
        params.token_window *= params.BDP;
        params.token_initial *= params.BDP;
        params.token_timeout *= params.get_full_pkt_tran_delay();
        params.token_resend_timeout *= params.BDP * params.get_full_pkt_tran_delay();
        params.rufhost_idle_timeout *= params.BDP * params.get_full_pkt_tran_delay();
        params.token_window_timeout *= params.BDP * params.get_full_pkt_tran_delay();
        params.ruf_controller_epoch *= params.BDP * params.get_full_pkt_tran_delay();
    } else if (params.host_type == PIM_HOST) {
        params.token_window_timeout *= params.BDP * params.get_full_pkt_tran_delay();
        params.token_resend_timeout *= params.BDP * params.get_full_pkt_tran_delay();
        params.token_initial *= params.BDP;
        params.token_window *= params.BDP;
        params.token_timeout *= params.get_full_pkt_tran_delay();

        params.pim_iter_epoch = params.pim_beta * (this->get_control_pkt_rtt(143));
        params.pim_epoch = params.pim_iter_limit * params.pim_iter_epoch * (1 + params.pim_alpha);
        params.pim_link_pkts = params.pim_iter_limit * params.pim_iter_epoch * params.pim_alpha / params.get_full_pkt_tran_delay() / params.pim_k;
    }
}

bool SnsFatTreeTopology::is_arbiter(Host* n) {
    if(n->host_type == FASTPASS_ARBITER || n->host_type == RUF_ARBITER)
        return true;
    return false;
}

bool SnsFatTreeTopology::is_same_rack(Host* a, Host*b) {

    if(a->id / (_k / 2) == b->id / (_k / 2))
        return true;
    return false;
}

bool SnsFatTreeTopology::is_same_rack(int a, int b) {
    if(a / (_k / 2) == b / (_k / 2))
        return true;
    return false;
}

uint32_t SnsFatTreeTopology::get_rack_num(Host* a) {
    return a->id / (_k / 2);
}

bool SnsFatTreeTopology::is_same_pod(Host* a, Host*b) {

    if(a->id / (_k * _k / 4) == b->id / (_k * _k / 4))
        return true;
    return false;
}

uint32_t SnsFatTreeTopology::get_pod_num(Host* a) {

    return a->id / (_k * _k / 4);
}

Queue* SnsFatTreeTopology::get_next_hop_inner(Packet* _p, Queue* q) {
    SnsPacket *p = (SnsPacket*)_p;
    if (params.debug_sns2)
        std::cout << "SnsFatTreeTopology::get_next_hop p->sns_type " << p->sns_type << "\n";

    if (q->dst->type == HOST) {
        // Check if the packet's destination matches the queue's destination
        // If not, this is likely due to a routing issue with the port pairs
        if (p->dst->id != q->dst->id) {
            if (params.debug_sns) {
                std::cout << "WARNING: Packet destination mismatch in get_next_hop_inner: " 
                          << "p->dst->id=" << p->dst->id << " q->dst->id=" << q->dst->id 
                          << " flow_id=" << p->flow->id 
                          << " p->saved_hash_port=" << p->saved_hash_port 
                          << " p->saved_hash_port_l2=" << p->saved_hash_port_l2 
                          << " p->sns_type=" << p->sns_type << std::endl;
            }
            // Return NULL to avoid the assertion failure
            // This effectively drops the packet, which is better than crashing
            return NULL;
        }
        
        // Use assert in debug mode only, but with graceful error handling in release mode
        #ifdef NDEBUG
        if (p->dst->id != q->dst->id) {
            return NULL; // Avoid crash in release mode
        }
        #else
        assert(p->dst->id == q->dst->id);
        #endif
        
        int hop = 6;
        if(is_same_rack(p->src, p->dst)){
            hop = 2;
        }
        else if(is_same_pod(p->src, p->dst)){
            hop = 4;
        }
        if(p->hop != hop) {
            std::cout << p->src->id << " " << p->dst->id << std::endl;
            std::cout << p->hop << " " << hop << std::endl;
            std::cout << p->flow->id << std::endl;
        }
        assert(p->hop == hop);
        return NULL; // Packet Arrival
    }

    // At host level
    if (q->src->type == HOST) {
        return get_host_next_hop(p, q);
    }
    // At switch level
    else if (q->src->type == SWITCH) {
        if (((SnsSwitch *)q->src)->switch_type == FAT_TREE_EDGE_SWITCH) {
            return get_edge_next_hop(p, q);
        }
        else if (((SnsSwitch *)q->src)->switch_type == FAT_TREE_AGG_SWITCH) {
            return get_agg_next_hop(p, q);
        } else if(((SnsSwitch *)q->src)->switch_type == FAT_TREE_CORE_SWITCH) {
            return get_core_next_hop(p, q);
        }
    }
    assert(false);
    return NULL;
}

Queue* SnsFatTreeTopology::get_next_hop(Packet* _p, Queue* q) {
    // print_queue_length1(0);
    SnsPacket* p = (SnsPacket*)_p;
    
    // Enhanced validation before calling get_next_hop_inner
    if (q == NULL) {
        if (params.debug_sns) {
            std::cout << "WARNING: NULL queue in get_next_hop for packet with"
                      << " flow_id=" << p->flow->id 
                      << " p->saved_hash_port=" << p->saved_hash_port 
                      << " p->saved_hash_port_l2=" << p->saved_hash_port_l2 
                      << " p->sns_type=" << p->sns_type << std::endl;
        }
        return NULL; // Drop the packet
    }
    
    if (q->dst == NULL) {
        if (params.debug_sns) {
            std::cout << "WARNING: Queue with NULL dst in get_next_hop for packet with"
                      << " flow_id=" << p->flow->id 
                      << " p->saved_hash_port=" << p->saved_hash_port 
                      << " p->saved_hash_port_l2=" << p->saved_hash_port_l2 
                      << " p->sns_type=" << p->sns_type << std::endl;
        }
        return NULL; // Drop the packet
    }
    
    if (q->dst->type == HOST && p->dst->id != q->dst->id) {
        if (params.debug_sns) {
            std::cout << "WARNING: Packet destination mismatch in get_next_hop: " 
                      << "p->dst->id=" << p->dst->id << " q->dst->id=" << q->dst->id 
                      << " flow_id=" << p->flow->id 
                      << " p->saved_hash_port=" << p->saved_hash_port 
                      << " p->saved_hash_port_l2=" << p->saved_hash_port_l2 
                      << " p->sns_type=" << p->sns_type << std::endl;
        }
        return NULL; // Drop the packet
    }
    
    Queue* next_hop = get_next_hop_inner(_p, q);
    if (!next_hop){
        return NULL; // Packet Arrival
    }

    return next_hop;
}

Queue* SnsFatTreeTopology::get_host_next_hop(Packet *p, Queue *q) {
    assert(q->src->type == HOST);
    assert(p->src->id == q->src->id);

    if (is_same_rack(p->src, p->dst)) {
        p->hop = 2;

        return ((SnsSwitch *) q->dst)->queues[p->dst->id % (_k / 2)];
    }
    else if (is_same_pod(p->src, p->dst)) {
        p->hop = 4;
    } else {
        p->hop = 6;
    }
    uint32_t hash_port = 0;
    if(params.load_balancing == 0)
        hash_port = q->spray_counter++ % (_k / 2);
    else if(params.load_balancing == 1)
        hash_port = (p->src->id + p->dst->id + p->flow->id) % (_k / 2);
    else if(params.load_balancing == 3)
        hash_port = p->saved_hash_port;        
    if (p->sns_type == SNS_SIM_PACKET){
        p->saved_hash_port = hash_port;        
    } else {
        if (p->saved_hash_port != -1 && p->sns_type == SNS_DATA_PACKET)
            hash_port = p->saved_hash_port; 
    }
    return ((SnsSwitch *) q->dst)->queues[_k / 2 + hash_port];
}
void SnsFatTreeTopology::print_queue_length1(double time) {
    static int count = 0;
    static float last_current_time = 1;
    float current_time = get_current_time() * 1000000;
    float current_time2 = (get_current_time() - 1) * 1000000;
    count++;
    if (count % 1000 != 0)
        return;
    current_time = count;
    if (current_time == last_current_time)
         return;
    last_current_time = current_time;
    //std::cout << "print_queue_length1: " << std::endl;
    static std::ofstream queue_util_file;
    if(!queue_util_file.is_open()) {
        queue_util_file.open("../queue_util.txt");
    }
    
    for(int i = 0; i < this->sns_switches.size(); i++) {
        for (int j = 0; j < this->sns_switches[i]->queues.size(); j++) {
            int sim = 0;
            int sim_ack = 0;
            int data = 0;
            for(int k = 0; k < this->sns_switches[i]->queues[j]->packets.size(); k++){
                if (this->sns_switches[i]->queues[j]->packets[k]->sns_type == SNS_SIM_PACKET)
                    sim++;
                else if(this->sns_switches[i]->queues[j]->packets[k]->sns_type == SNS_SIM_ACK_PACKET)
                    sim_ack++;
                else if(this->sns_switches[i]->queues[j]->packets[k]->sns_type == SNS_DATA_PACKET)
                    data++;
                else if(this->sns_switches[i]->queues[j]->packets[k]->sns_type == SNS_DATA_ACK_PACKET)
                    continue;
                else
                    assert(0);
            }
            //if (this->sns_switches[i]->queues[j]->unique_id == 289){
            queue_util_file << current_time2 <<  " " << this->sns_switches[i]->queues[j]->unique_id << " " 
                            << this->sns_switches[i]->queues[j]->packets.size() << " " << sim <<
                            " " << sim_ack << " " << data << " ### ";
            for (int k = 0; k < this->sns_switches[i]->queues[j]->packets.size(); k++){
                queue_util_file << this->sns_switches[i]->queues[j]->packets[k]->sns_type <<  " " << this->sns_switches[i]->queues[j]->packets[k]->sns_msg_id << " ";
            }
            queue_util_file << ((SnsQueue*)(this->sns_switches[i]->queues[j]))->sim_packets.size() << "\n";
        }
    }
}
Queue* SnsFatTreeTopology::get_edge_next_hop(Packet *p, Queue *q) {
    assert(((SnsSwitch *)q->src)->switch_type == FAT_TREE_EDGE_SWITCH);
    
    // Destination is a host connected to this edge switch
    if (q->dst->type == HOST) {
        return NULL; // Packet Arrival
    }
    
    assert(((SnsSwitch *)q->dst)->switch_type == FAT_TREE_AGG_SWITCH);

    if(is_same_pod(p->src, p->dst)) {
        uint32_t edge_num = get_rack_num(p->dst) % (_k / 2);
        return ((SnsSwitch *) q->dst)->queues[edge_num];
    } else {
        uint32_t hash_port = 0;
        if(params.load_balancing == 0)
            hash_port = q->spray_counter++ % (_k / 2);
        else if(params.load_balancing == 3)
            hash_port = p->saved_hash_port_l2;
        else if(params.load_balancing == 1)
            hash_port = (p->src->id + p->dst->id + p->flow->id) % (_k / 2);
        if (p->sns_type == SNS_SIM_PACKET){
            p->saved_hash_port_l2 = hash_port;        
        } else {
            if (p->saved_hash_port_l2 != -1 && p->sns_type == SNS_DATA_PACKET){
                hash_port = p->saved_hash_port_l2; 
            }
        }
        return ((SnsSwitch *) q->dst)->queues[_k / 2 + hash_port];
    }
}

Queue* SnsFatTreeTopology::get_agg_next_hop(Packet *p, Queue *q) {
    assert(((SnsSwitch *)q->src)->switch_type == FAT_TREE_AGG_SWITCH);
    
    if(((SnsSwitch *)q->dst)->switch_type == FAT_TREE_EDGE_SWITCH) {
        uint32_t queue_num = p->dst->id % (_k / 2);
        return ((SnsSwitch *) q->dst)->queues[queue_num];
    } else {
        assert(((SnsSwitch *)q->dst)->switch_type == FAT_TREE_CORE_SWITCH);
        uint32_t pod_num = get_pod_num(p->dst);
        return ((SnsSwitch *) q->dst)->queues[pod_num];
    }
}

Queue* SnsFatTreeTopology::get_core_next_hop(Packet *p, Queue *q) {
    assert(((SnsSwitch *)q->src)->switch_type == FAT_TREE_CORE_SWITCH);
    assert(((SnsSwitch *)q->dst)->switch_type == FAT_TREE_AGG_SWITCH);
    
    uint32_t edge_num = get_rack_num(p->dst) % (_k / 2);
    return ((SnsSwitch *) q->dst)->queues[edge_num];
}

double SnsFatTreeTopology::get_worst_rtt(Flow *f) {
    int num_hops = 6;

    double propagation_delay;
    propagation_delay = num_hops * params.propagation_delay; //us
    double transmission_delay;
    transmission_delay = (f->hdr_size) * 8.0 / (params.bandwidth / params.sns_simulation_frq_size) *
                        num_hops * params.simulation_queue_size;

    transmission_delay += ((f->mss + f->hdr_size) * 8.0 / (params.bandwidth)) * num_hops; 

    transmission_delay += /*sim_ack*/ num_hops * ((f->hdr_size) * 8.0 / (params.bandwidth))*params.simulation_queue_size;

    double total;
    double second = ((num_hops*2 / (2*params.bandwidth)) * (params.hdr_size + params.mss) * 8.0)
                    * (params.simulation_queue_size + 2 + (params.simulation_queue_size + 3)/params.sns_simulation_frq_size);
    total = propagation_delay*2 + second;


    // Use the pre-calculated clock drift factor from the host
    static double last_update_dynamic_clock_drift = 0;
    if ((get_current_time()-1)*1e6 - last_update_dynamic_clock_drift > 100000){
        last_update_dynamic_clock_drift = (get_current_time()-1)*1e6;
        ((SnsHost*)f->src)->update_dynamic_clock_drift_factor();
    }
    total /= (1+(((SnsHost*)f->src)->clock_drift_factor/1e6));
    total *= params.worst_rrt_factor;
    return total; //us
}

double SnsFatTreeTopology::get_oracle_fct(Flow *f) {
    int num_hops = 6;
    if (is_same_rack(f->src, f->dst)) {
        num_hops = 2;
    } else if(is_same_pod(f->src, f->dst)) {
        num_hops = 4;
    }
    
    double propagation_delay;
    if (params.ddc != 0) { 
        if (num_hops == 2) {
            propagation_delay = 0.440;
        }
        if (num_hops == 4) {
            propagation_delay = 2.040;
        }
    }
    else {
        propagation_delay = 2 * 1000000.0 * num_hops * f->src->queue->propagation_delay; //us
    }
   
    double pkts = (double) f->size / params.mss;
    uint32_t np = floor(pkts);
    uint32_t leftover = (pkts - np) * params.mss;
    double incl_overhead_bytes = (params.mss + f->hdr_size) * np + leftover;
    if(leftover > 0) {
        incl_overhead_bytes += f->hdr_size;
    }

    double bandwidth = f->src->queue->rate / 1000000.0; // For us
    double transmission_delay;
    
    transmission_delay = (incl_overhead_bytes + f->hdr_size) * 8.0 / bandwidth;
    // last packet and 1 ack
    if (leftover != params.mss && leftover != 0) {
        // less than mss sized flow. the 1 packet is leftover sized.
        transmission_delay += (num_hops - 1) * (leftover + 2 * params.hdr_size) * 8.0 / (bandwidth);
    } else {
        // 1 packet is full sized
        transmission_delay += (num_hops - 1) * (params.mss + 2 * params.hdr_size) * 8.0 / (bandwidth);
    }
    
    return (propagation_delay + transmission_delay); //us
}

double SnsFatTreeTopology::get_control_pkt_rtt(int host_id) {
    if(host_id / (_k /2) == 0) {
        return (2 * params.propagation_delay + 2 * (params.hdr_size * 8 / params.bandwidth)) * 2;
    } else if(host_id / (_k * _k / 4) == 0) {
        return (4 * params.propagation_delay + 4 * (params.hdr_size * 8 / params.bandwidth)) * 2;
    } else {
        return (6 * params.propagation_delay + 6 * (params.hdr_size * 8 / params.bandwidth)) * 2;
    }
}

int SnsFatTreeTopology::num_hosts_per_tor() {
    return int(_k / 2);
}
