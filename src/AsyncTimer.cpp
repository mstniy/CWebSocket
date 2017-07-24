#pragma once

#include "AsyncTimer.h"

AsyncTimer::AsyncTimer() :
	_tTimer(nullptr)
{
}

AsyncTimer::~AsyncTimer()
{
	DeleteTimerQueueTimer(NULL, _tTimer, INVALID_HANDLE_VALUE); // Cancel the pending callback, if exists. Wait for the pending callback, if exists.
}

void CALLBACK AsyncTimer::TimerCallback(PVOID _callback, BOOLEAN)
{
	auto callback = (std::function<void()>*)(_callback);
	(*callback)();
	delete callback;
}

bool AsyncTimer::Set(DWORD delayms, std::function<void()> callback)
{
	auto clbkcpy = new(std::nothrow) std::function<void()>(callback);
	if (clbkcpy != nullptr)
	{
		DeleteTimerQueueTimer(NULL, _tTimer, INVALID_HANDLE_VALUE);
		_tTimer = nullptr;
		if (TRUE == CreateTimerQueueTimer(&_tTimer, NULL, TimerCallback, clbkcpy, delayms, 0, 0))
			return true;
		delete clbkcpy;
	}
	return false;
}

void AsyncTimer::Cancel()
{
	DeleteTimerQueueTimer(NULL, _tTimer, INVALID_HANDLE_VALUE);
	_tTimer = nullptr;
}