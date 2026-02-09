#ifndef QUEUE_H
#define QUEUE_H

#include <deque>
#include <stdint.h>
#include <vector>
#include <fstream>

#define DROPTAIL_QUEUE 1

class Node;
class Packet;
class Event;

class QueueProcessingEvent;
class PacketPropagationEvent;

class Queue {
    public:
        Queue(uint32_t id, double rate, int limit_bytes, int location);
        void set_src_dst(Node *src, Node *dst);
        bool check_if_there_is_already_x_simulation(u_int32_t threshold);
        virtual void enque(Packet *packet);
        virtual Packet *deque();
        virtual void drop(Packet *packet);
        double get_transmission_delay(uint32_t size);
        void preempt_current_transmission();
        uint32_t count_sns_sim_packets();
        uint32_t count_sns_data_packets();
        std::string get_all_packet_types_string();
        void print_sns_sim_packet_count();
        void record_queueing_entry(Packet *packet, int is_deque);

        double record() {
            max_bytes_in_queue = std::max(max_bytes_in_queue, bytes_in_queue);
            total_bytes_in_queue = total_bytes_in_queue + bytes_in_queue;
            record_time += 1;
        };
        // Members
        uint32_t id;
        uint32_t unique_id;
        static uint32_t instance_count;
        double rate;
        int limit_bytes;
        std::deque<Packet *> packets;
        uint32_t bytes_in_queue;
        bool busy;
        QueueProcessingEvent *queue_proc_event;

        std::vector<Event*> busy_events;
        Packet* packet_transmitting;

        Node *src;
        Node *dst;

        uint64_t b_arrivals, b_departures;
        uint64_t p_arrivals, p_departures;

        double propagation_delay;
        bool interested;

        uint64_t pkt_drop;
        uint64_t spray_counter;

        int location;

        // for tracing purpose for recording queue event
        uint32_t max_bytes_in_queue;
        uint64_t total_bytes_in_queue;
        uint64_t record_time;

        uint32_t current_sim;
        uint32_t current_data;
        uint32_t free_tokens;
        int token_bucket_run;
};


class ProbDropQueue : public Queue {
    public:
        ProbDropQueue(
                uint32_t id, 
                double rate, 
                uint32_t limit_bytes,
                double drop_prob, 
                int location
                );
        virtual void enque(Packet *packet);

        double drop_prob;
};

class SnsQueue : public Queue {
    public:
        SnsQueue(
                uint32_t id, 
                double rate, 
                uint32_t limit_bytes,
                int location
                );
        virtual void enque(Packet *packet);
        virtual Packet *deque();
        
        double get_transmission_delay(Packet *packet);
        int sim_turn;
        std::deque<Packet *> sim_packets;
        uint32_t sim_bytes_in_queue;
        std::ofstream queue_detail_file;

};

#endif
