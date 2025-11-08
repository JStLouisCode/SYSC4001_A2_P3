/**
 * @file interrupts.cpp
 * @authors
 * Jared St.Louis and Yuvraj Bains
 *  
 * runs the main simulator for fork/exec system calls.
 * It handles process creation, memory allocation, and program loading 
 * while keeping track of timing and system state. 
 */

#include <interrupts.hpp>

// PID counter to assign unique IDs to processes
int next_pid = 1;

/**
 * 
 * Handles CPU bursts, SYSCALLs, END_IO, FORK, and EXEC calls.
 * Forks create child processes and exec replaces the current process code.
 * 
 * @param trace_file  vector of trace lines
 * @param time        current simulation time
 * @param vectors     interrupt vectors
 * @param delays      ISR delays
 * @param external_files list of program files with sizes
 * @param current     current process PCB
 * @param wait_queue  list of waiting PCBs
 * 
 * @return tuple with execution log, system status, and updated time
 */
std::tuple<std::string, std::string, int> simulate_trace(
    std::vector<std::string> trace_file, 
    int time, 
    std::vector<std::string> vectors, 
    std::vector<int> delays, 
    std::vector<external_file> external_files, 
    PCB current, 
    std::vector<PCB> wait_queue) {

    std::string execution = "";
    std::string system_status = "";
    int current_time = time;

    // Go through each line of the trace file
    for (size_t i = 0; i < trace_file.size(); i++) {
        auto trace = trace_file[i];
        auto [activity, duration_intr, program_name] = parse_trace(trace);

        if (activity == "CPU") {
            // CPU burst simulation
            execution += std::to_string(current_time) + ", " +
                        std::to_string(duration_intr) + ", CPU Burst\n";
            current_time += duration_intr;

        } else if (activity == "SYSCALL") {
            // Handle SYSCALL interrupt
            auto [intr, time] = intr_boilerplate(current_time, duration_intr, 10, vectors);
            execution += intr;
            current_time = time;

            execution += std::to_string(current_time) + ", " +
                        std::to_string(delays[duration_intr]) + ", SYSCALL ISR\n";
            current_time += delays[duration_intr];

            execution += std::to_string(current_time) + ", 1, IRET\n";
            current_time += 1;

        } else if (activity == "END_IO") {
            // Handle END_IO interrupt
            auto [intr, time] = intr_boilerplate(current_time, duration_intr, 10, vectors);
            current_time = time;
            execution += intr;

            execution += std::to_string(current_time) + ", " +
                        std::to_string(delays[duration_intr]) + ", ENDIO ISR\n";
            current_time += delays[duration_intr];

            execution += std::to_string(current_time) + ", 1, IRET\n";
            current_time += 1;

        } else if (activity == "FORK") {
            // Standard FORK (vector 2)
            auto [intr, time] = intr_boilerplate(current_time, 2, 10, vectors);
            execution += intr;
            current_time = time;

            // Clone PCB for child process
            execution += std::to_string(current_time) + ", " +
                        std::to_string(duration_intr) + ", cloning the PCB\n";
            current_time += duration_intr;

            execution += std::to_string(current_time) + ", 0, scheduler called\n";
            execution += std::to_string(current_time) + ", 1, IRET\n";
            current_time += 1;

            // Create child PCB (inherits parent info)
            PCB child(next_pid++, current.PID, current.program_name, current.size, current.partition_number);

            // Parent waits while child runs
            wait_queue.push_back(current);

            // Snapshot system state
            system_status += "time: " + std::to_string(current_time) + 
                             "; current trace: FORK, " + std::to_string(duration_intr) + "\n";
            system_status += print_PCB(child, wait_queue);

            // Extract child trace section
            std::vector<std::string> child_trace;
            bool skip = true;
            bool exec_flag = false;
            int parent_index = 0;

            for (size_t j = i + 1; j < trace_file.size(); j++) {
                auto [_activity, _duration, _pn] = parse_trace(trace_file[j]);

                if (skip && _activity == "IF_CHILD") {
                    skip = false;
                    continue;
                } else if (_activity == "IF_PARENT") {
                    skip = true;
                    parent_index = j;
                    if (exec_flag) break;
                } else if (skip && _activity == "ENDIF") {
                    skip = false;
                    continue;
                } else if (!skip && _activity == "EXEC") {
                    skip = true;
                    child_trace.push_back(trace_file[j]);
                    exec_flag = true;
                }

                if (!skip) child_trace.push_back(trace_file[j]);
            }

            // Run the child recursively
            auto [child_exec, child_status, new_time] = simulate_trace(
                child_trace,
                current_time,
                vectors,
                delays,
                external_files,
                child,
                std::vector<PCB>() // child starts with no waiting processes
            );

            execution += child_exec;
            system_status += child_status;
            current_time = new_time;

            // Free child memory when done
            free_memory(&child);

            // Continue parent trace
            i = parent_index;

        } else if (activity == "EXEC") {
            // Standard EXEC (vector 3)
            auto [intr, time] = intr_boilerplate(current_time, 3, 10, vectors);
            current_time = time;
            execution += intr;

            // Load new program info
            unsigned int program_size = get_size(program_name, external_files);

            execution += std::to_string(current_time) + ", " +
                        std::to_string(duration_intr) + ", Program is " +
                        std::to_string(program_size) + " Mb large\n";
            current_time += duration_intr;

            // Simulate loading
            int load_time = program_size * 15;
            execution += std::to_string(current_time) + ", " +
                        std::to_string(load_time) + ", loading program into memory\n";
            current_time += load_time;

            // Replace memory and update PCB
            free_memory(&current);
            current.program_name = program_name;
            current.size = program_size;

            if (!allocate_memory(&current))
                std::cerr << "ERROR! Memory allocation failed for " << program_name << std::endl;

            // Random small delays
            int mark_time = (rand() % 10) + 1;
            execution += std::to_string(current_time) + ", " +
                        std::to_string(mark_time) + ", marking partition as occupied\n";
            current_time += mark_time;

            int update_time = (rand() % 10) + 1;
            execution += std::to_string(current_time) + ", " +
                        std::to_string(update_time) + ", updating PCB\n";
            current_time += update_time;

            execution += std::to_string(current_time) + ", 0, scheduler called\n";
            execution += std::to_string(current_time) + ", 1, IRET\n";
            current_time += 1;

            // Snapshot after EXEC
            system_status += "time: " + std::to_string(current_time) + 
                             "; current trace: EXEC " + program_name + ", " + 
                             std::to_string(duration_intr) + "\n";
            system_status += print_PCB(current, wait_queue);

            // Load new program trace file
            std::ifstream exec_trace_file(program_name + ".txt");
            if (!exec_trace_file.is_open()) {
                std::cerr << "ERROR! Could not open " << program_name << ".txt" << std::endl;
                break;
            }

            std::vector<std::string> exec_traces;
            std::string exec_trace;
            while (std::getline(exec_trace_file, exec_trace))
                exec_traces.push_back(exec_trace);
            exec_trace_file.close();

            // Recursively run the new program
            auto [exec_exec, exec_status, final_time] = simulate_trace(
                exec_traces,
                current_time,
                vectors,
                delays,
                external_files,
                current,
                wait_queue
            );

            execution += exec_exec;
            system_status += exec_status;
            current_time = final_time;

            // EXEC replaces process, stop old trace
            break;
        }
    }

    return {execution, system_status, current_time};
}

/**
 * 
 * Initializes simulation, sets up the first process (init), 
 * loads trace files, and outputs results to text files.
 */
int main(int argc, char** argv) {
    srand(time(NULL)); // random seed for delays

    auto [vectors, delays, external_files] = parse_args(argc, argv);
    std::ifstream input_file(argv[1]);

    print_external_files(external_files); // verify inputs

    PCB current(0, -1, "init", 1, -1);
    if (!allocate_memory(&current)) {
        std::cerr << "ERROR! Memory allocation failed for init!" << std::endl;
        return 1;
    }

    std::vector<PCB> wait_queue;

    // Load trace file into vector
    std::vector<std::string> trace_file;
    std::string trace;
    while (std::getline(input_file, trace))
        trace_file.push_back(trace);
    input_file.close();

    // Start simulation
    auto [execution, system_status, _] = simulate_trace(
        trace_file,
        0,
        vectors,
        delays,
        external_files,
        current,
        wait_queue
    );

    // Output results
    write_output(execution, "execution.txt");
    write_output(system_status, "system_status.txt");

    std::cout << "\nSimulation complete!" << std::endl;
    std::cout << "Check execution.txt and system_status.txt for results." << std::endl;

    return 0;
}
