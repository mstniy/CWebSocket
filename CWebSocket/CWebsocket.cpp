#include <shlwapi.h>

#include "..\MutexHolder\CMutexHolder.h"
#include "CWebSocket.h"
#include "CWebSocketEncodingHelpers.h"

CWebSocket::CWebSocket():
	_hSession(nullptr),
	_hConnection(nullptr),
	_hWebSocket(nullptr),
	_serverName(nullptr),
	_path(nullptr),
	_callbackList(nullptr),
	_initialized(false),
	_hMutex(nullptr),
	_hDestructionStarted(nullptr),
	_hRequestHandleClosed(nullptr),
	_hWebSocketHandleClosed(nullptr),
	_state(CWebSocketState::JustCreated)
{
}

CWebSocket::~CWebSocket()
{
	SetEvent(_hDestructionStarted);
	if (_hMutex != nullptr)
		ACQUIRE_MUTEX_OR_RETURN(_hMutex);

	if (_hWebSocket != nullptr)
	{
		WinHttpCloseHandle(_hWebSocket);
		WaitForSingleObject(_hWebSocketHandleClosed, INFINITE); //Wait for WinHTTP handles to get HANDLE_CLOSING. This is cruicial if we're used in a dll.
	}
	if (_hRequest != nullptr)
	{
		WinHttpCloseHandle(_hRequest);
		WaitForSingleObject(_hRequestHandleClosed, INFINITE);
	}
	WinHttpCloseHandle(_hConnection); // Since we don't register any callbacks for _hConnection or _hSession, we don't need to wait for them to get HANDLE_CLOSING.
	WinHttpCloseHandle(_hSession);

	CloseHandle(_hMutex);
	CloseHandle(_hDestructionStarted);
	CloseHandle(_hWebSocketHandleClosed);
	CloseHandle(_hRequestHandleClosed);
	CoTaskMemFree(_serverName);
	CoTaskMemFree(_path);
	if (_callbackList != nullptr)
		delete _callbackList;
	// No need to release _hMutex, because WinHttp won't call us anymore, because we got HANDLE_CLOSING on both _hRequest and _hWebSocket, nor can we called by a C++ function (because the destructor has run on the object).
}

bool CWebSocket::_WinHttpReceive()
{
	DWORD dwError = WinHttpWebSocketReceive(_hWebSocket,
		_winHttpBuffer,
		WinHttpBufferLength,
		NULL,
		NULL);
	if (dwError != ERROR_SUCCESS)
		return false;
	else
		return true;
}

void CWebSocket::CWebSocketOnOpen()
{
	_state = CWebSocketState::ExecutingOnOpen;
	if (_callbackList->onOpen != nullptr)
		_callbackList->onOpen(this, _context);
	if (_state == CWebSocketState::ExecutingOnOpen) // If there was no onOpen callback or the callback didn't call 'Close'.
	{
		_state = CWebSocketState::WaitingForActivity;
		if (_WinHttpReceive() == false)
			CWebSocketOnError();
	}
}

void CWebSocket::CWebSocketOnError()
{
	if (_state != CWebSocketState::ExecutingOnError) // Prevent infinite loops if the client does an invalid operation inside its onError handler (such as trying to close the socket, or write to it).
	{
		if (_callbackList->onError != nullptr)
		{
			_state = CWebSocketState::ExecutingOnError;
			_callbackList->onError(this, _context);
		}
		_state = CWebSocketState::Error;
	}
}

bool CWebSocket::_QueryCloseStatus(PWSTR *_reason, USHORT *_status)
{
	(*_reason) = nullptr;
	(*_status) = 0;

	bool result = false;
	USHORT status;
	PWSTR reason;
	reason = new(std::nothrow) WCHAR[CloseReasonBufferLength + 1]; //One more for the null character.
	DWORD reasonLengthConsumed;
	if (reason != nullptr)
	{
		DWORD dwError = WinHttpWebSocketQueryCloseStatus(_hWebSocket,
			&status,
			reason,
			CloseReasonBufferLength * sizeof(WCHAR),
			&reasonLengthConsumed);
		if (dwError == ERROR_SUCCESS)
		{
			reason[reasonLengthConsumed / sizeof(WCHAR)] = L'\0';
			(*_reason) = reason;
			(*_status) = status;
			result = true;
		}
		else
			delete[] reason;
	}
	return result;
}

// Called from state SendingCloseFrame1, when CLOSE_COMPLETE callback is received from WinHTTP.
// Close function doesn't call this.
void CWebSocket::CWebSocketOnClose()
{
	/*if (_state != CWebSocketState::SendingCloseFrame1)
	{
		CWebSocketOnError();
		return;
	}*/
	if (_callbackList->onClose == nullptr)
	{
		CWebSocketOnClosed();
	}
	else
	{
		USHORT usStatus;
		PWSTR reason;
		if (_QueryCloseStatus(&reason, &usStatus) == true)
		{
			_state = CWebSocketState::ExecutingOnClose;
			_callbackList->onClose(this, _context, usStatus, reason, true);
			if (_state == CWebSocketState::ExecutingOnClose) // _state might be Error, for ex. if the clien tries to call send or close within its onClose.
			{
				CWebSocketOnClosed();
			}
			delete[] reason;
		}
		else
			CWebSocketOnError();
	}
}

void CWebSocket::CWebSocketOnClosed()
{
	/*if ((_state ! = CWebSocketState::SendingCloseFrame1) &&
		(_state ! = CWebSocketState::SendingCloseFrame2) &&
		(_state != CWebSocketState::ExecutingOnClose) &&
		(_state != CWebSocketState::ExecutingDirtyClosing) &&
		(_state != CWebSocketState::ExecutingDirtyClose)
		)
	{
		CWebSocketOnError();
		return ;
	}
	*/
	_state = CWebSocketState::ExecutingOnClosed;
	if (_callbackList->onClosed != nullptr)
		_callbackList->onClosed(this, _context);
	if (_state == CWebSocketState::ExecutingOnClosed) // _state may have changed to Error during execution of the onClosed handler.
		_state = CWebSocketState::Done;
}

void CWebSocket::CWebSocketOnSendBufferSent()
{
	/*if ((_state != CWebSocketState::SendingSendBuffer1) &&
		(_state != CWebSocketState::SendingSendBuffer2))
	{
		CWebSocketOnError();
		return ;
	}
	*/
	if (_state == CWebSocketState::SendingSendBuffer1)
	{
		_state = CWebSocketState::SendingCloseFrame1;
	}
	else if (_state == CWebSocketState::SendingSendBuffer2)
	{
		_state = CWebSocketState::SendingCloseFrame2;
	}
	PVOID reason = ((_closeReason.length() == 0) ? nullptr : (PVOID)_closeReason.c_str());
	DWORD dwError = WinHttpWebSocketClose(_hWebSocket,
		_closeStatus,
		reason,
		_closeReason.length() * sizeof(WCHAR));
	if (dwError != ERROR_SUCCESS)
	{
		CWebSocketOnError();
	}
}

// Called by WinHttp when the server initiates the closing handshake.
void CWebSocket::CWebSocketOnClosing()
{
	/*if (_state != CWebSocketState::WaitingForActivity)
	{
		CWebSocketOnError();
		return;
	}*/
	PWSTR reason;
	USHORT usStatus;
	if (_QueryCloseStatus(&reason, &usStatus) == true)
	{
		_state = CWebSocketState::ExecutingOnClosing;
		if (_callbackList->onClosing != nullptr)
			_callbackList->onClosing(this, _context, usStatus, reason, true);
		if (_callbackList->onClosing == nullptr || _state == CWebSocketState::ExecutingOnClosing)
			Close(usStatus, reason); // If there is no onClosing handler, or the handler didn't call Close, echo back the close status and reason sent by the server.
	}
	else
		CWebSocketOnError();
}

void CWebSocket::CWebSocketOnConnectionReset()
{
	if ((_state == CWebSocketState::WaitingForActivity) ||
		(_state == CWebSocketState::SendingSendBuffer1) || 
		(_state == CWebSocketState::SendingSendBuffer2))
	{
		_state = CWebSocketState::ExecutingDirtyClosing;
		if (_callbackList->onClosing != nullptr)
			_callbackList->onClosing(this, _context, WINHTTP_WEB_SOCKET_ABORTED_CLOSE_STATUS, nullptr, false);
	}
	else if (_state == CWebSocketState::SendingCloseFrame1)
	{
		_state = CWebSocketState::ExecutingDirtyClose;
		if (_callbackList->onClose != nullptr)
			_callbackList->onClose(this, _context, WINHTTP_WEB_SOCKET_ABORTED_CLOSE_STATUS, nullptr, false);
	}
	else if (_state == CWebSocketState::SendingCloseFrame2)
	{
	}
	/*else
	{
		CWebSocketOnError();
	}*/
	CWebSocketOnClosed();
}

void CWebSocket::CWebSocketOnMessage(WINHTTP_WEB_SOCKET_STATUS* status)
{
	if ((_state != CWebSocketState::WaitingForActivity) && 
		(_state != CWebSocketState::SendingSendBuffer1))
		return;
	if (status->eBufferType != WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)
	{
		if (status->dwBytesTransferred > 0)
		{
			_receiveBuffer.insert(_receiveBuffer.end(), _winHttpBuffer, _winHttpBuffer + status->dwBytesTransferred);
			if ((status->eBufferType == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) ||
				(status->eBufferType == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE))
			{
				if (_state == CWebSocketState::WaitingForActivity)
					_state = CWebSocketState::ExecutingOnMessageDuringWaitingForActivity;
				else if (_state == CWebSocketState::SendingSendBuffer1)
					_state = CWebSocketState::ExecutingOnMessageDuringSendingSendBuffer1;
				if (status->eBufferType == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE)
				{
					if (_callbackList->onBinaryMessage != nullptr)
						_callbackList->onBinaryMessage(this, _context, _receiveBuffer.data(), _receiveBuffer.size());
				}
				else // status->eBufferType == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE
				{
					if (_callbackList->onUTF8Message != nullptr)
					{
						PWSTR unicodeString = UTF8ToUnicode(_receiveBuffer.data(), _receiveBuffer.size());
						if (unicodeString != nullptr)
						{
							_callbackList->onUTF8Message(this, _context, unicodeString);
							delete[] unicodeString;
						}
						else
							CWebSocketOnError();
					}
				}
				//TODO: Use a secure vector class to handle confidential data, using SecureZeroMemory.
				_receiveBuffer.clear();
				if (_state == CWebSocketState::ExecutingOnMessageDuringWaitingForActivity)
				{
					if (_WinHttpReceive() == false)
						CWebSocketOnError();
					else
						_state = CWebSocketState::WaitingForActivity;
				}
			}
		}
	}
	else
	{
		CWebSocketOnClosing();
	}
}

void CWebSocket::CWebSocketOnWriteComplete()
{
	/*if (_sendBuffer.size() == 0)
	{
		CWebSocketOnError();
		return;
	}
	if ((_state != CWebSocketState::WaitingForActivity) && 
		(_state != CWebSocketState::SendingSendBuffer1)
		(_state != CWebSocketState::SendingSendBuffer2))
	{
		CWebSocketOnError();
		return ;
	}
	*/
	_sendBuffer.pop();
	if (_sendBuffer.size())
	{
		std::vector<BYTE> message = _sendBuffer.front(); //TODO: As an optimization, we can use move semantics here.
		DWORD dwError = WinHttpWebSocketSend(_hWebSocket,
			WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE,
			(PVOID)message.data(),
			message.size());
		if (dwError != ERROR_SUCCESS)
			CWebSocketOnError();
	}
	else
	{
		if ((_state == CWebSocketState::SendingSendBuffer1) ||
			(_state == CWebSocketState::SendingSendBuffer2))
			CWebSocketOnSendBufferSent();
	}
}

void CWebSocket::CWebSocketOnSendRequestComplete()
{
	if (_state != CWebSocketState::SendingUpgradeRequest)
	{
		//CWebSocketOnError();
		return;
	}

	_state = CWebSocketState::ReceivingUpgradeResponse; //WinHttpReceiveResponse can operate synchronously.
	BOOL fStatus = WinHttpReceiveResponse(_hRequest, 0);
	if (fStatus == FALSE)
		CWebSocketOnError();
}

void CWebSocket::CWebSocketOnReceiveResponseComplete()
{
	if (_state != CWebSocketState::ReceivingUpgradeResponse)
	{
		//CWebSocketOnError();
		return;
	}
	_hWebSocket = WinHttpWebSocketCompleteUpgrade(_hRequest, (DWORD_PTR)this);
	if (_hWebSocket != NULL)
	{
		CWebSocketOnOpen();
	}
	else
		CWebSocketOnError();
}

HRESULT CWebSocket::_SendUpgradeRequest()
{
	HRESULT result = E_FAIL;

	_hSession = WinHttpOpen(L"CWebSocket",
		WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		NULL,
		NULL,
		WINHTTP_FLAG_ASYNC);
	if (_hSession != NULL)
	{
		_hConnection = WinHttpConnect(_hSession,
			_serverName,
			_port,
			0);
		if (_hConnection != NULL)
		{
			_hRequest = WinHttpOpenRequest(_hConnection,
				L"GET",
				_path,
				NULL,
				NULL,
				NULL,
				_secure ? WINHTTP_FLAG_SECURE : 0);
			if (_hRequest != NULL)
			{
				if (WINHTTP_INVALID_STATUS_CALLBACK != WinHttpSetStatusCallback(_hRequest, CWebSocketCallback, WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS, NULL)) // This callback will be inherited by _hWebSocket, as per the documentation of WinHttpSetStatusCallback.
				{
					BOOL fStatus = WinHttpSetOption(_hRequest,
						WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,
						NULL,
						0);
					if (fStatus)
					{
						_state = CWebSocketState::SendingUpgradeRequest; //WinHttpSendRequest can operate synchronously.
						BOOL fStatus = WinHttpSendRequest(_hRequest,
							WINHTTP_NO_ADDITIONAL_HEADERS,
							0,
							NULL,
							0,
							0,
							(DWORD_PTR)this);
						if (fStatus)
						{
							result = true;
						}
					}
				}
			}
		}
	}
	return result;
}

// A callback to be called by WinHttp when a pertinent event happens.
void CALLBACK CWebSocket::CWebSocketCallback(
	HINTERNET hInternet,
	DWORD_PTR dwContext,
	DWORD     dwInternetStatus,
	LPVOID    lpvStatusInformation,
	DWORD     dwStatusInformationLength)
{
	CWebSocket *cws = (CWebSocket *)dwContext;
	if (cws == nullptr)
		return;

	if (dwInternetStatus == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING)
	{
		if (hInternet == cws->_hRequest)
			SetEvent(cws->_hRequestHandleClosed);
		else if (hInternet == cws->_hWebSocket)
			SetEvent(cws->_hWebSocketHandleClosed);
		return;
	}

	const HANDLE handles[2] = { cws->_hDestructionStarted, cws->_hMutex };
	DWORD waitResult = WaitForMultipleObjects(2, handles, FALSE, INFINITE);

	if (waitResult == WAIT_OBJECT_0) // _hDestructionStarted event is signaled.
		return;
	else if (
		(waitResult != WAIT_OBJECT_0 + 1) && 
		(waitResult != WAIT_ABANDONED_0 + 1))
		return; // WaitForMultipleObjects returned error.

	if (cws->_state != CWebSocketState::Error)
	{
		if (dwInternetStatus == WINHTTP_CALLBACK_STATUS_CLOSE_COMPLETE)
		{
			if (cws->_state == CWebSocketState::SendingCloseFrame1)
				cws->CWebSocketOnClose();
			else if (cws->_state == CWebSocketState::SendingCloseFrame2)
				cws->CWebSocketOnClosed();
			/*
			else
			cws->CWebSocketOnError();
			*/
		}
		else if (dwInternetStatus == WINHTTP_CALLBACK_STATUS_READ_COMPLETE)
		{
			cws->CWebSocketOnMessage((WINHTTP_WEB_SOCKET_STATUS *)lpvStatusInformation);
		}
		else if (dwInternetStatus == WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE)
		{
			cws->CWebSocketOnWriteComplete();
		}
		else if (dwInternetStatus == WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE)
		{
			cws->CWebSocketOnSendRequestComplete();
		}
		else if (dwInternetStatus == WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE)
		{
			cws->CWebSocketOnReceiveResponseComplete();
		}
		else if (dwInternetStatus == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR)
		{
			auto asyncResult = (WINHTTP_ASYNC_RESULT *)lpvStatusInformation;
			if (asyncResult->dwError == ERROR_WINHTTP_OPERATION_CANCELLED)
			{
				;
				/*if (cws->_state == CWebSocketState::SendingCloseFrame1)
					; // Do nothing. WinHttp notifies us that our last receive call failed because WinHttpWebSocketClose is called on the websocket.
				else
					cws->CWebSocketOnError(); // We have a problem.*/
			}
			else if (asyncResult->dwError == ERROR_WINHTTP_CONNECTION_ERROR)
				cws->CWebSocketOnConnectionReset();
			else
			{
				cws->CWebSocketOnError();
			}
		}
		else if (dwInternetStatus == WINHTTP_CALLBACK_STATUS_SECURE_FAILURE)
			cws->CWebSocketOnError();
	}
	ReleaseMutex(cws->_hMutex);
}

bool CWebSocket::Initialize(const WCHAR *__serverName, INTERNET_PORT __port, const WCHAR *__path, const CWebSocketCallbackList *__callbackList, bool __secure, void *__context)
{
	if (_initialized)
		return false;
	_initialized = true;

	HRESULT hr = E_FAIL;

	_secure = __secure;
	_port = __port;
	_context = __context;
	_hMutex = CreateMutex(NULL, FALSE, NULL);
	if (_hMutex != nullptr)
		_hDestructionStarted = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (_hDestructionStarted != nullptr)
		_hWebSocketHandleClosed = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (_hWebSocketHandleClosed != nullptr)
		_hRequestHandleClosed = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (_hRequestHandleClosed != nullptr)
		hr = SHStrDupW(__serverName, &_serverName);
	if (SUCCEEDED(hr))
		hr = SHStrDupW(__path, &_path);
	if (SUCCEEDED(hr))
		hr = CloneCWebSocketCallbackList(__callbackList, &_callbackList);
	if (SUCCEEDED(hr))
		hr = _SendUpgradeRequest();
	return(SUCCEEDED(hr));
}
void CWebSocket::_SendBinaryOrUTF8(const BYTE *message, size_t length, WINHTTP_WEB_SOCKET_BUFFER_TYPE bufferType)
{
	if ((_state != CWebSocketState::ExecutingOnOpen) &&
		(_state != CWebSocketState::ExecutingOnMessageDuringWaitingForActivity) &&  // Calling Send during ExecutingOnMessageDuringSendingSendBuffer1 is not accepted since the user of the library would have already called Close.
		(_state != CWebSocketState::WaitingForActivity) &&
		(_state != CWebSocketState::ExecutingOnClosing))
	{
		CWebSocketOnError();
		return;
	}
	_sendBuffer.push(std::vector<BYTE>(message, message + length));
	if (_sendBuffer.size() == 1)
	{
		DWORD dwError = WinHttpWebSocketSend(_hWebSocket,
			bufferType,
			(PVOID)message,
			length);
		if (dwError != ERROR_SUCCESS)
			CWebSocketOnError();
	}
}
void CWebSocket::SendBinary(const BYTE *message, size_t length)
{
	ACQUIRE_MUTEX_OR_RETURN(_hMutex);

	_SendBinaryOrUTF8(message, length, WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE);
}
void CWebSocket::SendWString(const WCHAR *message)
{
	ACQUIRE_MUTEX_OR_RETURN(_hMutex);

	std::vector<BYTE> utf8String;
	if (UnicodeToUTF8(message, utf8String))
	{
		_SendBinaryOrUTF8(utf8String.data(), utf8String.size(), WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE);
	}
	else
		CWebSocketOnError();
}
void CWebSocket::SendWStringAsBinary(const WCHAR *message)
{
	ACQUIRE_MUTEX_OR_RETURN(_hMutex);

	size_t length = wcslen(message) * sizeof(WCHAR);
	_SendBinaryOrUTF8((const BYTE *)message, length, WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE);
}

void CWebSocket::Close(USHORT usStatus, const WCHAR *reason)
{
	ACQUIRE_MUTEX_OR_RETURN(_hMutex);

	if ((_state != CWebSocketState::ExecutingOnOpen) &&
		(_state != CWebSocketState::ExecutingOnMessageDuringWaitingForActivity) && // Calling Close during ExecutingOnMessageDuringSendingSendBuffer1 is not accepted since the user of the library would have already called Close.
		(_state != CWebSocketState::WaitingForActivity) &&
		(_state != CWebSocketState::ExecutingOnClosing))
	{
		CWebSocketOnError();
		return;
	}
	if (_state == CWebSocketState::ExecutingOnClosing) // ExecutingOnClosing
		_state = CWebSocketState::SendingSendBuffer2; // Closing handshake is initiated by the server.
	else // WaitingForActivity, ExecutingOnOpen, ExecutingOnMessage
		_state = CWebSocketState::SendingSendBuffer1; // Closing handshake is initiated by us.
	_closeStatus = usStatus;
	if (reason != nullptr)
		_closeReason = std::wstring(reason);
	else
		_closeReason = std::wstring();
	if (_sendBuffer.size() == 0)
	{
		CWebSocketOnSendBufferSent();
	}
}