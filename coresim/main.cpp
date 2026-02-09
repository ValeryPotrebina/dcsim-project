#include <iostream>
#include <algorithm>
#include <fstream>
#include <stdlib.h>
#include <deque>
#include <stdint.h>
#include <time.h>
#include <iomanip>
#include "assert.h"

#include "flow.h"
#include "packet.h"
#include "node.h"
#include "event.h"
#include "topology.h"
#include "fatTreeTopology.h"
#include "queue.h"
#include "random_variable.h"

#include "../ext/factory.h"
//#include "../ext/fastpasshost.h"
#include "../ext/rufhost.h"
#include "../ext/rufTopology.h"

#include "../run/params.h"

using namespace std;

Topology* topology;
double current_time = 0;
std::priority_queue<Event*, std::vector<Event*>, EventComparator> event_queue;
std::deque<Flow*> flows_to_schedule;
std::deque<Event*> flow_arrivals;

long long num_outstanding_packets = 0;
long long max_outstanding_packets = 0;
long long num_outstanding_packets_at_50 = 0;
long long num_outstanding_packets_at_100 = 0;
long long arrival_packets_at_50 = 0;
long long arrival_packets_at_100 = 0;
long long arrival_packets_count = 0;
uint32_t total_finished_flows = 0;
uint32_t duplicated_packets_received = 0;

uint32_t injected_packets = 0;
uint32_t duplicated_packets = 0;
uint32_t dead_packets = 0;
uint32_t completed_packets = 0;
uint32_t backlog3 = 0;
uint32_t backlog4 = 0;
uint32_t total_completed_packets = 0;
uint32_t sent_packets = 0;

extern DCExpParams params;
double start_time = -1;

// Forward declaration
void print_queueing_data();

const std::string currentDateTime() {
    time_t     now = time(0);
    struct tm  tstruct;
    char       buf[80];
    tstruct = *localtime(&now);
    // Visit http://en.cppreference.com/w/cpp/chrono/c/strftime
    // for more information about date/time format
    strftime(buf, sizeof(buf), "%Y-%m-%d.%X", &tstruct);

    return buf;
}

void add_to_event_queue(Event* ev) {
    event_queue.push(ev);
}

int get_event_queue_size() {
    return event_queue.size();
}

double get_current_time() {
    return current_time; // in us
}

/* Runs a initialized scenario */
void run_scenario() {
    // Flow Arrivals create new flow arrivals
    // Add the first flow arrival
    double next_time = 1.0;
    double max = 0;
    if (params.debug_sns)
        std::cout << "1event_queue.size() " << event_queue.size() << "\n";

    if (flow_arrivals.size() > 0) {
        add_to_event_queue(flow_arrivals.front());
        flow_arrivals.pop_front();
    }
    int last_evt_type = -1;
    int same_evt_count = 0;
    if (params.debug_sns)
        std::cout << "2event_queue.size() " << event_queue.size() << "\n";

    if(params.debug_queue) {
        add_to_event_queue(new RecordQueueEvent(1.0, params.debug_queue_interval));
    }
    if (params.debug_sns)
        std::cout << "3event_queue.size() " << event_queue.size() << "\n";

    while (event_queue.size() > 0) {
        Event *ev = event_queue.top();
        event_queue.pop();
        current_time = ev->time;
        if (start_time < 0) {
            start_time = current_time;
        }
        if (ev->cancelled) {
            delete ev; //TODO: Smarter
            continue;
        }
        ev->process_event();

        if(last_evt_type == ev->type && last_evt_type != 9)
            same_evt_count++;
        else
            same_evt_count = 0;

        last_evt_type = ev->type;
    
        // if(same_evt_count > 100000){
        //     std::cout << "Ended event dead loop. Type:" << last_evt_type << "\n";
        //     break;
        // }
        if(params.print_max_min_fairness && get_current_time() > 1.2) {
            for(int i = 0; i < topology->hosts.size(); i++) {
                ((RufHost*)topology->hosts[i])->print_max_min_fairness();
            }
            assert(false);
        }

        delete ev;
        if (total_finished_flows >= params.num_flows_to_run){
            print_queueing_data();
            return;
        }
    }
}

void print_queueing_data(){
    if (params.flow_type == SNS_FLOW) {
        // Use fct_file with .queueing extension since queuing_file parameter doesn't exist
        static std::ofstream queueing_file;
        static bool queueing_file_initialized = false;
        
        if (!queueing_file_initialized) {
            std::string queueing_filename = params.queuing_file_2;
            queueing_file.open(queueing_filename);
            if (queueing_file.is_open()) {
                // Write header to the file
                queueing_file << "# time flow_id sns_msg_id packet_type value" << std::endl;
                queueing_file_initialized = true;
            }
        }
        
        if (queueing_file.is_open()) {
            // Additional safety check: verify flow is not corrupted
            
            
            // Cast flow to SnsFlow to access queueing data
                try {
                    const auto& queueing_times = params.specific_queueing_packets;
                    if (!queueing_times.empty()) {
                        for (const auto& entry : queueing_times) {
                            // Ensure the entry has the expected number of elements
                            if (entry.size() >= 3) {
                                // Format: flow_id sns_msg_id packet_type value
                                queueing_file << std::fixed << std::setprecision(6)
                                            << static_cast<double>(entry[0]) << " "  // time
                                             << static_cast<int>(entry[1]) << " "  // queue_id
                                             << static_cast<int>(entry[2]) << " "  // Flow id
                                             << static_cast<int>(entry[3]) << " "  // msg id
                                             << static_cast<double>(entry[4]) << " "  // type
                                             << static_cast<double>(entry[5]) << " "  // value (0=SIM, 1=DATA)
                                             <<  std::endl;  // queuing_delay_microseconds (as double)
                                             
                            }
                        }
                        queueing_file.flush();
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Error: FlowFinishedEvent::process_event() - exception while accessing queueing data: " ;
                } catch (...) {
                    std::cerr << "Error: FlowFinishedEvent::process_event() - unknown exception while accessing queueing data ";
                }
            }
        }
    }

extern void run_experiment(int argc, char** argv, uint32_t exp_type);

int main (int argc, char ** argv) {
    time_t start_time;
    time(&start_time);

    //srand(time(NULL));
    srand(0);
    std::cout.precision(15);

    uint32_t exp_type = atoi(argv[1]);
    switch (exp_type) {
        case GEN_ONLY:
        case DEFAULT_EXP:
            run_experiment(argc, argv, exp_type);
            break;
        default:
            assert(false);
    }

    time_t end_time;
    time(&end_time);
    double duration = difftime(end_time, start_time);
    cout << currentDateTime() << " Simulator ended. Execution time: " << duration << " seconds\n";
}

