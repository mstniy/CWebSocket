#pragma once

namespace cwebsocketinternal // Encapsulate these methods in a seperate namespace so that we don't clutter the global one.
{
	//Encodes unicodeString in UTF8, returns the result in UTF8String.
	//Returns true for success, false for failure.
	//Assumes unicodeString is a null-terminated string.
	//Returned UTF8 string doesn't have a null terminator.
	bool UnicodeToUTF8(PCWSTR unicodeString, std::vector<BYTE> &UTF8String);

	// Encodes the given UTF-8 string into unicode.
	// UTF8String: The not-null-terminated UTF-8 string to be encoded into unicode.
	// byteLength: The length of the UTF-8 String, in bytes.
	// byteLength is checked to verify that the given string really is not null-terminated.
	// Returns a null-terminated unicode string allocated using 'new[]' if successful, or NULL if not.
	PWSTR UTF8ToUnicode(const BYTE *UTF8String, size_t byteLength);
};