#include "minitop/proc_parser.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <dirent.h>
#include <sys/types.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <thread>
#include <chrono>
#endif

namespace minitop {

bool ParseSystemMemInfo(const std::string& proc_dir, SystemMemInfo& mem_info) {
    std::ifstream file(proc_dir + "/meminfo");
    if (!file.is_open()) return false;

    std::string line;
    mem_info.total_kb = 0;
    mem_info.free_kb = 0;
    mem_info.available_kb = 0;

    int fields_found = 0;
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string key;
        unsigned long long val;
        std::string unit;
        if (ss >> key >> val >> unit) {
            if (key == "MemTotal:") {
                mem_info.total_kb = val;
                fields_found++;
            } else if (key == "MemFree:") {
                mem_info.free_kb = val;
                fields_found++;
            } else if (key == "MemAvailable:") {
                mem_info.available_kb = val;
                fields_found++;
            }
        }
    }
    return fields_found > 0;
}

bool ParseSystemCpuTime(const std::string& proc_dir, SystemCpuTime& cpu_time) {
    std::ifstream file(proc_dir + "/stat");
    if (!file.is_open()) return false;

    std::string line;
    if (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string label;
        ss >> label;
        if (label == "cpu") {
            unsigned long long u, n, s, id, io, irq, sirq, steal, guest, guest_nice;
            u = n = s = id = io = irq = sirq = steal = guest = guest_nice = 0;
            if (ss >> u >> n >> s >> id >> io >> irq >> sirq >> steal) {
                cpu_time.total = u + n + s + id + io + irq + sirq + steal;
                cpu_time.active = cpu_time.total - (id + io);
                return true;
            }
        }
    }
    return false;
}

bool ParseProcessCpuTime(const std::string& proc_dir, int pid, ProcessCpuTime& proc_cpu) {
    std::ifstream file(proc_dir + "/" + std::to_string(pid) + "/stat");
    if (!file.is_open()) return false;

    std::string line;
    if (std::getline(file, line)) {
        size_t last_paren = line.rfind(')');
        if (last_paren != std::string::npos) {
            std::string rest = line.substr(last_paren + 2); // skip ") "
            std::stringstream ss(rest);
            std::string state;
            int ppid, pgrp, session, tty_nr, tpgid;
            unsigned int flags;
            unsigned long minflt, cminflt, majflt, cmajflt;
            unsigned long long utime, stime;
            
            utime = stime = 0;
            if (ss >> state >> ppid >> pgrp >> session >> tty_nr >> tpgid >> flags 
                   >> minflt >> cminflt >> majflt >> cmajflt >> utime >> stime) {
                proc_cpu.utime = utime;
                proc_cpu.stime = stime;
                return true;
            }
        }
    }
    return false;
}

bool ParseProcessStatus(const std::string& proc_dir, int pid, std::string& name, double& memory_mb) {
    std::ifstream file(proc_dir + "/" + std::to_string(pid) + "/status");
    if (!file.is_open()) return false;

    std::string line;
    name = "";
    memory_mb = 0.0;
    bool found_name = false;

    while (std::getline(file, line)) {
        if (line.rfind("Name:", 0) == 0) {
            size_t pos = line.find_first_not_of(" \t", 5);
            if (pos != std::string::npos) {
                name = line.substr(pos);
                found_name = true;
            }
        } else if (line.rfind("VmRSS:", 0) == 0) {
            std::stringstream ss(line.substr(6));
            unsigned long long rss_kb;
            if (ss >> rss_kb) {
                memory_mb = rss_kb / 1024.0;
            }
        }
    }
    return found_name;
}

std::vector<int> GetRunningPids(const std::string& proc_dir) {
    std::vector<int> pids;
    DIR* dir = opendir(proc_dir.c_str());
    if (dir != nullptr) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string name(entry->d_name);
            // Check if name is all digits
            if (!name.empty() && std::all_of(name.begin(), name.end(), ::isdigit)) {
                pids.push_back(std::stoi(name));
            }
        }
        closedir(dir);
    }
    return pids;
}

static void CrossPlatformSleep(int ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
#endif
}

std::vector<ProcessInfo> GetProcessSnapshot(const std::string& proc_dir, int interval_ms) {
    SystemCpuTime sys_cpu1{0, 0};
    std::unordered_map<int, ProcessCpuTime> proc_cpu1;
    
    // Take first snapshot
    ParseSystemCpuTime(proc_dir, sys_cpu1);
    std::vector<int> pids1 = GetRunningPids(proc_dir);
    for (int pid : pids1) {
        ProcessCpuTime pct{0, 0};
        if (ParseProcessCpuTime(proc_dir, pid, pct)) {
            proc_cpu1[pid] = pct;
        }
    }

    // Sleep for the interval
    CrossPlatformSleep(interval_ms);

    // Take second snapshot
    SystemCpuTime sys_cpu2{0, 0};
    ParseSystemCpuTime(proc_dir, sys_cpu2);
    
    std::vector<int> pids2 = GetRunningPids(proc_dir);
    std::vector<ProcessInfo> snapshot;
    
    unsigned long long total_diff = sys_cpu2.total - sys_cpu1.total;

    for (int pid : pids2) {
        ProcessCpuTime pct2{0, 0};
        if (ParseProcessCpuTime(proc_dir, pid, pct2)) {
            std::string name;
            double mem_mb = 0.0;
            ParseProcessStatus(proc_dir, pid, name, mem_mb);

            double cpu_pct = 0.0;
            if (total_diff > 0) {
                auto it = proc_cpu1.find(pid);
                if (it != proc_cpu1.end()) {
                    unsigned long long proc_diff = (pct2.utime + pct2.stime) - (it->second.utime + it->second.stime);
                    cpu_pct = (static_cast<double>(proc_diff) / total_diff) * 100.0;
                }
            }

            // Clean name formatting if contains trailing whitespace/newlines
            while (!name.empty() && (name.back() == '\r' || name.back() == '\n' || name.back() == ' ')) {
                name.pop_back();
            }

            snapshot.push_back({pid, name, cpu_pct, mem_mb});
        }
    }
    
    return snapshot;
}

} // namespace minitop
