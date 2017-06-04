#pragma once

#include <windows.h>

#define ACQUIRE_MUTEX_OR_RETURN_VALUE(mutex,value) {CMutexHolder __local_mutex_holder(mutex); \
		if (__local_mutex_holder.isProperlyInitialized()==false) \
			return(value);}

#define ACQUIRE_MUTEX_OR_RETURN(mutex) {CMutexHolder __local_mutex_holder(mutex); \
		if (__local_mutex_holder.isProperlyInitialized()==false) \
			return;}

//Acquires a specified mutex during construction, releases it during destruction (ex, end of a function).
class CMutexHolder
{
public:
	CMutexHolder(HANDLE _hMutex);
	~CMutexHolder();
	bool isProperlyInitialized();

private:
	bool properlyInitialized;
	HANDLE hMutex;
};
