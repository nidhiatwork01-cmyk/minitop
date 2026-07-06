#include <iostream>
#include <cassert>
#include <vector>
#include "minitop/proc_parser.h"

#ifdef _WIN32
#include "minitop/proc_mock.h"
const std::string TEST_DIR = ".minitop_test_proc";
#else
const std::string TEST_DIR = "/proc";
#endif

void TestMemoryParsing() {
    std::cout << "[Test] Testing Memory Parsing...\n";
    minitop::SystemMemInfo mem{0, 0, 0};
    bool success = minitop::ParseSystemMemInfo(TEST_DIR, mem);
    assert(success);
    assert(mem.total_kb > 0);
    assert(mem.free_kb > 0);
    assert(mem.available_kb > 0);
    std::cout << "[Test] Memory Parsing Passed. Total: " << mem.total_kb << " kB.\n";
}

void TestCpuParsing() {
    std::cout << "[Test] Testing System CPU Parsing...\n";
    minitop::SystemCpuTime cpu{0, 0};
    bool success = minitop::ParseSystemCpuTime(TEST_DIR, cpu);
    assert(success);
    assert(cpu.total > 0);
    std::cout << "[Test] CPU Parsing Passed. Total ticks: " << cpu.total << ".\n";
}

void TestProcessParsing() {
    std::cout << "[Test] Testing Process Listing & Stats...\n";
    std::vector<int> pids = minitop::GetRunningPids(TEST_DIR);
    assert(!pids.empty());
    
    bool parsed_at_least_one = false;
    for (int pid : pids) {
        minitop::ProcessCpuTime cpu{0, 0};
        std::string name;
        double mem_mb = 0.0;
        if (minitop::ParseProcessCpuTime(TEST_DIR, pid, cpu) &&
            minitop::ParseProcessStatus(TEST_DIR, pid, name, mem_mb)) {
            assert(!name.empty());
            parsed_at_least_one = true;
            break;
        }
    }
    assert(parsed_at_least_one);
    std::cout << "[Test] Process Parsing Passed.\n";
}

int main() {
#ifdef _WIN32
    // Setup temporary mock directory for testing
    minitop::ProcMock mock(TEST_DIR);
    mock.Update(); 
#endif

    try {
        TestMemoryParsing();
        TestCpuParsing();
        TestProcessParsing();
        std::cout << "[Success] All unit tests passed!\n";
    } catch (...) {
        std::cerr << "[Error] A test assertion failed!\n";
#ifdef _WIN32
        mock.Stop();
#endif
        return 1;
    }

#ifdef _WIN32
    mock.Stop();
#endif
    return 0;
}
