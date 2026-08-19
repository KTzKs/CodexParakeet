#include "pch.h"
#include "framework.h"
#include "Common.h"
#include <cstring>
#include <string>

CString DecodeMessageArgument(const TCHAR* argument)
{
	if (argument == nullptr) return CString();
	const CString prefix = _T("--message-base64=");
	if (_tcsncmp(argument, prefix, prefix.GetLength()) != 0)
		return CString(argument);

	const std::string encoded(CT2A(argument + prefix.GetLength(), CP_UTF8));
	static constexpr char alphabet[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string bytes;
	int value = 0;
	int bits = -8;
	for (const char c : encoded)
	{
		if (c == '=') break;
		const char* found = std::strchr(alphabet, c);
		if (found == nullptr) continue;
		value = (value << 6) + static_cast<int>(found - alphabet);
		bits += 6;
		if (bits >= 0)
		{
			bytes.push_back(static_cast<char>((value >> bits) & 0xFF));
			bits -= 8;
		}
	}
	if (bytes.empty()) return CString();
	const int length = MultiByteToWideChar(CP_UTF8, 0, bytes.data(),
		static_cast<int>(bytes.size()), nullptr, 0);
	if (length <= 0) return CString();
	std::wstring decoded(static_cast<size_t>(length), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()),
		decoded.data(), length);
	return CString(decoded.c_str(), static_cast<int>(decoded.size()));
}
