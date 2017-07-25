#pragma once

#include <windows.h>
#include <WinHttp.h>
#include <vector>
#include <queue>
#include <assert.h>

#include "CWebSocketCallbackList.h"
#include "SeqAsyncQueue.h"
#include "AsyncTimer.h"

#pragma comment (lib, "winhttp.lib")
#pragma comment (lib, "Shlwapi.lib")

enum class CWebSocketState
{
	NoTcpConnection, // CWebSocket is created in this state, stays in this state until the first call to Connect, and falls back to this state whenever Abort is called.
	ConnectPending, // Connect was called with delayms != 0. Previous connection has been aborted and a new connection will be reopened when the timer fires.
	SendingUpgradeRequest,
	ReceivingUpgradeResponse,
	WaitingForActivity,
	SendingSendBuffer1, // Closing handshake initiated by us.
	SendingCloseFrame1,
	ReceivedCloseFrame2, // Closing handshake initiated by the server.
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
	HINTERNET _hRequest;
	LPWSTR _serverName;
	INTERNET_PORT _port;
	LPWSTR _path;
	SeqAsyncQueue _saq; // Calls to all public member functions get queued and are executed in a worker thread sequentially.
	AsyncTimer _at; // The timer that is set when Connect is called with delayms != 0.
	cwebsocketinternal::CWebSocketCallbackList _callbackList;
	BYTE _winHttpBuffer[WinHttpBufferLength];
	std::vector<BYTE> _receiveBuffer;
	std::queue< std::pair<std::vector<BYTE>, WINHTTP_WEB_SOCKET_BUFFER_TYPE> > _sendBuffer;
	bool _initialized;
	CWebSocketState _state;
	HANDLE _mMutex;
	HANDLE _eDrainWinHttpCallbacks; // If this event is set, CWebSocketWinHttpCallback will ignore all callbacks from WinHttp except WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING.
	HANDLE _eDrainSaqAtCallbacks; // If this event is set, asynchronous callbacks from SaqAsyncQueue and AsyncTimer will be ignored.
	HANDLE _eRequestHandleClosed;
	HANDLE _eWebSocketHandleClosed;
	USHORT _closeStatus;
	std::vector<BYTE> _UTF8CloseReason;
	bool _secure;
	size_t _reconnectCount; // A counter that increases with every call to Connect.

private:
	HRESULT _CreateSessionConnectionHandles();
	bool _SendUpgradeRequest();
	bool _WinHttpReceive();
	bool _QueryCloseStatus(PWSTR *reason, USHORT *status);
	void _ClientSendBinaryOrUTF8(const BYTE *message, size_t length, WINHTTP_WEB_SOCKET_BUFFER_TYPE bufferType);
	void _Abort();
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
	CWebSocket(const CWebSocket&) = delete; // It is invalid to 'copy' a websocket.
	~CWebSocket();

	// Initializes CWebSocket with given parameters. Call it before any other member function (except for the constructor, of course).
	// Note that calling Initialize does not open the connection.
	// serverName: The server to connect to.
	// port: The port to connect to.
	// path: The url on the server to connect to.
	// secure: true for secure communication (over SSL/TLS), false otherwise.
	// Returns true for success, false for failure.
	// If this function returns false, destruct the object without calling any member functions.
	// If this function returns true, set pertinent callbacks using on* class of functions and call Connect to connect the websocket.
	bool Initialize(const WCHAR *__serverName, INTERNET_PORT __port, const WCHAR *__path, bool secure);

	// Send the given binary message over the websocket.
	void SendBinary(const BYTE *message, size_t length);

	// Send the given unicode message over the websocket as a UTF8 message.
	void SendWString(const WCHAR *message);

	// Send the given UTF8 encoded unicode message as a UTF8 message.
	void SendUTF8String(const BYTE *message, size_t length);

	// Send the given unicode message over the websocket as a binary message.
	void SendWStringAsBinary(const WCHAR *message);

	// Gracefully closes the underlying websocket. To abort a websocket, call Abort or destruct it.
	// usStatus defaults to WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS (1000) and reason defaults to empty string.
	// CWebSocket encodes the given reason string in UTF8 before sending it.
	// Note that CWebSocket will not send a close frame right away if there is data waiting to be sent to the server in the send buffer, but will instead wait for the buffer to be completely sent.
	// Do not call this function while a call to Connect is waiting for the timeout.
	void Close(USHORT code = WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, const WCHAR *reason = L"");

	// on* class of function below set or change respecting callbacks.
	// They return a reference to the websocket itself for fluid api.
	// Since all public functions are executed lazily, even after these functions return, a network event may happen in the time it takes to change a callback, which will cause the previous callback to be called.
	// It's important to note that if a network event happens while a callback is executing, CWebSocket will wait for the callback of the previous event to finish before calling the callback for the new event.
	CWebSocket& onOpen(CWebSocketOnOpenCallback cb);
	CWebSocket& onBinaryMessage(CWebSocketOnBinaryMessageCallback cb);
	CWebSocket& onUTF8Message(CWebSocketOnUTF8MessageCallback cb);
	CWebSocket& onClose(CWebSocketOnCloseCallback cb);
	CWebSocket& onClosing(CWebSocketOnClosingCallback cb);
	CWebSocket& onClosed(CWebSocketOnClosedCallback cb);
	CWebSocket& onError(CWebSocketOnErrorCallback cb);

	// Attempts to connect the websocket to the url specified in the call to Initialize, after waiting for delayms milliseconds.
	// Do not call Connect while another call to Connect is waiting for the timeout.
	// Aborts the current connection, if exists.
	// Expect either onOpen or onError callback to be called as the response.
	void Connect(DWORD delayms = 0);

	// Closes the underlying TCP connection without a proper websocket closing handshake.
	// After this function is called, you may call Connect to open a new connection to the server.
	// Do not call this function while a call to Connect is waiting for the timeout.
	void Abort();
};