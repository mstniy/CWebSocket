#pragma once

//Encodes unicodeString in Utf8, returns the result in utf8String.
//Returns true for success, false for failure.
//Assumes unicodeString is a null-terminated string.
//Returned utf8 string doesn't have a null terminator.
bool UnicodeToUTF8(PCWSTR unicodeString, std::vector<BYTE> &utf8String);

// Encodes the given UTF-8 string into unicode.
// utf8String: The not-null-terminated UTF-8 string to be encoded into unicode.
// byteLength: The length of the UTF-8 String, in bytes.
// byteLength is checked to verify that the given string really is not null-terminated.
// Returns a null-terminated unicode string allocated using 'new[]' if successful, or NULL if not.
PWSTR UTF8ToUnicode(const BYTE *utf8String, size_t byteLength);