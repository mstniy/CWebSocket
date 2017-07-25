#include "MutexHelper.h"
#include <windows.h>

MutexHelper::MutexHelper(HANDLE _mMutex, HANDLE _eEvent)
{
	const HANDLE objects[2] = {_mMutex, _eEvent};
	DWORD r = WaitForMultipleObjects(2, objects, FALSE, INFINITE);
	if (r == WAIT_OBJECT_0 || r == WAIT_ABANDONED_0)
	{
		mutexIsAcquired = true;
		mMutex = _mMutex;
	}
	else
	{
		mutexIsAcquired = false;
		mMutex = nullptr;
	}
}
MutexHelper::~MutexHelper()
{
	if (mutexIsAcquired)
		ReleaseMutex(mMutex);
}
bool MutexHelper::isMutexAcquired()
{
	return mutexIsAcquired;
}
