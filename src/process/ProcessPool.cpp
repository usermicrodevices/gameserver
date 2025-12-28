#include <sys/wait.h>
#include <sys/prctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

#include "../../include/process/ProcessPool.hpp"
#include "../../include/logging/Logger.hpp"

ProcessPool::ProcessPool(int numProcesses)
: numProcesses_(numProcesses) {

    workerPids_.resize(numProcesses_, -1);
    workerPipes_.resize(numProcesses_ * 2, -1); // Two pipes per worker
}

bool ProcessPool::Initialize() {
    if (numProcesses_ <= 0) {
        Logger::Error("Invalid number of processes: {}", numProcesses_);
        return false;
    }

    masterPid_ = getpid();
    SetupSignalHandlers();

    // Create pipes for IPC with workers
    for (int i = 0; i < numProcesses_; ++i) {
        int pipefd[2];
        if (pipe(pipefd) == -1) {
            Logger::Error("Failed to create pipe for worker {}: {}", i, strerror(errno));
            return false;
        }
        workerPipes_[i * 2] = pipefd[0];     // Read from worker
        workerPipes_[i * 2 + 1] = pipefd[1]; // Write to worker

        // Set non-blocking
        fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
        fcntl(pipefd[1], F_SETFL, O_NONBLOCK);
    }

    return true;
}

void ProcessPool::Run() {
    if (!Initialize()) {
        Logger::Critical("Failed to initialize process pool");
        return;
    }

    if (getpid() == masterPid_) {
        MasterProcess();
    } else {
        WorkerProcess(workerId_);
    }
}

void ProcessPool::MasterProcess() {
    Logger::Info("Master process started (PID: {})", getpid());

    // Fork worker processes
    for (int i = 0; i < numProcesses_; ++i) {
        pid_t pid = fork();

        if (pid == 0) {
            // Child process
            workerId_ = i;
            role_ = ProcessRole::WORKER;

            // Close unused pipe ends
            for (int j = 0; j < numProcesses_ * 2; ++j) {
                if (j != workerId_ * 2 + 1) { // Keep write pipe to master
                    close(workerPipes_[j]);
                }
            }

            // Set process name
            std::string processName = "game_worker_" + std::to_string(i);
            prctl(PR_SET_NAME, processName.c_str(), 0, 0, 0);

            Logger::Info("Worker process {} started (PID: {})", i, getpid());

            if (workerMain_) {
                workerMain_(i);
            }

            exit(0);

        } else if (pid > 0) {
            // Parent process
            workerPids_[i] = pid;

            // Close unused pipe end
            close(workerPipes_[i * 2 + 1]);

            // Record worker health
            {
                std::lock_guard<std::mutex> lock(healthMutex_);
                workerHealth_[i] = {pid, time(nullptr)};
            }

        } else {
            Logger::Error("Failed to fork worker {}: {}", i, strerror(errno));
        }
    }

    running_ = true;

    // Monitor worker processes
    while (running_) {
        CleanupDeadWorkers();
        sleep(1);
    }

    // Wait for all workers to exit
    for (int i = 0; i < numProcesses_; ++i) {
        if (workerPids_[i] > 0) {
            kill(workerPids_[i], SIGTERM);
            waitpid(workerPids_[i], nullptr, 0);
        }
    }

    Logger::Info("Master process shutdown complete");
}

void ProcessPool::WorkerProcess(int workerId) {
    this->workerId_ = workerId;

    // Set up signal handlers for worker
    signal(SIGTERM, [](int) { _exit(0); });
    signal(SIGINT, SIG_IGN);

    if (workerMain_) {
        workerMain_(workerId);
    }
}

void ProcessPool::CleanupDeadWorkers() {
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        // Find which worker died
        for (int i = 0; i < numProcesses_; ++i) {
            if (workerPids_[i] == pid) {
                Logger::Warn("Worker {} (PID: {}) died, restarting...", i, pid);

                // Restart worker
                RestartWorker(i);
                break;
            }
        }
    }
}

void ProcessPool::RestartWorker(int workerId) {
    pid_t pid = fork();

    if (pid == 0) {
        // New worker process
        workerId_ = workerId;
        role_ = ProcessRole::WORKER;

        // Reopen pipes
        close(workerPipes_[workerId * 2 + 1]);

        if (workerMain_) {
            workerMain_(workerId);
        }

        exit(0);

    } else if (pid > 0) {
        workerPids_[workerId] = pid;

        {
            std::lock_guard<std::mutex> lock(healthMutex_);
            workerHealth_[workerId] = {pid, time(nullptr)};
        }

        Logger::Info("Worker {} restarted with PID: {}", workerId, pid);
    }
}
