#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600 // Enable GetSystemTimes
#endif
#include "minitop/proc_mock.h"
#include <windows.h>
#include <psapi.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>

namespace minitop {

// Helper to convert FILETIME to unsigned long long (100-nanosecond units)
static unsigned long long FileTimeToUll(const FILETIME& ft) {
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return uli.QuadPart;
}

ProcMock::ProcMock(const std::string& mock_dir)
    : mock_dir_(mock_dir), running_(false), thread_handle_(nullptr) {}

ProcMock::~ProcMock() {
    Stop();
}

unsigned long __stdcall ProcMock::ThreadEntry(void* param) {
    static_cast<ProcMock*>(param)->RunLoop();
    return 0;
}

void ProcMock::Start() {
    if (running_) return;
    running_ = true;
    
    // Create directory first
    CreateDirectoryA(mock_dir_.c_str(), NULL);
    
    thread_handle_ = CreateThread(NULL, 0, &ProcMock::ThreadEntry, this, 0, NULL);
}

void ProcMock::Stop() {
    if (!running_) return;
    running_ = false;
    if (thread_handle_ != nullptr) {
        WaitForSingleObject(thread_handle_, INFINITE);
        CloseHandle(thread_handle_);
        thread_handle_ = nullptr;
    }
    CleanMockDir();
}

void ProcMock::CleanMockDir() {
    // Delete files in directory and then directory itself
    std::string search_path = mock_dir_ + "\\*";
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search_path.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(fd.cFileName, ".") != 0 && strcmp(fd.cFileName, "..") != 0) {
                std::string file_path = mock_dir_ + "\\" + fd.cFileName;
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    // It's a PID folder, delete files inside it first
                    std::string sub_search = file_path + "\\*";
                    WIN32_FIND_DATAA sub_fd;
                    HANDLE hSubFind = FindFirstFileA(sub_search.c_str(), &sub_fd);
                    if (hSubFind != INVALID_HANDLE_VALUE) {
                        do {
                            if (strcmp(sub_fd.cFileName, ".") != 0 && strcmp(sub_fd.cFileName, "..") != 0) {
                                std::string sub_file = file_path + "\\" + sub_fd.cFileName;
                                DeleteFileA(sub_file.c_str());
                            }
                        } while (FindNextFileA(hSubFind, &sub_fd));
                        FindClose(hSubFind);
                    }
                    RemoveDirectoryA(file_path.c_str());
                } else {
                    DeleteFileA(file_path.c_str());
                }
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
    RemoveDirectoryA(mock_dir_.c_str());
}

void ProcMock::Update() {
    CreateDirectoryA(mock_dir_.c_str(), NULL);

    // 1. Write meminfo
    MEMORYSTATUSEX mem_status;
    mem_status.dwLength = sizeof(mem_status);
    if (GlobalMemoryStatusEx(&mem_status)) {
        std::ofstream mem_file(mock_dir_ + "/meminfo");
        if (mem_file.is_open()) {
            mem_file << "MemTotal:       " << (mem_status.ullTotalPhys / 1024) << " kB\n";
            mem_file << "MemFree:        " << (mem_status.ullAvailPhys / 1024) << " kB\n";
            mem_file << "MemAvailable:   " << (mem_status.ullAvailPhys / 1024) << " kB\n";
        }
    }

    // 2. Write system stat (CPU times)
    FILETIME idle_ft, kernel_ft, user_ft;
    if (GetSystemTimes(&idle_ft, &kernel_ft, &user_ft)) {
        unsigned long long idle = FileTimeToUll(idle_ft);
        unsigned long long kernel = FileTimeToUll(kernel_ft);
        unsigned long long user = FileTimeToUll(user_ft);
        
        // Windows kernel time includes idle time, so system CPU time is kernel - idle
        unsigned long long system = (kernel > idle) ? (kernel - idle) : 0;
        
        std::ofstream stat_file(mock_dir_ + "/stat");
        if (stat_file.is_open()) {
            // Write cpu user nice system idle iowait irq softirq steal guest guest_nice
            // We scale filetime (100ns units) down to jiffies/ticks (e.g. / 10000) to keep numbers reasonable
            unsigned long long scale = 10000;
            stat_file << "cpu  " 
                      << (user / scale) << " 0 " 
                      << (system / scale) << " " 
                      << (idle / scale) << " 0 0 0 0 0 0\n";
        }
    }

    // 3. Write processes
    DWORD process_ids[1024];
    DWORD bytes_returned;
    if (EnumProcesses(process_ids, sizeof(process_ids), &bytes_returned)) {
        int count = bytes_returned / sizeof(DWORD);
        
        int written_count = 0;
        for (int i = 0; i < count; ++i) {
            if (written_count >= 60) {
                break;
            }

            DWORD pid = process_ids[i];
            if (pid == 0) continue;

            HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
            if (hProcess == NULL) {
                // Try with lesser rights
                hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            }

            if (hProcess != NULL) {
                std::string proc_name = "unknown";
                char img_path[MAX_PATH] = "";
                if (GetProcessImageFileNameA(hProcess, img_path, sizeof(img_path)) > 0) {
                    std::string path_str(img_path);
                    size_t last_slash = path_str.find_last_of("\\/");
                    if (last_slash != std::string::npos) {
                        proc_name = path_str.substr(last_slash + 1);
                    } else {
                        proc_name = path_str;
                    }
                } else {
                    char mod_name[MAX_PATH] = "";
                    if (GetModuleBaseNameA(hProcess, NULL, mod_name, sizeof(mod_name)) > 0) {
                        proc_name = mod_name;
                    }
                }

                PROCESS_MEMORY_COUNTERS pmc;
                unsigned long long rss_kb = 0;
                if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) {
                    rss_kb = pmc.WorkingSetSize / 1024;
                }

                FILETIME create_ft, exit_ft, kernel_ft, user_ft;
                unsigned long long utime_ticks = 0;
                unsigned long long stime_ticks = 0;
                if (GetProcessTimes(hProcess, &create_ft, &exit_ft, &kernel_ft, &user_ft)) {
                    unsigned long long scale = 10000;
                    utime_ticks = FileTimeToUll(user_ft) / scale;
                    stime_ticks = FileTimeToUll(kernel_ft) / scale;
                }

                CloseHandle(hProcess);

                // Create folder for the process
                std::string proc_path = mock_dir_ + "/" + std::to_string(pid);
                CreateDirectoryA(proc_path.c_str(), NULL);

                // Write [pid]/status
                std::ofstream status_file(proc_path + "/status");
                if (status_file.is_open()) {
                    status_file << "Name:\t" << proc_name << "\n";
                    status_file << "VmRSS:\t" << rss_kb << " kB\n";
                }

                // Write [pid]/stat
                // 14: utime, 15: stime
                std::ofstream stat_file(proc_path + "/stat");
                if (stat_file.is_open()) {
                    stat_file << pid << " (" << proc_name << ") S 0 0 0 0 0 0 0 0 0 0 "
                              << utime_ticks << " " << stime_ticks << " 0 0 0 0 0 0 0 0 0\n";
                }

                written_count++;
            }
        }
    }
}

void ProcMock::RunLoop() {
    while (running_) {
        Update();
        Sleep(500);
    }
}

} // namespace minitop
#endif
