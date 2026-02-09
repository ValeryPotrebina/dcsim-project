//
//  event.cpp
//  TurboCpp
//
//  Created by Gautam Kumar on 3/9/14.
//
//

#include <iomanip>

#include "event.h"
#include "packet.h"
#include "topology.h"
#include "SnsleafSpineTopology.h"
#include "SnsFatTreeTopology.h"
#include "fatTreeTopology.h"
#include "debug.h"

#include "../ext/factory.h"
#include "../ext/snsflow.h"

#include "../run/params.h"

extern Topology* topology;
extern std::priority_queue<Event*, std::vector<Event*>, EventComparator> event_queue;
extern double current_time;
extern DCExpParams params;
extern std::deque<Event*> flow_arrivals;
extern std::deque<Flow*> flows_to_schedule;

extern long long num_outstanding_packets;
extern long long max_outstanding_packets;

extern long long num_outstanding_packets_at_50;
extern long long num_outstanding_packets_at_100;
extern long long arrival_packets_at_50;
extern long long arrival_packets_at_100;
extern long long arrival_packets_count;
extern uint32_t total_finished_flows;

extern uint32_t backlog3;
extern uint32_t backlog4;
extern uint32_t duplicated_packets_received;
extern uint32_t duplicated_packets;
extern uint32_t injected_packets;
extern uint32_t completed_packets;
extern uint32_t total_completed_packets;
extern uint32_t dead_packets;
extern uint32_t sent_packets;

extern EmpiricalRandomVariable *nv_bytes;

extern double get_current_time();
extern void add_to_event_queue(Event *);
extern int get_event_queue_size();

uint32_t Event::instance_count = 0;

Event::Event(uint32_t type, double time) {
    this->type = type;
    this->time = time;
    this->cancelled = false;
    this->unique_id = Event::instance_count++;
}

Event::~Event() {
}


/* Flow Arrival */
FlowCreationForInitializationEvent::FlowCreationForInitializationEvent(
        double time, 
        Host *src, 
        Host *dst,
        EmpiricalRandomVariable *nv_bytes, 
        RandomVariable *nv_intarr
    ) : Event(FLOW_CREATION_EVENT, time) {
    this->src = src;
    this->dst = dst;
    this->nv_bytes = nv_bytes;
    this->nv_intarr = nv_intarr;
}

FlowCreationForInitializationEvent::~FlowCreationForInitializationEvent() {}

void FlowCreationForInitializationEvent::process_event() {
    uint32_t nvVal, size;
    uint32_t id = flows_to_schedule.size();
    if (params.debug_sns){
        std::cout << "FlowCreationForInitializationEvent::process_event flows_to_schedule.size() " <<  flows_to_schedule.size() << "\n";
        std::cout << "params.bytes_mode " << params.bytes_mode << "\n";
    }
    if (params.bytes_mode) {
        nvVal = nv_bytes->value();
        size = (uint32_t) nvVal;
    } else {
        nvVal = (nv_bytes->value() + 0.5); // truncate(val + 0.5) equivalent to round to nearest int
        if (nvVal > 2500000) {
            std::cout << "Giant Flow! event.cpp::FlowCreation:" << 1000000.0 * time << " Generating new flow " << id << " of size " << (nvVal*params.mss) << " between " << src->id << " " << dst->id << "\n";
            nvVal = 2500000;
        }
        size = (uint32_t) nvVal * params.mss;
    }
    Host* new_dst = dst;
    
    while(new_dst == NULL) {
        new_dst =  topology->hosts[rand()% topology->hosts.size()];
        if (new_dst->id == src->id) {
            new_dst = NULL;
        }
    }

    if (size != 0) {
        flows_to_schedule.push_back(Factory::get_flow(id, time, size, src, new_dst, params.flow_type));
    }

    double tnext = time + nv_intarr->value();
//        std::cout << "event.cpp::FlowCreation:" << 1000000.0 * time << " Generating new flow " << id << " of size "
//         << size << " between " << src->id << " " << dst->id << " " << (tnext - get_current_time())*1e6 << "\n";

    add_to_event_queue(
            new FlowCreationForInitializationEvent(
                tnext,
                src, 
                dst,
                nv_bytes, 
                nv_intarr
                )
            );
}


/* Flow Arrival */
int flow_arrival_count = 0;

FlowArrivalEvent::FlowArrivalEvent(double time, Flow* flow) : Event(FLOW_ARRIVAL, time) {
    this->flow = flow;
}

FlowArrivalEvent::~FlowArrivalEvent() {
}

void FlowArrivalEvent::process_event() {
    //Flows start at line rate; so schedule a packet to be transmitted
    //First packet scheduled to be queued

    num_outstanding_packets += (this->flow->size / this->flow->mss);
    arrival_packets_count += this->flow->size_in_pkt;
    if (num_outstanding_packets > max_outstanding_packets) {
        max_outstanding_packets = num_outstanding_packets;
    }
    this->flow->start_flow();
    flow_arrival_count++;
    if (flow_arrivals.size() > 0) {
        add_to_event_queue(flow_arrivals.front());
        flow_arrivals.pop_front();
    }

    if(params.num_flows_to_run > 10 && flow_arrival_count % 100000 == 0){
    //DS if(params.num_flows_to_run > 10 && flow_arrival_count % 10 == 0){

        double curr_time = get_current_time();
        uint32_t num_unfinished_flows = 0;
        for (uint32_t i = 0; i < flows_to_schedule.size(); i++) {
            Flow *f = flows_to_schedule[i];
            if (f->start_time < curr_time) {
                if (!f->finished) {
                    num_unfinished_flows ++;
                }
            }
        }
        if(flow_arrival_count == (int)(params.num_flows_to_run * 0.5))
        {
            arrival_packets_at_50 = arrival_packets_count;
            num_outstanding_packets_at_50 = num_outstanding_packets;
        }
        if(flow_arrival_count == params.num_flows_to_run)
        {
            arrival_packets_at_100 = arrival_packets_count;
            num_outstanding_packets_at_100 = num_outstanding_packets;
        }
        if (params.debug_sns)
            std::cout << "## " << current_time << " NumPacketOutstanding " << num_outstanding_packets
                << " NumUnfinishedFlows " << num_unfinished_flows << " StartedFlows " << flow_arrival_count
                << " StartedPkts " << arrival_packets_count << std::endl;
        if(params.debug_queue) {
            topology->print_queue_length();
        }
    }
    
}


/* Packet Queuing */
PacketQueuingEvent::PacketQueuingEvent(double time, Packet *packet,
        Queue *queue) : Event(PACKET_QUEUING, time) {
    this->packet = packet;
    this->queue = queue;
}

PacketQueuingEvent::~PacketQueuingEvent() {
}

void PacketQueuingEvent::process_event() {
    if (!queue->busy) {
        queue->queue_proc_event = new QueueProcessingEvent(get_current_time(), queue);
        add_to_event_queue(queue->queue_proc_event);
        queue->busy = true;
        queue->packet_transmitting = packet;
    }
    else if( params.preemptive_queue && this->packet->pf_priority < queue->packet_transmitting->pf_priority) {
        double remaining_percentage;
        if (params.flow_type != SNS_FLOW)
            remaining_percentage = (queue->queue_proc_event->time - get_current_time()) / queue->get_transmission_delay(queue->packet_transmitting->size);
        else {
            remaining_percentage = (queue->queue_proc_event->time - get_current_time()) / ((SnsQueue*)queue)->get_transmission_delay(queue->packet_transmitting);
        }

        if(remaining_percentage > 0.01){
            queue->preempt_current_transmission();

            queue->queue_proc_event = new QueueProcessingEvent(get_current_time(), queue);
            add_to_event_queue(queue->queue_proc_event);
            queue->busy = true;
            queue->packet_transmitting = packet;
        }
    }
    queue->enque(packet);
    if(params.debug_queue) {
        if (dynamic_cast<SnsLeafSpineTopology*>(topology) != nullptr) {
            ((SnsLeafSpineTopology*)topology)->print_queue_length1(this->time);
        }
   }
}

/* Packet Arrival */
PacketArrivalEvent::PacketArrivalEvent(double time, Packet *packet)
    : Event(PACKET_ARRIVAL, time) {
        this->packet = packet;
    }

PacketArrivalEvent::~PacketArrivalEvent() {
}

void PacketArrivalEvent::process_event() {
    // if (!packet) {
    //     // std::cerr << "Error: PacketArrivalEvent::process_event() - packet is null at time " 
    //     //           << get_current_time() << std::endl;
    //     return;
    // }
    
    // if (!packet->flow) {
    //     // std::cerr << "Error: PacketArrivalEvent::process_event() - packet->flow is null for packet " 
    //     //           << packet->unique_id << " at time " << get_current_time() << std::endl;
    //     return;
    // }
    
    // // Additional validation: Check if flow ID looks corrupted
    // // Valid flow IDs should be reasonable numbers, not near UINT_MAX
    // if (packet->flow->id >= 0xFFFFFFF0U) {
    //     // std::cerr << "Error: PacketArrivalEvent::process_event() - corrupted flow pointer detected. "
    //     //           << "Flow ID: " << packet->flow->id << " (likely corrupted memory) "
    //     //           << "packet_id: " << packet->unique_id << " at time " << get_current_time() << std::endl;
    //     // Clean up the packet and return to avoid crash
    //     delete packet;
    //     return;
    // }
    
    // // Additional safety check: verify flow is not finished before proceeding
    // if (packet->flow->finished) {
    //     // std::cerr << "Warning: PacketArrivalEvent::process_event() - packet arrived for finished flow. "
    //     //           << "Flow ID: " << packet->flow->id << " packet_id: " << packet->unique_id 
    //     //           << " at time " << get_current_time() << std::endl;
    //     // Clean up the packet and return
    //     delete packet;
    //     return;
    // }
    
    if (packet->type == NORMAL_PACKET) {
        completed_packets++;
    }
    // std::cout << std::setprecision(8) << "PacketArrivalEvent::process_event time: " << (get_current_time()-1)*1e6 << " packet_id: " << packet->unique_id << " flow_id: " << packet->flow->id << " seq_no: " << packet->seq_no << " size: " << packet->size << " src: " << packet->src->id << " dst: " << packet->dst->id << "\n";

    packet->flow->receive(packet);
    
}


/* Queue Processing */
QueueProcessingEvent::QueueProcessingEvent(double time, Queue *queue)
    : Event(QUEUE_PROCESSING, time) {
        this->queue = queue;
}

QueueProcessingEvent::~QueueProcessingEvent() {
    if (queue->queue_proc_event == this) {
        queue->queue_proc_event = NULL;
        queue->busy = false; 
    }
}

void QueueProcessingEvent::process_event() {
    Packet *packet = queue->deque();

    if (packet) {
        // // Check for null flow pointer before processing
        // if (packet->flow == nullptr) {
        //     std::cerr << "Error: QueueProcessingEvent::process_event() - packet->flow is null for packet " 
        //               << packet->unique_id << " at time " << get_current_time() << std::endl;
        //     // Skip this packet and continue processing the queue
        //     queue->busy = false;
        //     queue->busy_events.clear();
        //     queue->packet_transmitting = NULL;
        //     queue->queue_proc_event = NULL;
        //     delete packet;
        //     return;
        // }
        
        // // Additional validation: Check if flow ID looks corrupted
        // if (packet->flow->id >= 0xFFFFFFF0U) {
        //     std::cerr << "Error: QueueProcessingEvent::process_event() - corrupted flow pointer detected. "
        //               << "Flow ID: " << packet->flow->id << " (likely corrupted memory) "
        //               << "packet_id: " << packet->unique_id << " at time " << get_current_time() << std::endl;
        //     // Skip this packet and continue processing the queue
        //     queue->busy = false;
        //     queue->busy_events.clear();
        //     queue->packet_transmitting = NULL;
        //     queue->queue_proc_event = NULL;
        //     delete packet;
        //     return;
        // }
        
        queue->busy = true;
        queue->busy_events.clear();
        queue->packet_transmitting = packet;
        Queue *next_hop = topology->get_next_hop(packet, queue);
        //std::cout << "QueueProcessingEvent::process_event queue_id: " << queue->unique_id << " packet->sns_typ: " << packet->sns_type <<   "\n";
        //std::cout << get_current_time() << "packet->sns_typ: " << packet->sns_type << " " ;
        double td;
        if (params.flow_type != SNS_FLOW)
            td = queue->get_transmission_delay(packet->size);
        else
            td = ((SnsQueue*)queue)->get_transmission_delay(packet);
         if (params.debug_sns){
            std::cout << std::setprecision(4) << " AA_td: " << td*1000000 << " sns_type: " << packet->sns_type << " unique_id: " << queue->unique_id << "packet_size: " << packet->size << "\n";
         }
        double pd = queue->propagation_delay;
        // if (packet->sns_type == SNS_SIM_ACK_PACKET){
            // pd = td = 0;
        // }
        // td = 0;
        // if (packet->sns_type == SNS_SIM_PACKET){
        //     pd = pd/SNS_SIMULATION_FRAQ_SIZE;
        // }

        //double additional_delay = 1e-10;
        queue->queue_proc_event = new QueueProcessingEvent(time + td, queue);
        add_to_event_queue(queue->queue_proc_event);
        queue->busy_events.push_back(queue->queue_proc_event);
        //std::cout << "33flowid next_hop: " << next_hop << " \n";
        if (next_hop == NULL) {
            //std::cout << "33flowid next_hop: " << next_hop << " \n";
            Event* arrival_evt = new PacketArrivalEvent(time + td + pd, packet);
            add_to_event_queue(arrival_evt);
            queue->busy_events.push_back(arrival_evt);
        } else {
            Event* queuing_evt = NULL;
            if (params.cut_through == 1) {
                double cut_through_delay;
                if (params.flow_type != SNS_FLOW)
                    cut_through_delay = queue->get_transmission_delay(packet->flow->hdr_size);
                else
                    cut_through_delay = ((SnsQueue*)queue)->get_transmission_delay(packet);
                queuing_evt = new PacketQueuingEvent(time + cut_through_delay + pd, packet, next_hop);
            } else {
                queuing_evt = new PacketQueuingEvent(time + td + pd, packet, next_hop);
            }

            add_to_event_queue(queuing_evt);
            queue->busy_events.push_back(queuing_evt);
        }
    } else {
        queue->busy = false;
        queue->busy_events.clear();
        queue->packet_transmitting = NULL;
        queue->queue_proc_event = NULL;
    }
}


FillTokenBucketEvent::FillTokenBucketEvent(double time, Queue *queue)
    : Event(QUEUE_PROCESSING, time) {
        this->queue = queue;
}

FillTokenBucketEvent::~FillTokenBucketEvent() {
}

void FillTokenBucketEvent::process_event() {
                // std::cout << std::setprecision(4) << "1 " << this->queue->free_tokens << "\n";

    if (this->queue->free_tokens < 2){
        this->queue->free_tokens++;
    }
    Event* queuing_evt = NULL;
    double td = (params.hdr_size * 8.0) / (queue->rate/params.sns_simulation_frq_size);

    queuing_evt = new FillTokenBucketEvent(time + td, this->queue);

    add_to_event_queue(queuing_evt);
    if (this->queue->free_tokens == 1){
        add_to_event_queue(new SnsQueueProcessingEvent(get_current_time() + INFINITESIMAL_TIME, this->queue));
    }


}
SnsQueueProcessingEvent::SnsQueueProcessingEvent(double time, Queue *queue)
    : Event(QUEUE_PROCESSING, time) {
        this->queue = queue;
}

SnsQueueProcessingEvent::~SnsQueueProcessingEvent() {
}

void SnsQueueProcessingEvent::process_event() {
    if (this->queue->free_tokens < 1 ||
        ((SnsQueue*)this->queue)->sim_packets.empty())
    {
            // std::cout << std::setprecision(4) << "2 " << this->queue->free_tokens << "\n";

        return;
    }
            // std::cout << std::setprecision(4) << "3 SnsQueueProcessingEvent deque simulation time: " << (get_current_time()-1)*1e6 << " sns_type: " << p->sns_type << " unique_id: " << queue->unique_id << "packet_size: " << p->size << "\n";

    this->queue->free_tokens--;


    Packet *p = ((SnsQueue*)this->queue)->sim_packets.front();
    // std::cout << std::setprecision(4) << "3 SnsQueueProcessingEvent deque simulation time: " << (get_current_time()-1)*1e6 << " sns_type: " << p->sns_type << " unique_id: " << queue->unique_id << "packet_size: " << p->size << "\n";

    assert(p);
    //Packet *p = ((SnsQueue*)this->queue)->sim_packets.back();
    if (params.debug_sns){
        std::cout << std::setprecision(4) << " SnsQueueProcessingEvent deque simulation time: " << (get_current_time()-1)*1e6 << " sns_type: " << p->sns_type << " unique_id: " << queue->unique_id << "packet_size: " << p->size << "\n";
    }
    ((SnsQueue*)this->queue)->sim_packets.pop_front();
    ((SnsQueue*)this->queue)->current_sim--;
    //((SnsQueue*)this->queue)->sim_packets.pop_back();
    ((SnsQueue*)this->queue)->sim_bytes_in_queue -= p->size;
    
    double td = INFINITESIMAL_TIME;

    if (p)
        td = ((SnsQueue*)queue)->get_transmission_delay(p);
    ((SnsQueue*)this->queue)->Queue::enque(p);
    if (queue->queue_proc_event == NULL){
        queue->queue_proc_event = new QueueProcessingEvent(time + td, queue);
        add_to_event_queue(queue->queue_proc_event);
        queue->busy_events.push_back(queue->queue_proc_event);
    }

}

LoggingEvent::LoggingEvent(double time) : Event(LOGGING, time){
    this->ttl = 1e10;
}

LoggingEvent::LoggingEvent(double time, double ttl) : Event(LOGGING, time){
    this->ttl = ttl;
}

LoggingEvent::~LoggingEvent() {
}

void LoggingEvent::process_event() {
    double current_time = get_current_time();
    // can log simulator statistics here.
}


/* Flow Finished */
FlowFinishedEvent::FlowFinishedEvent(double time, Flow *flow)
    : Event(FLOW_FINISHED, time) {
        this->flow = flow;
    }

FlowFinishedEvent::~FlowFinishedEvent() {}

void FlowFinishedEvent::process_event() {
    this->flow->finished = true;
    this->flow->finish_time = get_current_time();
    this->flow->flow_completion_time = this->flow->finish_time - this->flow->start_time;
    total_finished_flows++;
    auto slowdown = 1000000 * flow->flow_completion_time / topology->get_oracle_fct(flow);
    if (slowdown < 1.0 && slowdown > 0.9999) {
        slowdown = 1.0;
    }

    static std::ofstream fct_file;

    if(params.fct_file != "") {
        if(!fct_file.is_open()) {
            fct_file.open(params.fct_file);
        }
        fct_file << params.load << " " << 1000000 * flow->finish_time - 1000000 * flow->start_time << std::endl;
    }

    int num_hops = 6;
    // Only calculate hops for Fat Tree topologies
    FatTreeTopology* fat_tree = dynamic_cast<FatTreeTopology*>(topology);
    SnsFatTreeTopology* sns_fat_tree = dynamic_cast<SnsFatTreeTopology*>(topology);
    
    if (fat_tree != nullptr) {
        if (fat_tree->is_same_rack(flow->src, flow->dst)) {
            num_hops = 2;
        } else if(fat_tree->is_same_pod(flow->src, flow->dst)) {
            num_hops = 4;
        }
    } else if (sns_fat_tree != nullptr) {
        if (sns_fat_tree->is_same_rack(flow->src, flow->dst)) {
            num_hops = 2;
        } else if(sns_fat_tree->is_same_pod(flow->src, flow->dst)) {
            num_hops = 4;
        }
    }
    // Write queueing data to file if this is an SNS_FLOW and fct_file is specified
    if (params.flow_type == SNS_FLOW) {
        // Use fct_file with .queueing extension since queuing_file parameter doesn't exist
        static std::ofstream queueing_file;
        static bool queueing_file_initialized = false;
        
        if (!queueing_file_initialized) {
            std::string queueing_filename = params.queuing_file;
            queueing_file.open(queueing_filename);
            if (queueing_file.is_open()) {
                // Write header to the file
                queueing_file << "# flow_id sns_msg_id packet_type queuing_delay_microseconds" << std::endl;
                queueing_file_initialized = true;
            }
        }
        
        if (queueing_file.is_open()) {
            // Additional safety check: verify flow is not corrupted
            
            
            // Cast flow to SnsFlow to access queueing data
            SnsFlow* sns_flow = dynamic_cast<SnsFlow*>(this->flow);
            if (sns_flow != nullptr) {
                try {
                    const auto& queueing_times = sns_flow->get_queueing_times();
                    // if (!queueing_times.empty() && num_hops == 6) {
                    if (!queueing_times.empty()) {
                        for (const auto& entry : queueing_times) {
                            // Ensure the entry has the expected number of elements
                            if (entry.size() >= 4) {
                                // Format: flow_id sns_msg_id packet_type queuing_delay_microseconds
                                queueing_file << std::fixed << std::setprecision(6)
                                            << (entry[0]) << " "  // time
                                             << sns_flow->id << " " 
                                             << static_cast<int>(entry[1]) << " "  // sns_msg_id (as int)
                                             << static_cast<int>(entry[2]) << " "  // packet_type (0=SIM, 1=DATA)
                                             << entry[3] << std::endl;  // queuing_delay_microseconds (as double)
                            }
                        }
                        queueing_file.flush();
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Error: FlowFinishedEvent::process_event() - exception while accessing queueing data: " 
                              << e.what() << " for flow ID: " << sns_flow->id << " at time " << get_current_time() << std::endl;
                } catch (...) {
                    std::cerr << "Error: FlowFinishedEvent::process_event() - unknown exception while accessing queueing data "
                              << "for flow ID: " << sns_flow->id << " at time " << get_current_time() << std::endl;
                }
            }
        }
    }


    if (print_flow_result()) {
        std::cout << std::setprecision(4) << std::fixed ;
        if (params.flow_type == SNS_FLOW){
            std::cout                
                << flow->sim_ids_sent.size() << " "
                << flow->sim_ack_ids_sent.size() << " "
                << flow->data_ids_sent.size() << " "
                << flow->data_ids_recived.size() << " "
                << flow->sns_sim_pkt_drop << " "
                << flow->flow_rtt*1000000 << " ";
            
        }
        std::cout
            << flow->id << " "
            << flow->size << " "
            << flow->src->id << " "
            << flow->dst->id << " "
            << 1000000 * flow->start_time << " "
            << 1000000 * flow->finish_time << " "
            << 1000000.0 * flow->flow_completion_time << " "
            << topology->get_oracle_fct(flow) << " "
            << slowdown << " "
            << flow->total_pkt_sent << "/" << (flow->size/flow->mss) << "//" << flow->received_count << " "
            << flow->data_pkt_drop << "/" << flow->ack_pkt_drop << "/" << flow->pkt_drop << " "
            << 1000000 * (flow->first_byte_send_time - flow->start_time) << " "
            << params.BDP << " "
            << flow->collective_id << " "
            << flow->num_of_reorders << " "
            << flow->get_avg_queuing_delay_in_us() << " "
            << num_hops << " "
            // << flow->received_count << " "
            // << flow->total_queuing_time << " "
            << std::endl;
        std::cout << std::setprecision(9) << std::fixed;
    }
    if (slowdown < 1.0) {
        std::cout << "bad slowdown " << "flow size:" << flow->size_in_pkt << " " << 1e6 * flow->flow_completion_time << " " << topology->get_oracle_fct(flow) << " " << slowdown << std::endl;
    }
    assert(slowdown >= 1.0);
}


/* Flow Processing */
FlowProcessingEvent::FlowProcessingEvent(double time, Flow *flow)
    : Event(FLOW_PROCESSING, time) {
        this->flow = flow;
    }

FlowProcessingEvent::~FlowProcessingEvent() {
    if (flow->flow_proc_event == this) {
        flow->flow_proc_event = NULL;
    }
}

void FlowProcessingEvent::process_event() {
    this->flow->send_pending_data();
}


/* Retx Timeout */
RetxTimeoutEvent::RetxTimeoutEvent(double time, Flow *flow)
    : Event(RETX_TIMEOUT, time) {
        this->flow = flow;
    }

RetxTimeoutEvent::~RetxTimeoutEvent() {
    if (flow->retx_event == this) {
        flow->retx_event = NULL;
    }
}

void RetxTimeoutEvent::process_event() {
    flow->handle_timeout();
}

/* Record Queue Event */
RecordQueueEvent::RecordQueueEvent(double time, double interval)
    : Event(RECORD_QUEUEING, time) {
        this->interval = interval;
    }

RecordQueueEvent::~RecordQueueEvent() {
}

void RecordQueueEvent::process_event() {
    for(int i = 0; i < topology->num_hosts; i++) {
        topology->hosts[i]->queue->record();
    }
    for(int i = 0; i < topology->switches.size(); i++) {
        topology->switches[i]->record();
        for (int j = 0; j < topology->switches[i]->queues.size(); j++) {
            topology->switches[i]->queues[j]->record();
        }
    }
    if (total_finished_flows >= params.num_flows_to_run)
        return;
    add_to_event_queue(new RecordQueueEvent(get_current_time() + params.debug_queue_interval, params.debug_queue_interval));

}
