#include <iostream>
#include <iomanip>
#include <algorithm>
#include <vector>
#include <csignal>
#include <getopt.h>
#include <fstream>
#include "minitop/proc_parser.h"

// Load configuration from file
void LoadConfig(const std::string& filename, double& delay, std::string& sort, std::string& mode) {
    std::ifstream file(filename);
    if (!file.is_open()) return;
    std::string line;
    while (std::getline(file, line)) {
        size_t comment = line.find('#');
        if (comment != std::string::npos) {
            line = line.substr(0, comment);
        }
        line.erase(std::remove_if(line.begin(), line.end(), ::isspace), line.end());
        if (line.empty()) continue;
        size_t eq = line.find('=');
        if (eq != std::string::npos) {
            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);
            if (key == "delay") {
                try { delay = std::stod(val); } catch (...) {}
            } else if (key == "sort") {
                sort = val;
            } else if (key == "mode") {
                mode = val;
            }
        }
    }
}
#include "minitop/thread_compat.h"
#include "minitop/keyboard.h"
#include "minitop/task_scheduler.h"

#ifdef _WIN32
#include <windows.h>
#include "minitop/proc_mock.h"
const std::string PROC_DIR = ".minitop_proc";
#else
#include <thread>
#include <chrono>
const std::string PROC_DIR = "/proc";
#endif

// =================================================================
// Shared Data Structures & Synchronization
// =================================================================
struct SharedData {
    std::vector<minitop::ProcessInfo> processes;
    minitop::SystemMemInfo mem_info;
    minitop::SrwMutex mutex;
    bool updated;
};

// Global shared variables
SharedData g_shared{ {}, {0,0,0}, {}, false };
volatile std::sig_atomic_t keep_running = 1;
std::string g_sort_by = "cpu";
int g_target_pid = -1;
int g_delay_ms = 1000;

// Signal and Ctrl handlers
void SignalHandler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        keep_running = 0;
    }
}

#ifdef _WIN32
BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT || ctrlType == CTRL_CLOSE_EVENT) {
        keep_running = 0;
        return TRUE;
    }
    return FALSE;
}
#endif

#ifndef _WIN32
volatile std::sig_atomic_t screen_resized = 0;
void WinchHandler(int signum) {
    screen_resized = 1;
}
#endif

// =================================================================
// Worker Thread: Polls /proc or simulated /proc
// =================================================================
#ifdef _WIN32
unsigned long __stdcall PollThreadFunc(void* param) {
#else
void* PollThreadFunc(void* param) {
#endif
    while (keep_running) {
        // Collect snapshots (sample over 100ms or 20% of delay to keep UI snappy)
        int sample_ms = std::min(150, std::max(50, g_delay_ms / 5));
        std::vector<minitop::ProcessInfo> local_procs = minitop::GetProcessSnapshot(PROC_DIR, sample_ms);
        minitop::SystemMemInfo local_mem{0, 0, 0};
        minitop::ParseSystemMemInfo(PROC_DIR, local_mem);

        // Update shared structure under lock
        {
            // --- DELIBERATE RACE CONDITION ---
            // For interviewing/learning: If you comment out this LockGuard line,
            // ThreadSanitizer or Helgrind will instantly flag a data race because
            // the main thread reads 'g_shared' while this thread is writing to it!
            minitop::SrwLockGuard lock(g_shared.mutex);
            
            g_shared.processes = local_procs;
            g_shared.mem_info = local_mem;
            g_shared.updated = true;
        }

        // Sleep for the remainder of the delay
        int sleep_ms = g_delay_ms - sample_ms;
        if (sleep_ms > 0) {
#ifdef _WIN32
            Sleep(sleep_ms);
#else
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
#endif
        }
    }
    return 0;
}

// =================================================================
// Help Screen
// =================================================================
void PrintHelp(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " [options]\n\n"
              << "Options:\n"
              << "  -m, --mode <monitor|scheduler>  Execution mode (default: monitor)\n"
              << "  -d <seconds>                    Refresh delay in seconds (default: 1.0)\n"
              << "  -p <pid>                        Monitor only a single Process ID\n"
              << "  -s, --sort <cpu|mem>            Sort columns by cpu or mem (default: cpu)\n"
              << "  -h, --help                      Show this help message\n\n"
              << "Interactive Controls (Monitor Mode):\n"
              << "  'c'  Sort by CPU%\n"
              << "  'm'  Sort by Memory (RSS)\n"
              << "  'q'  Quit cleanly\n";
}

// =================================================================
// Main Runner
// =================================================================
int main(int argc, char* argv[]) {
    double delay_sec = 1.0;
    std::string mode = "monitor";

    // Load config values first (command line flags will override them)
    LoadConfig("minitop.conf", delay_sec, g_sort_by, mode);

    // Parse options
    static struct option long_options[] = {
        {"mode", required_argument, 0, 'm'},
        {"sort", required_argument, 0, 's'},
        {"help", no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "d:p:s:m:h", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'm':
                mode = optarg;
                std::transform(mode.begin(), mode.end(), mode.begin(), ::tolower);
                if (mode != "monitor" && mode != "scheduler") {
                    std::cerr << "Invalid mode: " << optarg << ". Choose 'monitor' or 'scheduler'.\n";
                    return 1;
                }
                break;
            case 'd':
                try {
                    delay_sec = std::stod(optarg);
                    if (delay_sec < 0.1) delay_sec = 0.1;
                } catch (...) {
                    std::cerr << "Invalid delay value: " << optarg << "\n";
                    return 1;
                }
                break;
            case 'p':
                try {
                    g_target_pid = std::stoi(optarg);
                } catch (...) {
                    std::cerr << "Invalid PID value: " << optarg << "\n";
                    return 1;
                }
                break;
            case 's':
                g_sort_by = optarg;
                std::transform(g_sort_by.begin(), g_sort_by.end(), g_sort_by.begin(), ::tolower);
                if (g_sort_by != "cpu" && g_sort_by != "mem") {
                    std::cerr << "Sort column must be either 'cpu' or 'mem'.\n";
                    return 1;
                }
                break;
            case 'h':
                PrintHelp(argv[0]);
                return 0;
            default:
                PrintHelp(argv[0]);
                return 1;
        }
    }

    g_delay_ms = static_cast<int>(delay_sec * 1000.0);

    // Setup signal / ctrl handlers
#ifdef _WIN32
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
#else
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);
    std::signal(SIGWINCH, WinchHandler);
#endif

#ifdef _WIN32
    // Setup Mock /proc folder on Windows
    std::cout << "[System] Starting local process simulator on Windows...\n";
    minitop::ProcMock mock(PROC_DIR);
    mock.Start();
    // Wait for the simulator to write its first batch of files
    Sleep(600);
#endif

    if (mode == "scheduler") {
        // =================================================================
        // MODE 2: Multi-threaded Task Scheduler
        // =================================================================
        std::cout << "\n=================================================================\n";
        std::cout << "  MINITOP MULTI-THREADED TASK SCHEDULER MODE\n";
        std::cout << "=================================================================\n";
        std::cout << "[Scheduler] Initializing thread pool...\n";

        minitop::TaskScheduler scheduler;
        scheduler.Start();

        std::cout << "[Scheduler] Thread pool active. Scheduling background tasks...\n";

        // Schedule a repeating health check task (re-queues itself!)
        std::function<void()> health_check;
        health_check = [&scheduler, &health_check]() {
            std::cout << "[Job Runner] [" << minitop::TaskScheduler::GetCurrentTimeMs() 
                      << " ms] TASK: System Health Check -> CPU: OK, Memory: OK\n" << std::flush;
            // Schedule itself again in 3 seconds
            scheduler.Schedule("HealthCheck", health_check, 3000, 10);
        };

        // Schedule a repeating log rotation task
        std::function<void()> log_rotation;
        log_rotation = [&scheduler, &log_rotation]() {
            std::cout << "[Job Runner] [" << minitop::TaskScheduler::GetCurrentTimeMs() 
                      << " ms] TASK: Log Rotation -> Compressed logs/syslog.log.1.gz\n" << std::flush;
            scheduler.Schedule("LogRotation", log_rotation, 8000, 5);
        };

        // Schedule a one-time database backup task
        auto backup_db = []() {
            std::cout << "[Job Runner] [" << minitop::TaskScheduler::GetCurrentTimeMs() 
                      << " ms] TASK: One-time Database Backup -> Completed backup.sql (4.2 MB)\n" << std::flush;
        };

        // Queue them up
        scheduler.Schedule("HealthCheck", health_check, 1000, 10);
        scheduler.Schedule("LogRotation", log_rotation, 2000, 5);
        scheduler.Schedule("OneTimeBackup", backup_db, 5000, 15); // Highest priority, runs once

        std::cout << "[System] Scheduler is running. Press 'q' or [Ctrl+C] to quit.\n";
        std::cout << "[System] Press 't' to dynamically queue a custom job.\n\n";

        minitop::InitKeyboard();
        while (keep_running) {
            if (minitop::KeyPressed()) {
                char ch = minitop::GetChar();
                if (ch == 'q' || ch == 'Q') {
                    keep_running = 0;
                } else if (ch == 't' || ch == 'T') {
                    std::cout << "[Scheduler] Dynamically queuing new custom task with 2-second delay...\n" << std::flush;
                    scheduler.Schedule("DynamicTask", []() {
                        std::cout << "[Job Runner] [" << minitop::TaskScheduler::GetCurrentTimeMs()
                                  << " ms] TASK: Custom Dynamic Task -> Executed successfully!\n" << std::flush;
                    }, 2000, 1);
                }
            }
#ifdef _WIN32
            Sleep(50);
#else
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
#endif
        }
        minitop::RestoreKeyboard();

        std::cout << "[Scheduler] Stopping background thread pool...\n";
        scheduler.Stop();
    } else {
        // =================================================================
        // MODE 1: Multi-threaded Process Monitor
        // =================================================================
        std::cout << "[System] Starting background collector thread...\n";
        void* poll_thread = minitop::SpawnThread(PollThreadFunc, nullptr);

        minitop::InitKeyboard();

        // Hide terminal cursor
        std::cout << "\033[?25l" << std::flush;

        bool force_redraw = true;

        while (keep_running) {
#ifndef _WIN32
            if (screen_resized) {
                std::cout << "\033[2J" << std::flush;
                screen_resized = 0;
                force_redraw = true;
            }
#endif

            // Check keyboard inputs
            if (minitop::KeyPressed()) {
                char ch = minitop::GetChar();
                if (ch == 'q' || ch == 'Q') {
                    keep_running = 0;
                } else if (ch == 'c' || ch == 'C') {
                    g_sort_by = "cpu";
                    force_redraw = true;
                } else if (ch == 'm' || ch == 'M') {
                    g_sort_by = "mem";
                    force_redraw = true;
                }
            }

            bool data_updated = false;
            std::vector<minitop::ProcessInfo> procs;
            minitop::SystemMemInfo mem{0,0,0};

            // Safely fetch data from polling thread
            {
                minitop::SrwLockGuard lock(g_shared.mutex);
                if (g_shared.updated || force_redraw) {
                    procs = g_shared.processes;
                    mem = g_shared.mem_info;
                    g_shared.updated = false;
                    data_updated = true;
                    force_redraw = false;
                }
            }

            // Only redraw if data changed or sorting changed
            if (data_updated && !procs.empty()) {
                // Apply single PID filter
                if (g_target_pid != -1) {
                    std::vector<minitop::ProcessInfo> filtered;
                    for (const auto& p : procs) {
                        if (p.pid == g_target_pid) {
                            filtered.push_back(p);
                            break;
                        }
                    }
                    procs = filtered;
                }

                // Apply Sorting
                if (g_sort_by == "mem") {
                    std::sort(procs.begin(), procs.end(), [](const minitop::ProcessInfo& a, const minitop::ProcessInfo& b) {
                        return a.memory_mb > b.memory_mb;
                    });
                } else {
                    std::sort(procs.begin(), procs.end(), [](const minitop::ProcessInfo& a, const minitop::ProcessInfo& b) {
                        return a.cpu_percent > b.cpu_percent;
                    });
                }

                double total_gb = mem.total_kb / (1024.0 * 1024.0);
                double free_gb = mem.free_kb / (1024.0 * 1024.0);
                double avail_gb = mem.available_kb / (1024.0 * 1024.0);

                // Draw screen using ANSI codes
                std::cout << "\033[H\033[J";
                std::cout << "=================================================================\n";
                std::cout << "  MINITOP LIVE MONITOR (Delay: " << delay_sec << "s, Sort: " << g_sort_by << ")\n";
                std::cout << "=================================================================\n";
                std::cout << "Memory: " << std::fixed << std::setprecision(2) 
                          << total_gb << " GB Total, " 
                          << free_gb << " GB Free, " 
                          << avail_gb << " GB Available\n";
                std::cout << "-----------------------------------------------------------------\n";
                std::cout << std::left << std::setw(8) << "PID" 
                          << std::setw(25) << "NAME" 
                          << std::setw(12) << "CPU%" 
                          << std::setw(12) << "MEM(MB)" << "\n";
                std::cout << "-----------------------------------------------------------------\n";

                size_t limit = (g_target_pid != -1) ? procs.size() : std::min(procs.size(), static_cast<size_t>(20));
                for (size_t i = 0; i < limit; ++i) {
                    const auto& p = procs[i];
                    std::cout << std::left << std::setw(8) << p.pid 
                              << std::setw(25) << (p.name.length() > 22 ? p.name.substr(0, 22) + "..." : p.name) 
                              << std::fixed << std::setprecision(1)
                              << std::setw(12) << p.cpu_percent
                              << std::setw(12) << p.memory_mb << "\n";
                }
                std::cout << "=================================================================\n";
                std::cout << "Interactive Controls: [c] sort CPU | [m] sort MEM | [q] quit cleanly\n" << std::flush;
            }

            // High frequency input polling (every 50ms) to ensure instant key response
#ifdef _WIN32
            Sleep(50);
#else
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
#endif
        }

        minitop::RestoreKeyboard();

        // Restore cursor
        std::cout << "\033[?25h" << std::endl;

        std::cout << "[System] Stopping background collector thread...\n";
        minitop::JoinThread(poll_thread);
    }

#ifdef _WIN32
    std::cout << "[System] Cleaning up mock resources...\n";
    mock.Stop();
#endif

    std::cout << "[System] Shutdown complete. Goodbye!\n";
    return 0;
}
