#pragma once

#include <windows.h>
#include <functional>
#include <stdint.h>

// AsyncTimer lets you execute a callback function at a particular point in the future, similar to javascript's setTimeout.
class AsyncTimer
{
private:
	HANDLE _tTimer; // AsyncTimer does not synchronize calls made to its member functions.
	std::function<void()> _callback;
private:
	static void CALLBACK TimerCallback(PVOID _callback, BOOLEAN);
public:
	AsyncTimer();
	~AsyncTimer();
	bool Set(DWORD delayms, std::function<void()> callback); // If a callback is pending, cancels it. If the previous callback is executing, waits for it.
	void Cancel(); // If the previous callback is executing, waits for it. If no callback is set, does nothing.
};