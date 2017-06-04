#pragma once

#include <windows.h>
#include <WinHttp.h>
#include <vector>
#include <queue>

#include "CWebSocketCallbackList.h"

#pragma comment (lib, "winhttp.lib")
#pragma comment (lib, "Shlwapi.lib")

enum class CWebSocketState
{
	JustCreated,
	SendingUpgradeRequest,
	ReceivingUpgradeResponse,
	ExecutingOnOpen,
	WaitingForActivity,
	ExecutingOnMessageDuringWaitingForActivity,
	ExecutingOnMessageDuringSendingSendBuffer1,
	ExecutingOnClose,
	ExecutingDirtyClose,
	ExecutingOnClosing,
	ExecutingDirtyClosing,
	ExecutingOnClosed,
	ExecutingOnError,
	SendingSendBuffer1,
	SendingCloseFrame1,
	SendingSendBuffer2,
	SendingCloseFrame2,
	Done,
	Error
};

class CWebSocket
{
private:
	const static DWORD WinHttpBufferLength = 1024;
	const static DWORD CloseReasonBufferLength = 123;
private:
	HINTERNET _hSession;
	HINTERNET _hConnection;
	HINTERNET _hWebSocket;
	HINTERNET _hRequest; // Used temporarily, during initialization.
	LPWSTR _serverName;
	INTERNET_PORT _port;
	LPWSTR _path;
	CWebSocketCallbackList *_callbackList;
	BYTE _winHttpBuffer[WinHttpBufferLength];
	std::vector<BYTE> _receiveBuffer;
	std::queue< std::vector<BYTE> > _sendBuffer;
	bool _initialized;
	CWebSocketState _state;
	HANDLE _hMutex;
	HANDLE _hDestructionStarted;
	HANDLE _hRequestHandleClosed;
	HANDLE _hWebSocketHandleClosed;
	USHORT _closeStatus;
	std::wstring _closeReason;
	bool _secure;
	void *_context;

private:
	HRESULT _SendUpgradeRequest();
	bool _WinHttpReceive();
	bool _QueryCloseStatus(PWSTR *reason, USHORT *status);
	void _SendBinaryOrUTF8(const BYTE *message, size_t length, WINHTTP_WEB_SOCKET_BUFFER_TYPE bufferType);
	void CWebSocketOnOpen();
	void CWebSocketOnError();
	void CWebSocketOnClose();
	void CWebSocketOnClosing();
	void CWebSocketOnClosed();
	void CWebSocketOnSendBufferSent();
	void CWebSocketOnConnectionReset();
	void CWebSocketOnMessage(WINHTTP_WEB_SOCKET_STATUS *status);
	void CWebSocketOnWriteComplete();
	void CWebSocketOnSendRequestComplete();
	void CWebSocketOnReceiveResponseComplete();

	void static CALLBACK CWebSocketCallback(
		HINTERNET hInternet,
		DWORD_PTR dwContext,
		DWORD     dwInternetStatus,
		LPVOID    lpvStatusInformation,
		DWORD     dwStatusInformationLength);

public:
	CWebSocket();
	~CWebSocket();

	// Initializes CWebSocket with given parameters. Call it before any other member function (except for the constructor, of course).
	// Note that the simple act of constructing a CWebSocket does nothing relevant to networking.
	// serverName: The server to connect to.
	// port: The port to connect to.
	// path: The url on the server to connect to.
	// callbackList: A pointer to a CWebSocketCallbackList that describes callbacks to be called on various events.
	// secure: true for secure communication (over SSL/TLS), false otherwise.
	// context: A pointer value to be sent to every callback associated with this CWebSocket. This value is optional and is intended to be semantic.
	// Returns true for success, false for failure.
	// If Initialization succeeds, expect onOpen callback to be called (if exists).
	// If this function returns false, destruct the object without calling any member functions.
	bool Initialize(const WCHAR *__serverName, INTERNET_PORT __port, const WCHAR *__path, const CWebSocketCallbackList *__callbackList, bool secure, void *context);

	// Send the given binary message over the websocket.
	// Note that CWebSocket currently has no support for sending UTF8 messages. In order to send one, use this method. Beware that it will be sent as a binary message and not a UTF8 one.
	void SendBinary(const BYTE *message, size_t length);

	// Send the given unicode message over the websocket.
	void SendWString(const WCHAR *message);

	// Send the given unicode message over the websocket as a binary message.
	void SendWStringAsBinary(const WCHAR *message);

	// Gracefully closes the underlying websocket. To abort a websocket, just destruct the CWebSocket object.
	// usStatus defaults to WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS (1000) and reason defaults to NULL.
	// Note that CWebSocket will not send a close frame right away if there is data waiting to be sent to the server in the send buffer, but will instead wait for the buffer to be completely sent.
	void Close(USHORT code = WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, const WCHAR *reason = nullptr);
};