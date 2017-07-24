#include <shlwapi.h>
#include <utility>

#include "CMutexHolder.h"
#include "CWebSocket.h"
#include "CWebSocketEncodingHelpers.h"

CWebSocket::CWebSocket() :
	_hSession(nullptr),
	_hConnection(nullptr),
	_hWebSocket(nullptr),
	_hRequest(nullptr),
	_serverName(nullptr),
	_path(nullptr),
	_initialized(false),
	_state(CWebSocketState::NoTcpConnection),
	_mMutex(nullptr),
	_eDrainWinHttpCallbacks(nullptr),
	_eRequestHandleClosed(nullptr),
	_eWebSocketHandleClosed(nullptr),
	_reconnectCount(0)
{
}

CWebSocket::~CWebSocket()
{
	_saq.WaitTheQueue(); // Wait for asynchronous method calls to end.
	_at.Cancel();
	if (_mMutex != nullptr)
		ACQUIRE_MUTEX_OR_RETURN(_mMutex); // If there is a WinHTTP callback running right now, wait for it to finish.
	_Abort();

	CloseHandle(_mMutex);
	CloseHandle(_eDrainWinHttpCallbacks);
	CloseHandle(_eWebSocketHandleClosed);
	CloseHandle(_eRequestHandleClosed);
	CoTaskMemFree(_serverName);
	CoTaskMemFree(_path);
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
	if (_WinHttpReceive() == false)
		CWebSocketOnError();
	else
	{
		_state = CWebSocketState::WaitingForActivity;
		_callbackList.onOpen();
	}
}

void CWebSocket::CWebSocketOnError()
{
	if (_state != CWebSocketState::Error) // One call to onError callback should be enough.
	{
		_state = CWebSocketState::Error;
		_callbackList.onError();
	}
}

bool CWebSocket::_QueryCloseStatus(PWSTR *reason, USHORT *status)
{
	bool result = false;
	BYTE* UTF8Reason;
	UTF8Reason = new(std::nothrow) BYTE[CloseReasonBufferLength];
	DWORD reasonLengthConsumed;
	if (UTF8Reason != nullptr)
	{
		DWORD dwError = WinHttpWebSocketQueryCloseStatus(_hWebSocket,
			status,
			UTF8Reason,
			CloseReasonBufferLength,
			&reasonLengthConsumed);
		if (dwError == ERROR_SUCCESS)
		{
			(*reason) = cwebsocketinternal::UTF8ToUnicode(UTF8Reason, reasonLengthConsumed);
			if ((*reason) != nullptr)
			{
				result = true;
			}
		}
		delete[] UTF8Reason;
	}
	return result;
}

void CWebSocket::_Abort()
{
	SetEvent(_eDrainWinHttpCallbacks);

	if (_hWebSocket != nullptr)
	{
		WinHttpCloseHandle(_hWebSocket);
		WaitForSingleObject(_eWebSocketHandleClosed, INFINITE); //Wait for WinHTTP handles to get HANDLE_CLOSING.
		_hWebSocket = nullptr;
	}
	if (_hRequest != nullptr)
	{
		WinHttpCloseHandle(_hRequest);
		WaitForSingleObject(_eRequestHandleClosed, INFINITE);
		_hRequest = nullptr;
	}
	ResetEvent(_eDrainWinHttpCallbacks);
	ResetEvent(_eWebSocketHandleClosed);
	ResetEvent(_eRequestHandleClosed);
}

// Called from state SendingCloseFrame1, when CLOSE_COMPLETE callback is received from WinHTTP.
// Close function doesn't call this.
void CWebSocket::CWebSocketOnClose()
{
	USHORT usStatus;
	PWSTR reason;
	if (_QueryCloseStatus(&reason, &usStatus) == true)
	{
		const size_t oldReconCnt = _reconnectCount;
		_state = CWebSocketState::Done;
		_callbackList.onClose(usStatus, reason, true);
		_saq.QueueAsyncWork([=]() {
			ACQUIRE_MUTEX_OR_RETURN(_mMutex);
			if (_state == CWebSocketState::Done && _reconnectCount == oldReconCnt) // Make sure the onClose callback didn't call Connect.
				CWebSocketOnClosed();
		});
		delete[] reason;
	}
	else
		CWebSocketOnError();
}

void CWebSocket::CWebSocketOnClosed()
{
	_state = CWebSocketState::Done;
	_callbackList.onClosed();
}

void CWebSocket::CWebSocketOnSendBufferSent()
{
	if ((_state == CWebSocketState::SendingSendBuffer1) ||
		(_state == CWebSocketState::SendingSendBuffer2))
	{
		if (_state == CWebSocketState::SendingSendBuffer1)
		{
			_state = CWebSocketState::SendingCloseFrame1;
		}
		else if (_state == CWebSocketState::SendingSendBuffer2)
		{
			_state = CWebSocketState::SendingCloseFrame2;
		}
		const PVOID reason = _UTF8CloseReason.size() == 0 ? nullptr : _UTF8CloseReason.data(); // WinHttpWebSocketClose fails when pvReason != nullptr and dwReasonLength == 0
		DWORD dwError = WinHttpWebSocketClose(_hWebSocket,
			_closeStatus,
			reason,
			_UTF8CloseReason.size());
		if (dwError != ERROR_SUCCESS)
		{
			CWebSocketOnError();
		}
	}
}

// Called by WinHttp when the server initiates the closing handshake.
void CWebSocket::CWebSocketOnClosing()
{
	PWSTR reason;
	USHORT usStatus;
	if (_QueryCloseStatus(&reason, &usStatus) == true)
	{
		const size_t oldReconCnt = _reconnectCount;
		_state = CWebSocketState::ReceivedCloseFrame2;
		_callbackList.onClosing(usStatus, reason, true);
		delete[] reason;
		_saq.QueueAsyncWork([=]() {
			if (_state == CWebSocketState::ReceivedCloseFrame2 && _reconnectCount == oldReconCnt) // If the onClosing handler didn't call Close, echo back the close status sent by the server.
				Close(usStatus);
		});
	}
	else
		CWebSocketOnError();
}

void CWebSocket::CWebSocketOnConnectionReset()
{
	const size_t oldReconCnt = _reconnectCount;
	CWebSocketState oldState = _state;
	_state = CWebSocketState::Done;
	if ((oldState == CWebSocketState::WaitingForActivity) ||
		(oldState == CWebSocketState::SendingSendBuffer1) ||
		(oldState == CWebSocketState::SendingSendBuffer2))
	{
		_callbackList.onClosing(WINHTTP_WEB_SOCKET_ABORTED_CLOSE_STATUS, nullptr, false);
	}
	else if (oldState == CWebSocketState::SendingCloseFrame1)
	{
		_callbackList.onClose(WINHTTP_WEB_SOCKET_ABORTED_CLOSE_STATUS, nullptr, false);
	}
	else if (oldState == CWebSocketState::SendingCloseFrame2)
	{
	}
	/*else
	{
		CWebSocketOnError();
	}*/
	_saq.QueueAsyncWork([=]() {
		ACQUIRE_MUTEX_OR_RETURN(_mMutex);
		if (_state == CWebSocketState::Done && _reconnectCount == oldReconCnt) // Make sure the callback didn't call Connect.
			CWebSocketOnClosed();
	});
}

void CWebSocket::CWebSocketOnMessage(WINHTTP_WEB_SOCKET_STATUS* status)
{
	if (_WinHttpReceive() == false)
	{
		CWebSocketOnError();
		return;
	}
	_receiveBuffer.insert(_receiveBuffer.end(), _winHttpBuffer, _winHttpBuffer + status->dwBytesTransferred);
	if ((status->eBufferType == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) || // If this fragment is the last one
		(status->eBufferType == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE))
	{
		if (status->eBufferType == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE)
		{
			_callbackList.onBinaryMessage(_receiveBuffer.data(), _receiveBuffer.size());
		}
		else if (status->eBufferType == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE)
		{
			PWSTR unicodeString = cwebsocketinternal::UTF8ToUnicode(_receiveBuffer.data(), _receiveBuffer.size());
			if (unicodeString != nullptr)
			{
				_callbackList.onUTF8Message(unicodeString);
				delete[] unicodeString;
			}
			else
				CWebSocketOnError();
		}
		//TODO: Use a secure vector class to handle confidential data, using SecureZeroMemory.
		_receiveBuffer.clear();
	}
}

void CWebSocket::CWebSocketOnWriteComplete()
{
	/*
		assert(_sendBuffer.size() > 0);
	*/
	_sendBuffer.pop();
	if (_sendBuffer.size())
	{
		std::pair<std::vector<BYTE>, WINHTTP_WEB_SOCKET_BUFFER_TYPE> message = _sendBuffer.front(); //TODO: As an optimization, we can use move semantics here.
		DWORD dwError = WinHttpWebSocketSend(_hWebSocket,
			message.second,
			(PVOID)message.first.data(),
			message.first.size());
		if (dwError != ERROR_SUCCESS)
			CWebSocketOnError();
	}
	else
	{
		CWebSocketOnSendBufferSent();
	}
}

void CWebSocket::CWebSocketOnSendRequestComplete()
{
	_state = CWebSocketState::ReceivingUpgradeResponse; //WinHttpReceiveResponse can operate synchronously.
	BOOL fStatus = WinHttpReceiveResponse(_hRequest, 0);
	if (fStatus == FALSE)
		CWebSocketOnError();
}

void CWebSocket::CWebSocketOnReceiveResponseComplete()
{
	_hWebSocket = WinHttpWebSocketCompleteUpgrade(_hRequest, (DWORD_PTR)this);
	if (_hWebSocket != NULL)
		CWebSocketOnOpen();
	else
		CWebSocketOnError();
}

HRESULT CWebSocket::_CreateSessionConnectionHandles()
{
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
			return S_OK;
	}
	return E_FAIL;
}

bool CWebSocket::_SendUpgradeRequest()
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
					return true;
				}
			}
		}
	}
	return false;
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
			SetEvent(cws->_eRequestHandleClosed);
		else if (hInternet == cws->_hWebSocket)
			SetEvent(cws->_eWebSocketHandleClosed);
		return;
	}

	const HANDLE handles[2] = { cws->_eDrainWinHttpCallbacks, cws->_mMutex };
	DWORD waitResult = WaitForMultipleObjects(2, handles, FALSE, INFINITE);

	if (waitResult == WAIT_OBJECT_0) // _eDrainWinHttpCallbacks event is signaled.
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
			WINHTTP_WEB_SOCKET_STATUS* statusInfo = (WINHTTP_WEB_SOCKET_STATUS *)lpvStatusInformation;
			if (statusInfo->eBufferType == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)
				cws->CWebSocketOnClosing();
			else
				cws->CWebSocketOnMessage(statusInfo);
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
				; // Do nothing. WinHttp notifies us that our last receive call failed because WinHttpWebSocketClose is called on the websocket.
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
	ReleaseMutex(cws->_mMutex);
}

bool CWebSocket::Initialize(const WCHAR *__serverName, INTERNET_PORT __port, const WCHAR *__path, bool __secure)
{
	if (_initialized)
		return false;
	_initialized = true;

	HRESULT hr = E_FAIL;

	_secure = __secure;
	_port = __port;
	if (_saq.Initialize())
		_mMutex = CreateMutex(NULL, FALSE, NULL);
	if (_mMutex != nullptr)
		_eDrainWinHttpCallbacks = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (_eDrainWinHttpCallbacks != nullptr)
		_eWebSocketHandleClosed = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (_eWebSocketHandleClosed != nullptr)
		_eRequestHandleClosed = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (_eRequestHandleClosed != nullptr)
		hr = SHStrDupW(__serverName, &_serverName);
	if (SUCCEEDED(hr))
		hr = SHStrDupW(__path, &_path);
	if (SUCCEEDED(hr))
		hr = _CreateSessionConnectionHandles();
	return(SUCCEEDED(hr));
}
void CWebSocket::_ClientSendBinaryOrUTF8(const BYTE *message, size_t length, WINHTTP_WEB_SOCKET_BUFFER_TYPE bufferType)
{
	if ((_state != CWebSocketState::WaitingForActivity) &&
		(_state != CWebSocketState::ReceivedCloseFrame2))
	{
		CWebSocketOnError();
		return;
	}
	_sendBuffer.push(std::make_pair(std::vector<BYTE>(message, message + length), bufferType));
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
	std::vector<BYTE> msgc(message, message + length);
	_saq.QueueAsyncWork([=]() {
		ACQUIRE_MUTEX_OR_RETURN(_mMutex);

		_ClientSendBinaryOrUTF8(msgc.data(), length, WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE);
	});
}
void CWebSocket::SendWString(const WCHAR *message)
{
	std::vector<BYTE> UTF8Message;
	if (cwebsocketinternal::UnicodeToUTF8(message, UTF8Message))
		_saq.QueueAsyncWork([=]() {
		ACQUIRE_MUTEX_OR_RETURN(_mMutex);

		_ClientSendBinaryOrUTF8(UTF8Message.data(), UTF8Message.size(), WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE);
	});
	else
		_saq.QueueAsyncWork([=]() {
		ACQUIRE_MUTEX_OR_RETURN(_mMutex);

		CWebSocketOnError();
	});
}
void CWebSocket::SendUTF8String(const BYTE *message, size_t length)
{
	std::vector<BYTE> msgc(message, message+length);
	_saq.QueueAsyncWork([=]() {
		ACQUIRE_MUTEX_OR_RETURN(_mMutex);

		_ClientSendBinaryOrUTF8(msgc.data(), length, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE);
	});
}
void CWebSocket::SendWStringAsBinary(const WCHAR *message)
{
	std::wstring msgc(message);
	_saq.QueueAsyncWork([=]() {
		ACQUIRE_MUTEX_OR_RETURN(_mMutex);

		size_t length = msgc.length() * sizeof(WCHAR);
		_ClientSendBinaryOrUTF8((const BYTE*)msgc.c_str(), length, WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE);
	});
}

void CWebSocket::Close(USHORT usStatus, const WCHAR *reason)
{
	std::vector<BYTE> UTF8Reason;
	if (cwebsocketinternal::UnicodeToUTF8(reason, UTF8Reason) == false)
		_saq.QueueAsyncWork([=]() {
		ACQUIRE_MUTEX_OR_RETURN(_mMutex);

		CWebSocketOnError();
	});
	else
		_saq.QueueAsyncWork([=]() {
		ACQUIRE_MUTEX_OR_RETURN(_mMutex);

		if ((_state != CWebSocketState::WaitingForActivity) &&
			(_state != CWebSocketState::ReceivedCloseFrame2))
		{
			CWebSocketOnError();
			return;
		}
		if (_state == CWebSocketState::ReceivedCloseFrame2)
			_state = CWebSocketState::SendingSendBuffer2; // Closing handshake is initiated by the server.
		else // _state == CWebSocketState::WaitingForActivity
			_state = CWebSocketState::SendingSendBuffer1; // Closing handshake is initiated by us.
		_closeStatus = usStatus;
		_UTF8CloseReason = UTF8Reason;
		if (_sendBuffer.size() == 0)
		{
			CWebSocketOnSendBufferSent();
		}
	});
}

CWebSocket& CWebSocket::onOpen(CWebSocketOnOpenCallback cb)
{
	_saq.QueueAsyncWork([=]() {
		ACQUIRE_MUTEX_OR_RETURN(_mMutex);
		_callbackList.onOpen = cb;
	});
	return *this;
}

CWebSocket& CWebSocket::onBinaryMessage(CWebSocketOnBinaryMessageCallback cb)
{
	_saq.QueueAsyncWork([=]() {
		ACQUIRE_MUTEX_OR_RETURN(_mMutex);
		_callbackList.onBinaryMessage = cb;
	});
	return *this;
}

CWebSocket& CWebSocket::onUTF8Message(CWebSocketOnUTF8MessageCallback cb)
{
	_saq.QueueAsyncWork([=]() {
		ACQUIRE_MUTEX_OR_RETURN(_mMutex);
		_callbackList.onUTF8Message = cb;
	});
	return *this;
}

CWebSocket& CWebSocket::onClose(CWebSocketOnCloseCallback cb)
{
	_saq.QueueAsyncWork([=]() {
		ACQUIRE_MUTEX_OR_RETURN(_mMutex);
		_callbackList.onClose = cb;
	});
	return *this;
}

CWebSocket& CWebSocket::onClosing(CWebSocketOnClosingCallback cb)
{
	_saq.QueueAsyncWork([=]() {
		ACQUIRE_MUTEX_OR_RETURN(_mMutex);
		_callbackList.onClosing = cb;
	});
	return *this;
}

CWebSocket& CWebSocket::onClosed(CWebSocketOnClosedCallback cb)
{
	_saq.QueueAsyncWork([=]() {
		ACQUIRE_MUTEX_OR_RETURN(_mMutex);
		_callbackList.onClosed = cb;
	});
	return *this;
}

CWebSocket& CWebSocket::onError(CWebSocketOnErrorCallback cb)
{
	_saq.QueueAsyncWork([=]() {
		ACQUIRE_MUTEX_OR_RETURN(_mMutex);
		_callbackList.onError = cb;
	});
	return *this;
}

void CWebSocket::Connect(DWORD delayms)
{
	_saq.QueueAsyncWork([=]() {
		ACQUIRE_MUTEX_OR_RETURN(_mMutex);

		_reconnectCount++;

		if (_state == CWebSocketState::ConnectPending)
		{
			CWebSocketOnError();
			return;
		}

		_Abort();
		if (delayms == 0)
		{
			if (_SendUpgradeRequest() == false)
				CWebSocketOnError();
		}
		else
		{
			_state = CWebSocketState::ConnectPending;
			_at.Set(delayms, [=]() {
				if (_SendUpgradeRequest() == false)
					CWebSocketOnError();
			});
		}
	});
}

void CWebSocket::Abort()
{
	_saq.QueueAsyncWork([=]() {
		ACQUIRE_MUTEX_OR_RETURN(_mMutex);

		if ((_state == CWebSocketState::NoTcpConnection) ||
			(_state == CWebSocketState::ConnectPending) ||
			(_state == CWebSocketState::Done) ||
			(_state == CWebSocketState::Error))
		{
			CWebSocketOnError();
			return;
		}

		_state = CWebSocketState::NoTcpConnection;
		_Abort();
	});
}