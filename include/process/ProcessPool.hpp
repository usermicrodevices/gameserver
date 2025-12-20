#pragma once

#include <vector>
#include <memory>
#include <atomic>
#include <functional>
#include <sys/types.h>
#include <unistd.h>

class ProcessPool {
public:
	enum class ProcessRole {
		MASTER,
		WORKER
	};

	ProcessPool(int numProcesses);
	~ProcessPool();

	bool Initialize();
	void Run();
	void Shutdown();

	ProcessRole GetRole() const { return role_; }
	int GetWorkerId() const { return workerId_; }
	pid_t GetMasterPid() const { return masterPid_; }

	// Callback for worker process
	void SetWorkerMain(std::function<void(int workerId)> workerMain);

	// Inter-process communication
	bool SendToWorker(int workerId, const std::string& message);
	std::string ReceiveFromMaster();

	// Process health monitoring
	bool IsWorkerAlive(int workerId) const;
	void RestartWorker(int workerId);

private:
	void MasterProcess();
	void WorkerProcess(int workerId);
	void SetupSignalHandlers();
	void CleanupDeadWorkers();

	int numProcesses_;
	ProcessRole role_{ProcessRole::MASTER};
	int workerId_{-1};
	pid_t masterPid_{-1};

	std::vector<pid_t> workerPids_;
	std::atomic<bool> running_{false};

	std::function<void(int workerId)> workerMain_;

	// IPC mechanisms
	std::vector<int> workerPipes_;

	// Health monitoring
	mutable std::mutex healthMutex_;
	std::unordered_map<int, std::pair<pid_t, time_t>> workerHealth_;
};
