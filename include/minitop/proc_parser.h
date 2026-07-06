#ifndef MINITOP_PROC_PARSER_H
#define MINITOP_PROC_PARSER_H

#include <string>
#include <vector>
#include <unordered_map>

namespace minitop {

struct ProcessInfo {
    int pid;
    std::string name;
    double cpu_percent;
    double memory_mb; // RSS in MB
};

struct SystemMemInfo {
    unsigned long long total_kb;
    unsigned long long free_kb;
    unsigned long long available_kb;
};

// Process-specific CPU time snapshot
struct ProcessCpuTime {
    unsigned long long utime;
    unsigned long long stime;
};

// System-wide CPU time snapshot
struct SystemCpuTime {
    unsigned long long active;
    unsigned long long total;
};

// Reads system-wide memory information from a directory path (e.g., "/proc")
bool ParseSystemMemInfo(const std::string& proc_dir, SystemMemInfo& mem_info);

// Reads system-wide CPU time from proc_dir/stat
bool ParseSystemCpuTime(const std::string& proc_dir, SystemCpuTime& cpu_time);

// Reads process-specific CPU time for a given PID from proc_dir/[pid]/stat
bool ParseProcessCpuTime(const std::string& proc_dir, int pid, ProcessCpuTime& proc_cpu);

// Reads process-specific RSS memory and name from proc_dir/[pid]/status
bool ParseProcessStatus(const std::string& proc_dir, int pid, std::string& name, double& memory_mb);

// Returns list of all running process PIDs in proc_dir
std::vector<int> GetRunningPids(const std::string& proc_dir);

// Compiles full list of process statistics by taking two snapshots separated by an interval
std::vector<ProcessInfo> GetProcessSnapshot(const std::string& proc_dir, int interval_ms);

} // namespace minitop

#endif // MINITOP_PROC_PARSER_H
