#include <windows.h>
#include <vector>

namespace cwebsocketinternal
{
	bool UnicodeToUTF8(PCWSTR unicodeString, std::vector<BYTE> &UTF8String)
	{
		bool result = false;
		DWORD wcResult;
		UTF8String.resize(0);
		DWORD bufSize = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, unicodeString, -1, nullptr, 0, nullptr, nullptr);
		if (bufSize > 0)
		{
			UTF8String.resize(bufSize);
			wcResult = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, unicodeString, -1, (LPSTR)UTF8String.data(), bufSize, nullptr, nullptr);
			if ((wcResult == bufSize) &&
				(UTF8String.back() == 0)) // WCtoMB will append a null terminator to the result.
			{
				UTF8String.pop_back(); // Remove the trailing null-terminator.
				result = true;
			}
		}
		return result;
	}

	bool VerifyIsNotNullTerminatedString(const BYTE *str, size_t length)
	{
		size_t i;
		for (i = 0; i < length; i++)
			if (str[i] == '\0')
				break;
		return(i == length);
	}

	PWSTR UTF8ToUnicode(const BYTE *UTF8String, size_t byteLength)
	{
		PWSTR result = nullptr;

		if (byteLength >= 0)
		{
			if (VerifyIsNotNullTerminatedString(UTF8String, byteLength))
			{
				int bufSize = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (LPCCH)UTF8String, byteLength, NULL, 0);
				if (bufSize >= 0)
				{
					result = new(std::nothrow) WCHAR[bufSize + 1]; // +1 for the null-terminator
					if (result != nullptr)
					{
						int mbResult = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (LPCCH)UTF8String, byteLength, result, bufSize);
						if (mbResult != bufSize)
						{
							delete[] result;
							result = nullptr;
						}
						else
							result[bufSize] = L'\0'; //Append a null-terminator.
					}
				}
			}
		}
		return result;
	}
};