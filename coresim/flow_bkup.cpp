#include <math.h>
#include <iostream>
#include <assert.h>

#include "flow.h"
#include "packet.h"
#include "event.h"

#include "../run/params.h"
#include "../ext/factory.h"

extern double get_current_time(); 
extern void add_to_event_queue(Event *);
extern int get_event_queue_size();
extern DCExpParams params;
extern long long num_outstanding_packets;
extern long long max_outstanding_packets;
extern uint32_t duplicated_packets_received;

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
    this->first_byte_send_time = -1;
    this->first_byte_receive_time = -1;
    this->first_hop_departure = 0;
    this->last_hop_departure = 0;

    this->sns_sim_pkt_drop = 0;
}

Flow::~Flow() {
    //  packets.clear();
}

void Flow::start_flow() {
    send_pending_data();
}

void Flow::send_pending_data() {
    if (params.debug_sns)
        std::cout << " send_pending_data flowid " << this->id << " next_seq_no:" << next_seq_no << " received_bytes " << received_bytes << " size: " << size << " mss: " << mss << std::endl;
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
                if (params.debug_sns)
                    std::cout << " send_pending_data seq " << seq << std::endl;                
            }

            if (seq + mss < size) {
                next_seq_no = seq + mss;
                seq += mss;
            } else {
                next_seq_no = size;
                seq = size;
            }
            if (params.debug_sns)
                    std::cout << " next_seq_no " << next_seq_no << std::endl;
            if (retx_event == NULL) {                
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
    if (params.flow_type == SNS_FLOW){
        if (params.debug_sns)
            std::cout << "Flow::send this->seq_that_got_sns_sim_ack.count(seq) " << this->seq_that_got_sns_sim_ack.count(seq) << " seq " << seq << "\n";
        if (this->seq_that_got_sns_sim_ack.count(seq) != 0){
            // this will not call, we are using it in snsflow.cpp to send regular packet when we receive sns_sim_ack
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
        } else {
            if (this->seq_that_send_sns_sim_ack.count(seq) > 3){
                if (params.debug_sns){
                    std::cout << "this->seq_that_send_sns_sim_ack.count(seq) " <<  this->seq_that_send_sns_sim_ack.count(seq) << " seq " << seq << "\n";
                    std::cout << "1this->sns_sim_pkt_drop: " << this->sns_sim_pkt_drop  << "\n";
                }
                this->sns_sim_pkt_drop++;
                return NULL;
            }
            else{
                SnsPacket *p_sns;
                if (params.debug_sns)
                    std::cout << "Creating SNS_SIM_PACKET " <<  this->seq_that_send_sns_sim_ack.count(seq) << " seq " << seq << "\n";
                    p_sns = new SnsPacket(SNS_SIM_PACKET, 
                        get_current_time(), 
                        this, seq, priority, pkt_size, 
                        src, dst,
                        -1);
                this->seq_that_send_sns_sim_ack.insert(seq);
                add_to_event_queue(new PacketQueuingEvent(get_current_time(), p_sns, src->queue));
                return p_sns;
            }
        }
    }
    else { // regular flow (not sns)
        
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
    }

    add_to_event_queue(new PacketQueuingEvent(get_current_time(), p, src->queue));
    return p;
}

void Flow::send_ack(uint32_t seq, std::vector<uint32_t> sack_list) {
    Packet *p = new Ack(this, seq, sack_list, hdr_size, dst, src); //Acks are dst->src
    add_to_event_queue(new PacketQueuingEvent(get_current_time(), p, dst->queue));
}

void Flow::send_ack(uint32_t seq, std::vector<uint32_t> sack_list, uint32_t seq_for_sns) {
    Packet *p = NULL;
    if (params.flow_type == SNS_FLOW){
        p = new Ack(this, seq, sack_list, hdr_size, dst, src, seq_for_sns); //Acks are dst->src
        if (params.debug_sns)
            std::cout << "Flow::send_ack send ACK with packet seq: " <<  p->seq_no << " seq_for_sns "<< seq_for_sns << "\n";

    }
    else
        p = new Ack(this, seq, sack_list, hdr_size, dst, src); //Acks are dst->src
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
    }
    else {
        assert(false);
    }

    delete p;
}

void Flow::receive_data_pkt(Packet* p) {
    received_count++;
    total_queuing_time += p->total_queuing_delay;
    if (params.debug_sns)
        std::cout << "receive_data_pkt: received.count(p->seq_no) " << received.count(p->seq_no) << " p->seq_no: " <<  p->seq_no << "\n";
    if (received.count(p->seq_no) == 0) {
        received.insert(p->seq_no);
        if(num_outstanding_packets >= ((p->size - hdr_size) / (mss)))
            num_outstanding_packets -= ((p->size - hdr_size) / (mss));
        else
            num_outstanding_packets = 0;
        received_bytes += (p->size - hdr_size);
    } else {
        duplicated_packets_received += 1;
    }
    if (p->seq_no > max_seq_no_recv) {
        max_seq_no_recv = p->seq_no;
    }
    // Determing which ack to send
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
    if (params.debug_sns)
        std::cout << "receive_data_pkt: going to sens ack for seq: " <<  p->seq_no << "\n";
    send_ack(recv_till, sack_list, p->seq_no); // Cumulative Ack
}

void Flow::set_timeout(double time) {
    if (last_unacked_seq < size) {
        RetxTimeoutEvent *ev = new RetxTimeoutEvent(time, this);
        add_to_event_queue(ev);
        retx_event = ev;
    }
}


void Flow::handle_timeout() {
     if (params.flow_type == SNS_FLOW){
        if (params.debug_sns)
            std::cout << "handle_timeout flowid: " << this->id << " last_unacked_seq " << last_unacked_seq << " next_seq_no " <<  next_seq_no << " this->seq_that_send_sns_sim_ack.count(last_unacked_seq) " << this->seq_that_send_sns_sim_ack.count(last_unacked_seq) << "\n";
        if (!finished && next_seq_no == size){  // we want to finish here the flow as ack for last was probably drop
            if (params.debug_sns)
            std::cout << "Flow finished flow id: " << this->id  << "\n";
            cancel_retx_event();      
            finished = true;
            received.clear();
            finish_time = get_current_time();
            flow_completion_time = finish_time - start_time;
            FlowFinishedEvent *ev = new FlowFinishedEvent(get_current_time(), this);
            add_to_event_queue(ev);
        }
        if (this->seq_that_send_sns_sim_ack.count(last_unacked_seq) < 3){
            next_seq_no = last_unacked_seq;            
        }
        else{
            if (params.debug_sns)
                std::cout << "2this->sns_sim_pkt_drop: " << this->sns_sim_pkt_drop  << "\n";
            this->sns_sim_pkt_drop++;
            
                last_unacked_seq += mss;
                if (last_unacked_seq > size)
                    last_unacked_seq = size;

            }
    }
    else
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
    if (params.debug_sns)
        std::cout << "Writing Util file total_recvd " << total_recvd << "\n";


    total_recvd += pkt_size;
    if(params.util_file != "" &&  get_current_time() - last_time > 0.00002) {
        if(!util_file.is_open()) {
            util_file.open(params.util_file);
        }
        util_file << get_current_time() <<  " " << 
            (total_recvd - total_recvd_last)  / (get_current_time() - last_time) / 1000000000 * 8 << " Gbps" << std::endl;
        total_recvd_last = total_recvd;
        last_time = get_current_time();
    }

}

