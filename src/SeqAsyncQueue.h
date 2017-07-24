#pragma once

#include <windows.h>
#include <functional>
#include <queue>

// SeqAsyncQueue stands for sequential asynchronous queue.
// It's a queue of works which are scheduled to be executed by a worker thread according to the FIFO principle.
class SeqAsyncQueue
{
private:
	HANDLE _mMutex;
	HANDLE _eQueueEmpty;
	std::queue<std::function<void()>> _q;
	PTP_WORK _work;
private:
	static VOID CALLBACK WorkCallback(PTP_CALLBACK_INSTANCE /*inst*/, PVOID context, PTP_WORK /*work*/);
public:
	SeqAsyncQueue();
	~SeqAsyncQueue();
	bool Initialize();
	void QueueAsyncWork(std::function<void()> callback);
	void WaitTheQueue();
};