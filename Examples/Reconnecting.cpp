#if 1

#include <iostream>
#include <windows.h>

#include "..\src\CWebSocket.h"

using namespace std;

HANDLE hEventDone;
PCWSTR helloWorldMessage = L"Hello world!";

int main()
{
	CWebSocket cws;

	if (cws.Initialize(L"echo.websocket.org", 443, L"/", true) == false)
	{
		wcout << L"Failed to initialize CWebSocket!" << endl;
		return 1;
	}

	hEventDone = CreateEvent(NULL, TRUE, FALSE, NULL);

	cws.onOpen([=, &cws]() {
		wcout << L"Websocket is now open! Sending \"" << helloWorldMessage << L"\" ..." << endl;
		cws.SendWString(helloWorldMessage);
	}).onUTF8Message([=, &cws](PCWSTR message) {
		wcout << L"Server responded: \"" << message << L"\"" << endl;
		Sleep(1000);
		wcout << L"Sending \"" << helloWorldMessage << L"\" ..." << endl;
		cws.SendWString(helloWorldMessage);
	}).onClosing([=, &cws](USHORT code, PCWSTR reason, bool wasClean){
		if (wasClean == false)
		{
			wcout << L"Connection dropped. Reconnecting in 5 seconds." << endl;
			cws.Connect(5000);
		}
		else
			wcout << L"Server closed the connection. Reason: " << reason << ". Code: " << code << endl;
	}).onClosed([=]() {
		wcout << L"Websocket is now closed by both sides. Terminating..." << endl;
		SetEvent(hEventDone);
	}).onError([=, &cws]() {
		wcout << L"onError is called. Reconnecting in 5 seconds." << endl;
		cws.Connect(5000);
	}).Connect();

	WaitForSingleObject(hEventDone, INFINITE);
	system("PAUSE");
	return 0;
}

#endif