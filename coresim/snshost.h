#ifndef SNS_HOST_H
#define SNS_HOST_H

#include <set>
#include <queue>
#include <map>
#include <vector>
#include <deque>

#include "../coresim/node.h"
#include "../coresim/packet.h"
#include "../coresim/event.h"
#include "../ext/schedulinghost.h"
#include "../ext/snsflow.h"
#include "../coresim/flow.h"


class SnsHost : public SchedulingHost{
    public:
        SnsHost(uint32_t id, double rate, uint32_t queue_type);
        void start_flow(SnsFlow* f);
        void schedule_host_proc_evt();
        void send();
        void move_sim_to_packets();
        SnsPacket* get_sim_or_sim_ack();
        void move_packets_to_queue();
        void update_dynamic_clock_drift_factor();
        virtual ~SnsHost() = default;
        
        // Utility functions for controlling scheduling behavior
        void set_collective_round_robin(bool enabled) { enable_collective_round_robin = enabled; }
        void set_auto_fallback_single_collective(bool enabled) { auto_fallback_single_collective = enabled; }
        void set_parallel_receiver_limit(bool enabled) { enable_parallel_receiver_limit = enabled; }

        SnsQueue *queue;
        //Queue *queue_simulation;
        //Queue *queue_simulation_ack;
        std::deque<SnsFlow*> active_sending_flows;
        double clock_drift_factor; // Pre-calculated clock drift factor for worst RTT calculation
        double clock_drift_factor_dynamic; // Pre-calculated clock drift factor for worst RTT calculation
        std::deque<SnsPacket *> packets_to_be_sent;
        std::deque<SnsPacket *> sims_to_be_sent;
        std::set<uint32_t> active_receivers; // Track currently active receiver IDs (persistent across send() calls)
        // static const int max_parallel_receivers = 4; // k=4 receivers in parallel
        static const int max_parallel_receivers = 10; // k=4 receivers in parallel
        bool enable_parallel_receiver_limit = false; // Control variable to enable/disable parallel receiver limiting
        bool enable_collective_round_robin = true; // Control variable to enable round-robin across collectives
        bool auto_fallback_single_collective = true; // Automatically fall back to simple round-robin when only one collective

    private:
        void send_with_parallel_receiver_limit();
        void send_with_collective_round_robin();
        
        // Collective round-robin state
        std::map<uint32_t, std::deque<SnsFlow*>> collective_flows; // Maps collective_id -> flows in that collective
        std::vector<uint32_t> collective_order; // Order of collectives for round-robin
        size_t current_collective_index; // Current position in round-robin across collectives
        std::map<uint32_t, size_t> collective_flow_index; // Current position in round-robin within each collective
        
        void organize_flows_by_collective();
        SnsFlow* get_next_flow_round_robin();


};
class SnsSwitch : public Switch { // replaced by core and agg below
    public:
        SnsSwitch(uint32_t id, uint32_t switch_type);
        std::vector<Queue *> queues_simulation;
        std::vector<Queue *> queues_simulation_ack;
        
};
class SnsCoreSwitch : public SnsSwitch {
    public:
        //All queues have same rate
        SnsCoreSwitch(uint32_t id, uint32_t nq, double rate, uint32_t queue_type);
        //std::vector<Queue *> queues_simulation;
};

class SnsAggSwitch : public SnsSwitch {
    public:
        // Different Rates
        SnsAggSwitch(uint32_t id, uint32_t nq1, double r1, uint32_t nq2, double r2, uint32_t queue_type);
        //std::vector<Queue *> queues_simulation;
        //Queue* queue_to_arbiter;
};


#endif
