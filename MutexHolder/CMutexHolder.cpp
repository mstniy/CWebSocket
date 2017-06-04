#include "CMutexHolder.h"
#include <windows.h>

CMutexHolder::CMutexHolder(HANDLE _hMutex):
	properlyInitialized(false),
	hMutex(_hMutex)
{
	DWORD r = WaitForSingleObject(_hMutex, INFINITE);
	if (r == WAIT_OBJECT_0 || r == WAIT_ABANDONED_0)
		properlyInitialized = true;
}
CMutexHolder::~CMutexHolder()
{
	if (properlyInitialized)
		ReleaseMutex(hMutex);
}
bool CMutexHolder::isProperlyInitialized()
{
	return properlyInitialized;
}
