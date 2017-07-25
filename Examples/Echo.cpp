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
		wcout << L"Closing connection..." << endl;
		cws.Close();
	}).onClose([=, &cws](USHORT code, PCWSTR reason, bool wasClean) {
		wcout << L"Server responded to our close request." << endl;
		wcout << L"\tCode: " << code << endl;
		wcout << L"\tReason: " << reason << endl;
		wcout << L"\tIt was " << (wasClean ? L"" : L"not ") << L"clean." << endl;
	}).onClosed([=]() {
		wcout << L"Websocket is now closed by both sides. Terminating..." << endl;
		SetEvent(hEventDone);
	}).onError([=]() {
		wcout << L"onError is called. Terminating..." << endl;
		SetEvent(hEventDone);
	}).Connect();

	WaitForSingleObject(hEventDone, INFINITE);
	system("PAUSE");
	return 0;
}

#endif