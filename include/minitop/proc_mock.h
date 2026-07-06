#ifndef MINITOP_PROC_MOCK_H
#define MINITOP_PROC_MOCK_H

#include <string>

namespace minitop {

// Simulates a Linux /proc directory on Windows for local development.
// It queries the Windows API for active processes and system metrics, 
// formatting and writing them to standard Linux /proc layouts.
class ProcMock {
public:
    explicit ProcMock(const std::string& mock_dir);
    ~ProcMock();

    // Starts the background update thread
    void Start();

    // Stops the background update thread and cleans up the mock directory
    void Stop();

    // Performs a single update cycle (creates files/directories)
    void Update();

    // Static entry point for the Windows thread
    #ifdef _WIN32
    static unsigned long __stdcall ThreadEntry(void* param);
    #endif

private:
    void RunLoop();
    void CleanMockDir();

    std::string mock_dir_;
    bool running_;
    void* thread_handle_;
};

} // namespace minitop

#endif // MINITOP_PROC_MOCK_H
