#pragma once

#include <windows.h>
#include <functional>

// A callback function to be called when the connection opens.
typedef std::function<void()> CWebSocketOnOpenCallback;

// A callback function to be called when a binary message arrives at the websocket.
typedef std::function<void(const BYTE* message, size_t length)> CWebSocketOnBinaryMessageCallback;

// A callback function to be called when a UTF8 string arrives at the websocket.
// Note that CWebSocket automatically converts the received UTF8 string to UTF16 before calling this callback.
// If the received UTF8 string is invalid, OnErrorCallback will be called.
typedef std::function<void(PCWSTR message)> CWebSocketOnUTF8MessageCallback;

// A callback function to be called when the server initiates the closing handshake.
// If this callback returns without calling Close, CWebSocket will automatically echo the close status it got from the server.
// You can do additional Send's inside this callback if wasClean is true. Since the closing handshake will have been started by the server, we won't have sent our close frame.
// If the underlying TCP connection is reset, wasClean will be false and code will be WINHTTP_WEB_SOCKET_ABORTED_CLOSE_STATUS (1006).
// CWebSocket automatically converts the received reason string from UTF8 to UTF16. If the received string isn't valid UTF8, onError callback will be called.
typedef std::function<void(USHORT usStatus, PCWSTR reason, bool wasClean)> CWebSocketOnClosingCallback;

// A callback function to be called when the server responds to a closing handshake initiated by us.
// If the underlying TCP connection is reset, wasClean will be false and code will be WINHTTP_WEB_SOCKET_ABORTED_CLOSE_STATUS (1006).
// CWebSocket automatically converts the received reason string from UTF8 to UTF16. If the received string isn't valid UTF8, onError callback will be called.
typedef std::function<void(USHORT usStatus, PCWSTR reason, bool wasClean)> CWebSocketOnCloseCallback;

// A callback function to be called when the closing handshake has been completed.
// This callback is called no matter which party initiated the closing handshake.
// If we initiated the closing handshake, first onClose, then onClosed will be called.
// If the server initiated the closing handshake, first onClosing, then onClosed will be called.
// After receiving this callback, Connect can be used to create a new connection to the same server.
typedef std::function<void()> CWebSocketOnClosedCallback;

// A callback function to be called when an unexpected error occurs.
// This callback is called for OS errors, connection errors, invalid UTF8 payloads and attempts to perform an illegal operation such as trying to do a Send inside the onClose handler.
// After receiving this callback, the websocket will receive no further callbacks even if another network event happens until Connect is called to create a new connection.
typedef std::function<void()> CWebSocketOnErrorCallback;

namespace cwebsocketinternal
{
	class CWebSocketCallbackList
	{
	public:
		CWebSocketOnOpenCallback onOpen;
		CWebSocketOnBinaryMessageCallback onBinaryMessage;
		CWebSocketOnUTF8MessageCallback onUTF8Message;
		CWebSocketOnCloseCallback onClose;
		CWebSocketOnClosingCallback onClosing;
		CWebSocketOnClosedCallback onClosed;
		CWebSocketOnErrorCallback onError;
	public:
		CWebSocketCallbackList();
	};
}