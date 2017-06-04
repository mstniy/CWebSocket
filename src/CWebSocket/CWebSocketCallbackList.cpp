#include <new>

#include "CWebSocketCallbackList.h"

HRESULT CloneCWebSocketCallbackList(const CWebSocketCallbackList *source, CWebSocketCallbackList **destination)
{
	(*destination) = new(std::nothrow) CWebSocketCallbackList;
	if ((*destination) == nullptr)
		return E_FAIL;
	(*destination)->onClose = source->onClose;
	(*destination)->onClosing = source->onClosing;
	(*destination)->onClosed = source->onClosed;
	(*destination)->onError = source->onError;
	(*destination)->onBinaryMessage = source->onBinaryMessage;
	(*destination)->onUTF8Message = source->onUTF8Message;
	(*destination)->onOpen = source->onOpen;
	return S_OK;
}