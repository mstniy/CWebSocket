#pragma once

#include <windows.h>

#define WAIT_FOR_MUTEX_OR_EVENT(mutex, event) MutexHelper __local_mutex_holder(mutex, event); \
		if (__local_mutex_holder.isMutexAcquired()==false) \
			return;

//Acquires a specified mutex and waits on an event during construction. If the event is set, doesn't wait on the mutex.
//If the mutex was acquired (i.e., the event was not set), releases it during destruction.
class MutexHelper
{
public:
	MutexHelper(HANDLE _mMutex, HANDLE _eEvent);
	~MutexHelper();
	bool isMutexAcquired();

private:
	bool mutexIsAcquired;
	HANDLE mMutex;
};
