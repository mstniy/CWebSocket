#include "SeqAsyncQueue.h"

VOID CALLBACK SeqAsyncQueue::WorkCallback(PTP_CALLBACK_INSTANCE /*inst*/, PVOID context, PTP_WORK /*work*/)
{
	SeqAsyncQueue* self = (SeqAsyncQueue*)context;
	WaitForSingleObject(self->_mMutex, INFINITE);
	std::function<void()> callback = self->_q.front();
	ReleaseMutex(self->_mMutex);
	callback();
	WaitForSingleObject(self->_mMutex, INFINITE);
	self->_q.pop();
	if (self->_q.size() == 0)
		SetEvent(self->_eQueueEmpty);
	else
		SubmitThreadpoolWork(self->_work);
	ReleaseMutex(self->_mMutex);
}
SeqAsyncQueue::SeqAsyncQueue() :
	_mMutex(nullptr),
	_eQueueEmpty(nullptr),
	_work(nullptr)
{
}
bool SeqAsyncQueue::Initialize()
{
	_mMutex = CreateMutex(NULL, FALSE, NULL);
	if (_mMutex != nullptr)
		_eQueueEmpty = CreateEvent(NULL, TRUE, TRUE, NULL);
	if (_eQueueEmpty != nullptr)
		_work = CreateThreadpoolWork(WorkCallback, this, NULL); // Use the default environment.
	if (_work != nullptr)
		return true;
	return false;
}
SeqAsyncQueue::~SeqAsyncQueue()
{
	if (_mMutex != nullptr && _eQueueEmpty != nullptr) // If either of these handles are NULL, this means Initialize had failed so we don't need to wait for anything.
	{
		const HANDLE objects[2] = { _mMutex, _eQueueEmpty };
		WaitForMultipleObjects(2, objects, TRUE, INFINITE);
	}
	CloseHandle(_mMutex);
	CloseHandle(_eQueueEmpty);
	CloseThreadpoolWork(_work);
}
void SeqAsyncQueue::QueueAsyncWork(std::function<void()> callback)
{
	WaitForSingleObject(_mMutex, INFINITE);
	_q.push(callback);
	ResetEvent(_eQueueEmpty);
	if (_q.size() == 1)
		SubmitThreadpoolWork(_work);
	ReleaseMutex(_mMutex);
}
void SeqAsyncQueue::WaitTheQueue()
{
	WaitForSingleObject(_eQueueEmpty, INFINITE);
}