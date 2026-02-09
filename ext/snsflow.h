#ifndef SNS_FLOW_H
#define SNS_FLOW_H

#include "../coresim/flow.h"
#include "../coresim/node.h"
#include "../coresim/packet.h"
#include "../ext/schedulinghost.h"
enum WindowMsgStatus {
    INIT,
    SIM_SENT,
    SIM_ACK_RECEIVED,
    DATA_SENT,
    DATA_ACK_RECEIVED
};
struct WindowMsgElement {
    double time; 
    int id;   
    WindowMsgStatus status; 
};
class RollingAverage {
private:
    double times[3] = {0, 0, 0}; // Stores the last 3 function call values
    int index = 0;               // Keeps track of the current index
    int count = 0;               // Number of values stored (max 3)

public:
    void addValue(double value); // Adds a new value to the rolling average
    double getAverage() const;   // Returns the average of stored values
};
class SnsFlow;
class SnsFlowSendDataAckEvent : public Event {
    public:
        SnsFlowSendDataAckEvent(double time, SnsFlow *host);
        ~SnsFlowSendDataAckEvent();
        void process_event();
        SnsFlow *flow;
        bool is_timeout;
};
class SnsFlow : public Flow {
    //static const int window_size = 10000;
    //static const int window_size = 1;

    public:
        SnsFlow(uint32_t id, double start_time, uint32_t size, Host* s, Host* d);
        virtual ~SnsFlow();  // Destructor to clean up allocated memory
        virtual void start_flow();
        int get_free_slot_in_window();
        int get_slot_by_id(int id);
        void drop_sim_if_timeout_pass();
        void check_and_set_to_finished();
        int get_number_of_flows_in_host(Host *src);
        void send_simulation();
        void send_non_simulation();
        void send_pending_data();
        void set_timeout(double time);
        void handle_timeout();
        void receive(Packet *p);
        void send_data_ack();
        
        // Getter for queueing times data with safety checks
        const std::vector<std::vector<double>>& get_queueing_times() const { 
            // Return empty static vector if this object is potentially corrupted
            static const std::vector<std::vector<double>> empty_vector;
            if (this == nullptr) {
                return empty_vector;
            }
            return queueing_times; 
        }

        //virtual uint32_t get_priority(uint32_t seq);
        //WindowMsgElement sim_window[window_size];
        WindowMsgElement *sim_window;
        int msg_id;
        int data_msg_id;
        bool stop_sending_sim;
        int window_size;
        int sim_turn;
        double last_sim_sent_time;
        RollingAverage sim_times;
        RollingAverage data_times;
        SnsFlowSendDataAckEvent* send_data_ack_event;
        std::multiset<uint32_t> acked_msg_ids;
        int dst_current_active_flows;
        
        // Matrix to track paths based on (saved_hash_port, saved_hash_port_l2) pairs over time
        std::vector<std::vector<int>> hist_path_matrix; // [port_pair_index][time_step]
        int time_steps; // Number of time steps to track
        int current_time_step; // Current time step index
        int k_value; // Topology parameter k (to calculate max port index: k/2)
        int enable_path_saving;
        double current_avg_rtt;
        std::vector<std::vector<double>> queueing_times;
        
        // Functions to manage the path history matrix
        void init_path_matrix(int k, int time_steps);
        void add_path_to_matrix(int saved_hash_port, int saved_hash_port_l2, int time_step, int value);
        int get_path_from_matrix(int saved_hash_port, int saved_hash_port_l2, int time_step);
        int get_port_pair_index(int saved_hash_port, int saved_hash_port_l2);
        std::pair<int, int> sample_port_pair_from_history(double temperature = 1.0);
        void print_path_matrix(); // Function to print matrix contents for debugging
        void reset_current_time_step(); // Function to reset all pairs for the current time step
};


#endif
