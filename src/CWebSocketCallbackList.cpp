#include "CWebSocketCallbackList.h"

namespace cwebsocketinternal
{
	CWebSocketCallbackList::CWebSocketCallbackList()
	{
		onOpen = []() {};
		onBinaryMessage = [](const BYTE *message, size_t length) {};
		onUTF8Message = [](PCWSTR message) {};
		onClose = [](USHORT code, PCWSTR reason, bool wasClean) {};
		onClosing = [](USHORT code, PCWSTR reason, bool wasClean) {};
		onClosed = []() {};
		onError = []() {};
	}
};