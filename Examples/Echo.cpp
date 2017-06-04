#include <iostream>
#include <windows.h>

#include "..\CWebSocket\CWebSocket.h"

using namespace std;

HANDLE hEventDone;
PCWSTR helloWorldMessage = L"Hello world!";

int main()
{
	CWebSocketCallbackList callbackList = { 0 }; // Do not forget to initialize callbacks to NULL if you won't set all callbacks.
	CWebSocket cws;

	callbackList.onOpen = [](CWebSocket *cws, void *context) {
		wcout << "Websocket is now open! Sending \"" << helloWorldMessage << "\" ..." << endl;
		cws->SendWString(helloWorldMessage);
	};
	callbackList.onUTF8Message = [](CWebSocket *cws, void *context, PCWSTR message) {
		wcout << "Server responded: \"" << message << "\"" << endl;
		cout << "Closing connection..." << endl;
		cws->Close();
	};
	callbackList.onClose = [](CWebSocket *cws, void *context, USHORT code, PCWSTR reason, bool wasClean) {
		cout << "Server responded to our close request." << endl;
		cout << "\tCode: " << code << endl;
		wcout << "\tReason: " << reason << endl;
		cout << "\tIt was " << (wasClean ? "" : "not ") << "clean." << endl;
	};
	callbackList.onClosed = [](CWebSocket *cws, void *context) {
		cout << "Websocket is now closed by both sides. Terminating..." << endl;
		SetEvent(hEventDone);
	};
	callbackList.onError = [](CWebSocket *cws, void *context) {
		cout << "onError is called. Terminating..." << endl;
		SetEvent(hEventDone);
	};

	hEventDone = CreateEvent(NULL, TRUE, FALSE, NULL);

	if (cws.Initialize(L"echo.websocket.org", 443, L"/", &callbackList, true, NULL) == false)
	{
		cout << "Failed to initialize CWebSocket!" << endl;
		return 1;
	}

	WaitForSingleObject(hEventDone, INFINITE);
	system("PAUSE");
	return 0;
}