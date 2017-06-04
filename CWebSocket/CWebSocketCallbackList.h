#pragma once

#include <windows.h>

#include "CWebSocket.fwd.h"

// A callback function to be called when the websocket is open.
typedef void(*CWebSocketOnOpenCallback)(CWebSocket *cws, void *context);

// A callback function to be called when a binary message arrives at the websocket.
typedef void(*CWebSocketOnBinaryMessageCallback)(CWebSocket *cws, void *context, const BYTE *message, size_t length);

// A callback function to be called when a UTF8 string arrives at the websocket.
// Note that CWebSocket automatically converts the received UTF8 string to unicode before calling this callback.
// If the received UTF8 string is invalid, OnErrorCallback will be called.
typedef void(*CWebSocketOnUTF8MessageCallback)(CWebSocket *cws, void *context, PCWSTR message);

// A callback function to be called when the server initiates the closing handshake.
// If this callback is not set (set to NULL) or returns without calling CWebSocket::Close, CWebSocket will automatically echo the close status and reason it got from the server.
// You can do additional Send's inside this callback if wasClean is true. Since the closing handshake will have been started by the server, we won't have sent our close frame.
// If the underlying TCP connection is reset, wasClean will be false and code will be WINHTTP_WEB_SOCKET_ABORTED_CLOSE_STATUS (1006).
typedef void(*CWebSocketOnClosingCallback)(CWebSocket *cws, void *context, USHORT code, const WCHAR *reason, bool wasClean);

// A callback function to be called when the server responds to a closing handshake initiated by us.
// If the underlying TCP connection is reset, wasClean will be false and code will be WINHTTP_WEB_SOCKET_ABORTED_CLOSE_STATUS (1006).
typedef void(*CWebSocketOnCloseCallback)(CWebSocket *cws, void *context, USHORT code, const WCHAR *reason, bool wasClean);

// A callback function to be called when the closing handshake has been completed. Indicates that no further callbacks will be made for this CWebSocket.
// This callback is called no matter which party initiated the closing handshake.
// If we initiated the closing handshake, first onClose, then onClosed will be called.
// If the server initiated the closing handshake, first onClosing, then onClosed will be called.
typedef void(*CWebSocketOnClosedCallback)(CWebSocket *cws, void *context);

// A callback function to be called when an unexpected error occurs.
// The most likely reason is the attempt of the user to perform an illegal operation such as trying to do a Send inside its onClose handler.
// This callback, too, indicates that no further callbacks will be made, just like onClosed.
typedef void(*CWebSocketOnErrorCallback)(CWebSocket *cws, void *context);

struct CWebSocketCallbackList
{
	CWebSocketOnOpenCallback onOpen;
	CWebSocketOnBinaryMessageCallback onBinaryMessage;
	CWebSocketOnUTF8MessageCallback onUTF8Message;
	CWebSocketOnCloseCallback onClose;
	CWebSocketOnClosingCallback onClosing;
	CWebSocketOnClosedCallback onClosed;
	CWebSocketOnErrorCallback onError;
};

HRESULT CloneCWebSocketCallbackList(const CWebSocketCallbackList *source, CWebSocketCallbackList **destination);