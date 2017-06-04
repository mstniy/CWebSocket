#include <windows.h>
#include <vector>

bool UnicodeToUTF8(PCWSTR unicodeString, std::vector<BYTE> &utf8String)
{
	bool result = false;
	DWORD wcResult;
	utf8String.resize(0);
	DWORD bufSize = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, unicodeString, -1, nullptr, 0, nullptr, nullptr);
	if (bufSize > 0)
	{
		utf8String.resize(bufSize);
		wcResult = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, unicodeString, -1, (LPSTR)utf8String.data(), bufSize, nullptr, nullptr);
		if ((wcResult == bufSize) &&
			(utf8String.back() == 0)) // WCtoMB will append a null terminator to the result.
		{
			utf8String.pop_back(); // Remove the trailing null-terminator.
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

PWSTR UTF8ToUnicode(const BYTE *utf8String, size_t byteLength)
{
	PWSTR result = nullptr;

	if (byteLength > 0)
	{
		if (VerifyIsNotNullTerminatedString(utf8String, byteLength))
		{
			int bufSize = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (LPCCH)utf8String, byteLength, NULL, 0);
			if (bufSize > 0)
			{
				result = new(std::nothrow) WCHAR[bufSize + 1]; // +1 for the null-terminator
				if (result != nullptr)
				{
					int mbResult = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (LPCCH)utf8String, byteLength, result, bufSize);
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