#include "SnsleafSpineTopology.h"
#include "../ext/factory.h"
#include "../run/params.h"
#include <algorithm>

extern DCExpParams params;
extern double get_current_time(); 

SnsLeafSpineTopology::SnsLeafSpineTopology(
        uint32_t num_hosts,
        uint32_t num_agg_switches,
        uint32_t num_core_switches,
        double bandwidth,
        uint32_t queue_type
        ) : Topology() {

    this->hosts_per_agg_switch = num_hosts / num_agg_switches;
    this->num_hosts = num_hosts;
    this->num_agg_switches = num_agg_switches;
    this->num_core_switches = num_core_switches;
    //Capacities
    double c1 = bandwidth;
    double c2 = hosts_per_agg_switch * params.os_ratio * bandwidth / num_core_switches;
    this->access_bw = c1;
    this->core_bw = c2;
    if (params.core_div_2) {
        this->core_bw /= 2;
    }
    // Create Hosts
    for (uint32_t i = 0; i < num_hosts; i++) {
        hosts.push_back((SnsHost*)Factory::get_host(i, c1, queue_type, params.host_type));
    }

 
    // Create Switches
    for (uint32_t i = 0; i < num_agg_switches; i++) {
        SnsAggSwitch* sw = new SnsAggSwitch(i, hosts_per_agg_switch, c1, num_core_switches, c2, queue_type);
        
        agg_switches.push_back(sw); // TODO make generic
        sns_switches.push_back(sw);
    }
    for (uint32_t i = 0; i < num_core_switches; i++) {
        SnsCoreSwitch* sw = new SnsCoreSwitch(i + num_agg_switches, num_agg_switches, c2, queue_type);
        core_switches.push_back(sw);
        sns_switches.push_back(sw);
    }

    //Connect host queues2
    for (uint32_t i = 0; i < num_hosts; i++) {
        ((SnsHost*)hosts[i])->queue->set_src_dst(hosts[i], agg_switches[i/hosts_per_agg_switch]);
        if (params.debug_sns)
            std::cout << "SnsLeafSpineTopology host: " << i << " coonect into agg_sw: " << i/hosts_per_agg_switch << std::endl;
    }

    // std::cout << "Linking arbiter with queue " << arbiter->queue->id << " " << arbiter->queue->unique_id << " with agg switch " << agg_switches[0]->id << "\n" ;


    // For agg switches -- REMAINING
    for (uint32_t i = 0; i < num_agg_switches; i++) {
        // Queues to Hosts
        for (uint32_t j = 0; j < hosts_per_agg_switch; j++) { // TODO make generic
            Queue *q = agg_switches[i]->queues[j];
            q->set_src_dst(agg_switches[i], hosts[i * hosts_per_agg_switch + j]);
            if (params.debug_sns) {
                std::cout << "SnsLeafSpineTopology SW: " << i << " port: " << j << " connect to host: " << i * hosts_per_agg_switch + j << std::endl;
                std::cout << "Linking Agg " << i << " to Host" << i * 16 + j << " with queue " << q->id << " " << q->unique_id << "\n";
            }
        }
        // Queues to Core
        uint32_t j = 0;
        uint32_t loop_until = num_core_switches;
        for (; j < loop_until; j++) {
            Queue *q = agg_switches[i]->queues[j + hosts_per_agg_switch];
            q->set_src_dst(agg_switches[i], core_switches[j]);
            if (params.debug_sns)
                std::cout << "Linking Agg " << i << " to Core" << j << " with queue " << q->id << " " << q->unique_id << "\n";
        }
    }
    // std::cout << "Linking arbiter switch " << agg_switches[0]->id << " with queue " << ((RufAggSwitch*) agg_switches[0])->queue_to_arbiter->id << " " << ((RufAggSwitch*) agg_switches[0])->queue_to_arbiter->unique_id << "\n";

    //For core switches -- PERFECT
    for (uint32_t i = 0; i < num_core_switches; i++) {
        for (uint32_t j = 0; j < num_agg_switches; j++) {
            Queue *q = core_switches[i]->queues[j];
            q->set_src_dst(core_switches[i], agg_switches[j]);
            if (params.debug_sns)
                std::cout << "Linking Core " << i << " to Agg" << j << " with queue " << q->id << " " << q->unique_id << "\n";
        }
    }

    for (auto s = this->sns_switches.begin(); s != this->sns_switches.end(); s++) {
        for (auto q = (*s)->queues.begin(); q != (*s)->queues.end(); q++) {

            assert((*q)->src == (*s));
            assert((*q)->src != NULL && (*q)->dst != NULL);
        }
    }
    // for (auto s = this->sns_switches.begin(); s != this->sns_switches.end(); s++) {
    //     for (auto q = (*s)->queues_simulation.begin(); q != (*s)->queues_simulation.end(); q++) {
    //         assert((*q)->src == (*s));
    //         assert((*q)->src != NULL && (*q)->dst != NULL);
    //     }
    // }
    // for (auto s = this->sns_switches.begin(); s != this->sns_switches.end(); s++) {
    //     for (auto q = (*s)->queues_simulation_ack.begin(); q != (*s)->queues_simulation_ack.end(); q++) {
    //         assert((*q)->src == (*s));
    //         assert((*q)->src != NULL && (*q)->dst != NULL);
    //     }
    // }

   
    // set up parameter
    this->set_up_parameter();
}

void SnsLeafSpineTopology::set_up_parameter() {
    params.rtt = (4 * params.propagation_delay + ((params.mss + params.hdr_size) * 8 / this->access_bw +  (params.mss + params.hdr_size) * 8 / this->core_bw) * 2) * 2;
    // params.ctrl_pkt_rtt = (4 * params.propagation_delay + (40 * 8 / params.bandwidth) * 2.5) * 2;
    params.BDP = ceil(params.rtt * this->access_bw / (params.mss + params.hdr_size) / 8);
    if (params.host_type == RUF_HOST) {
        params.ruf_max_tokens = ceil(params.ruf_max_tokens * params.BDP);
        params.ruf_min_tokens = ceil(params.ruf_min_tokens * params.BDP);
        params.token_window *= params.BDP;
        params.token_initial *= params.BDP;
        params.token_timeout *= params.get_full_pkt_tran_delay();
        params.token_resend_timeout *= params.BDP * params.get_full_pkt_tran_delay();
        params.rufhost_idle_timeout *= params.BDP * params.get_full_pkt_tran_delay();
        params.token_window_timeout *= params.BDP * params.get_full_pkt_tran_delay();
        // params.ruf_reset_epoch *= params.BDP * params.get_full_pkt_tran_delay();
        params.ruf_controller_epoch *= params.BDP * params.get_full_pkt_tran_delay();
    } else if (params.host_type == PIM_HOST) {
        params.token_window_timeout *= params.BDP * params.get_full_pkt_tran_delay();
        params.token_resend_timeout *= params.BDP * params.get_full_pkt_tran_delay();
        params.token_initial *= params.BDP;
        params.token_window *= params.BDP;
        params.token_timeout *= params.get_full_pkt_tran_delay();

        // params.pim_epoch *= params.BDP * params.get_full_pkt_tran_delay();
        params.pim_iter_epoch = params.pim_beta * (this->get_control_pkt_rtt(143));
        params.pim_epoch = params.pim_iter_limit * params.pim_iter_epoch * (1 + params.pim_alpha);
        params.pim_link_pkts = params.pim_iter_limit * params.pim_iter_epoch * params.pim_alpha /  params.get_full_pkt_tran_delay() / params.pim_k;
        // std::cout << "pim iter epoch:" << params.pim_iter_epoch << std::endl;
        // std::cout << "pim epoch:" << params.pim_epoch << std::endl;

        // std::cout << "params.pim_iter_epoc:" << params.pim_iter_epoch << std::endl;
        // std::cout << "params.pim_epoch:" << params.pim_epoch << std::endl;
        // std::cout << " params.token_window_timeout" << params.token_window_timeout << std::endl;

    }
}


bool SnsLeafSpineTopology::is_same_rack(Host* a, Host*b) {
	if(a->id / this->hosts_per_agg_switch == b->id / this->hosts_per_agg_switch)
		return true;
	return false;
}

bool SnsLeafSpineTopology::is_same_rack(int a, int b) {
    // assume arbiter is at pod 0 and edge switch 0;
    if(a / this->hosts_per_agg_switch == b / this->hosts_per_agg_switch)
        return true;
    return false;
}
Queue* SnsLeafSpineTopology::get_next_hop_inner(Packet* _p, Queue* q) {
    SnsPacket *p = (SnsPacket*)_p;
    if (params.debug_sns2)
        std::cout << "SnsLeafSpineTopology::get_next_hop p->sns_type " << p->sns_type << "\n";

    if (q->dst->type == HOST) {
        assert(p->dst->id == q->dst->id);
        return NULL; // Packet Arrival
    }
    if (params.debug_sns2)
        std::cout << "SnsLeafSpineTopology::get_next_hop q->src->type " << q->src->type << " q->dst->type " << q->dst->type << "\n";
    // At host level
    if (q->src->type == HOST) { // Same Rack or not
        assert (p->src->id == q->src->id);

        if (this->is_same_rack(p->src, p->dst)) {
            return ((SnsSwitch *) q->dst)->queues[p->dst->id % this->hosts_per_agg_switch];
        } 
        else {
            uint32_t hash_port = 0;
            if(params.load_balancing == 0)
                hash_port = q->spray_counter++ % this->num_core_switches ;
            else if(params.load_balancing == 1)
                hash_port = (p->src->id + p->dst->id + p->flow->id) % this->num_core_switches;
            if (p->sns_type == SNS_SIM_PACKET){
                p->saved_hash_port = hash_port;
                return ((SnsSwitch *) q->dst)->queues[this->hosts_per_agg_switch + hash_port];
            } else {
                if (p->saved_hash_port != -1 && p->sns_type != SNS_SIM_ACK_PACKET)
                    hash_port = p->saved_hash_port; // sns: is the random occur only in the first level?
                return ((SnsSwitch *) q->dst)->queues[this->hosts_per_agg_switch + hash_port];
            }
        }
    }

    // At switch level
    if (q->src->type == SWITCH) {
        //std::cout << " ((SnsSwitch *) q->src)->switch_type " << ((SnsSwitch *) q->src)->switch_type << std::endl;

        if (((SnsSwitch *) q->src)->switch_type == AGG_SWITCH) {
            return ((SnsSwitch *) q->dst)->queues[p->dst->id / this->hosts_per_agg_switch];
        }
        if (((SnsSwitch *) q->src)->switch_type == CORE_SWITCH) {
            return ((SnsSwitch *) q->dst)->queues[p->dst->id % this->hosts_per_agg_switch];
        }
    }
    if (params.debug_sns)
        std::cout << " q->src->type" << q->src->type << std::endl;

    assert(false);
    return NULL;
}

Queue* SnsLeafSpineTopology::get_next_hop(Packet* _p, Queue* q) {
    if (get_next_hop_inner(_p, q) == NULL)
        return NULL;
    Queue* p = get_next_hop_inner(_p, q);
    
    return p;
}
void SnsLeafSpineTopology::print_queue_length1(double time) {
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
    
	std::map<int, std::vector<uint64_t>> total_queue_length;
	std::map<int, uint32_t> max_queue_length;
	for (int i = 0; i < 6; i++) {
		total_queue_length[i] = std::vector<uint64_t>();
		max_queue_length[i] = 0;
	}
	for (int i = 0; i < this->num_hosts; i++) {
		int location = this->hosts[i]->queue->location;
        total_queue_length[location].push_back(this->hosts[i]->queue->total_bytes_in_queue);
        max_queue_length[location] = std::max(this->hosts[i]->queue->max_bytes_in_queue, max_queue_length[location]);
    }
    for(int i = 0; i < this->sns_switches.size(); i++) {
        for (int j = 0; j < this->sns_switches[i]->queues.size(); j++) {
            int sim = 0;
            int sim_ack = 0;
            int data = 0;
            for (int k = 0; k < this->sns_switches[i]->queues[j]->packets.size(); k++){
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
            queue_util_file << current_time2 <<  " " << this->sns_switches[i]->queues[j]->unique_id << " " 
                            << this->sns_switches[i]->queues[j]->packets.size() << " " << sim <<
                            " " << sim_ack << " " << data << " ### ";
            for (int k = 0; k < this->sns_switches[i]->queues[j]->packets.size(); k++){
                queue_util_file << this->sns_switches[i]->queues[j]->packets[k]->sns_type <<  " " << this->sns_switches[i]->queues[j]->packets[k]->sns_msg_id << " ";
            }
            queue_util_file << ((SnsQueue*)(this->sns_switches[i]->queues[j]))->sim_packets.size() << "\n";
            //}
            if (params.debug_sns) {
                if (this->sns_switches[i]->queues[j]->packets.size() > 0){
                    
                    std::cout << "DS1 sw: " << i << " queue: " << j << " uniq_id: " << this->sns_switches[i]->queues[j]->unique_id
                                << " num of data packets: " << this->sns_switches[i]->queues[j]->packets.size() <<
                            "\n";
                        for (const auto& pkt : this->sns_switches[i]->queues[j]->packets) {
                            std::cout << pkt->sns_msg_id << " ";
                        }
                    std::cout << "\n";
                }
            }
        }
        
    }
    for (int i = 0; i < 6; i++) {
    	uint64_t total = 0;
    	int record_time =  this->hosts[0]->queue->record_time;
    	if(total_queue_length[i].size() == 0) {
    		continue;
    	}
    	for(int j = 0; j < total_queue_length[i].size(); j++) {
    		total += total_queue_length[i][j];
    	}
        if (params.debug_sns) {
	    std::cout << "queue pos " << i << " " << double(total) / total_queue_length[i].size() / record_time
	    << " " << max_queue_length[i] << std::endl;
        }
    }
    uint64_t max_switch_queue = 0;
    uint64_t max_avg_switch_queue = 0;
    for(int i = 0; i < this->switches.size(); i++) {
            max_switch_queue = std::max(max_switch_queue, this->switches[i]->max_bytes_in_switch);
            uint64_t average = this->switches[i]->total_bytes_in_switch / this->switches[i]->record_time;
            max_avg_switch_queue = std::max(average, max_avg_switch_queue);        
    }
    if (params.debug_sns) {
    std::cout << "queue per switch: " << max_avg_switch_queue << " " << max_switch_queue << std::endl;
    }
}

double SnsLeafSpineTopology::get_oracle_fct(Flow *f) {
    int num_hops = 4;
    if (f->src->id / hosts_per_agg_switch == f->dst->id / hosts_per_agg_switch) {
        num_hops = 2;
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
    if(leftover != 0) {
        incl_overhead_bytes += f->hdr_size;
    }
    double bandwidth = this->access_bw / 1000000.0; // For us
    double transmission_delay;
    if (params.cut_through) {
        transmission_delay = 
            (
                np * (params.mss + params.hdr_size)
                + 1 * params.hdr_size
                + 2.0 * params.hdr_size // ACK has to travel two hops
            ) * 8.0 / bandwidth;
        if (num_hops == 4) {
            //1 packet and 1 ack
            transmission_delay += 2 * (2*params.hdr_size) * 8.0 / (this->core_bw / 1000000.0);
        }
        //std::cout << "pd: " << propagation_delay << " td: " << transmission_delay << std::endl;
    }
    else {
		transmission_delay = (incl_overhead_bytes + f->hdr_size) * 8.0 / bandwidth;
		if (num_hops == 4) {
			// 1 packet and 1 ack
			if (leftover != params.mss && leftover != 0) {
				// less than mss sized flow. the 1 packet is leftover sized.
				transmission_delay += 2 * (leftover + 2*params.hdr_size) * 8.0 / (this->core_bw / 1000000.0);
				
			} else {
				// 1 packet is full sized
				transmission_delay += 2 * (params.mss + 2*params.hdr_size) * 8.0 / (this->core_bw / 1000000.0);
			}
		}
        if (leftover != params.mss && leftover != 0) {
            // less than mss sized flow. the 1 packet is leftover sized.
            transmission_delay += (leftover + 2*params.hdr_size) * 8.0 / (bandwidth);
            
        } else {
            // 1 packet is full sized
            transmission_delay += (params.mss + 2*params.hdr_size) * 8.0 / (bandwidth);
        }
        //transmission_delay = 
        //    (
        //        (np + 1) * (params.mss + params.hdr_size) + (leftover + params.hdr_size)
        //        + 2.0 * params.hdr_size // ACK has to travel two hops
        //    ) * 8.0 / bandwidth;
        //if (num_hops == 4) {
        //    //1 packet and 1 ack
        //    transmission_delay += 2 * (params.mss + 2*params.hdr_size) * 8.0 / (4 * bandwidth);  //TODO: 4 * bw is not right.
        //}
    }
    return (propagation_delay + transmission_delay); //us
}
double SnsLeafSpineTopology::get_control_pkt_rtt(int host_id) {
    if(host_id / hosts_per_agg_switch == 0) {
        return (2 * params.propagation_delay + (40 * 8 / this->access_bw) * 2) * 2;
    } else {
        return (4 * params.propagation_delay + (40 * 8 / this->access_bw + 40 * 8 / this->core_bw) * 2) * 2;
    }
}

double SnsLeafSpineTopology::get_worst_rtt(Flow *f) {
    int num_hops = 4;
    if (is_same_rack(f->src, f->dst)) {
        num_hops = 2;
    }
    double propagation_delay;
    propagation_delay = 2 * 1000000.0 * num_hops * f->src->queue->propagation_delay; //us
    double bandwidth = f->src->queue->rate / 1000000.0; // For us
    double transmission_delay;
    transmission_delay = (params.mss + f->hdr_size) * 8.0 / bandwidth;
    transmission_delay *= num_hops;
    transmission_delay *= params.simulation_queue_size; // Worst case all packets in queue
    return (propagation_delay + transmission_delay); //us
}

int SnsLeafSpineTopology::num_hosts_per_tor() {
    return hosts_per_agg_switch;
}
