﻿// TortoiseSVN - a Windows shell extension for easy version control

// Copyright (C) 2003-2011, 2013-2018, 2020-2021, 2023 - TortoiseSVN
// Copyright (C) 2016 - TortoiseGit

// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//
#include "stdafx.h"
// ReSharper disable once CppUnusedIncludeDirective
#include "UnicodeUtils.h"
#include "StringUtils.h"
// ReSharper disable once CppUnusedIncludeDirective
#include "ClipboardHelper.h"
#include "SmartHandle.h"
#include <memory>
#include <WinCrypt.h>
#include <atlstr.h>

#pragma comment(lib, "Crypt32.lib")

int strwildcmp(const char* wild, const char* string)
{
    const char* cp = nullptr;
    const char* mp = nullptr;
    while ((*string) && (*wild != '*'))
    {
        if ((*wild != *string) && (*wild != '?'))
        {
            return 0;
        }
        wild++;
        string++;
    }
    while (*string)
    {
        if (*wild == '*')
        {
            if (!*++wild)
            {
                return 1;
            }
            mp = wild;
            cp = string + 1;
        }
        else if ((*wild == *string) || (*wild == '?'))
        {
            wild++;
            string++;
        }
        else
        {
            wild   = mp;
            string = cp++;
        }
    }

    while (*wild == '*')
    {
        wild++;
    }
    return !*wild;
}

int wcswildcmp(const wchar_t* wild, const wchar_t* string)
{
    const wchar_t* cp = nullptr;
    const wchar_t* mp = nullptr;
    while ((*string) && (*wild != '*'))
    {
        if ((*wild != *string) && (*wild != '?'))
        {
            return 0;
        }
        wild++;
        string++;
    }
    while (*string)
    {
        if (*wild == '*')
        {
            if (!*++wild)
            {
                return 1;
            }
            mp = wild;
            cp = string + 1;
        }
        else if ((*wild == *string) || (*wild == '?'))
        {
            wild++;
            string++;
        }
        else
        {
            wild   = mp;
            string = cp++;
        }
    }

    while (*wild == '*')
    {
        wild++;
    }
    return !*wild;
}

#ifdef _MFC_VER

void CStringUtils::RemoveAccelerators(CString& text)
{
    int pos = 0;
    while ((pos = text.Find('&', pos)) >= 0)
    {
        if (text.GetLength() > (pos - 1))
        {
            if (text.GetAt(pos + 1) != ' ')
                text.Delete(pos);
        }
        pos++;
    }
}

bool CStringUtils::WriteAsciiStringToClipboard(const CStringA& sClipdata, LCID lcid, HWND hOwningWnd)
{
    CClipboardHelper clipboardHelper;
    if (!clipboardHelper.Open(hOwningWnd))
        return false;

    EmptyClipboard();
    HGLOBAL hClipboardData = CClipboardHelper::GlobalAlloc(sClipdata.GetLength() + 1);
    if (!hClipboardData)
        return false;

    char* pchData = static_cast<char*>(GlobalLock(hClipboardData));
    if (!pchData)
    {
        GlobalFree(hClipboardData);
        return false;
    }

    strcpy_s(pchData, sClipdata.GetLength() + 1, static_cast<LPCSTR>(sClipdata));
    GlobalUnlock(hClipboardData);
    if (!SetClipboardData(CF_TEXT, hClipboardData))
    {
        GlobalFree(hClipboardData);
        return false;
    }

    HANDLE hlocmem = CClipboardHelper::GlobalAlloc(sizeof(LCID));
    if (!hlocmem)
        return false;

    PLCID plcid = static_cast<PLCID>(GlobalLock(hlocmem));
    if (plcid)
    {
        *plcid = lcid;
        SetClipboardData(CF_LOCALE, static_cast<HANDLE>(plcid));
    }
    GlobalUnlock(hlocmem);

    return true;
}

bool CStringUtils::WriteAsciiStringToClipboard(const CStringW& sClipdata, HWND hOwningWnd)
{
    CClipboardHelper clipboardHelper;
    if (!clipboardHelper.Open(hOwningWnd))
        return false;

    EmptyClipboard();
    HGLOBAL hClipboardData = CClipboardHelper::GlobalAlloc((sClipdata.GetLength() + 1) * sizeof(WCHAR));
    if (!hClipboardData)
        return false;

    WCHAR* pchData = static_cast<WCHAR*>(GlobalLock(hClipboardData));
    if (!pchData)
        return false;

    _tcscpy_s(pchData, sClipdata.GetLength() + 1, static_cast<LPCWSTR>(sClipdata));
    GlobalUnlock(hClipboardData);
    if (!SetClipboardData(CF_UNICODETEXT, hClipboardData))
        return false;

    // no need to also set CF_TEXT : the OS does this
    // automatically.
    return true;
}

bool CStringUtils::WriteHtmlAndStringToClipboard(const CStringW& sHtml, const CStringW& sPlainText, HWND hOwningWnd)
{
    // Convert the HTML fragment to UTF-8 and generate a CF_HTML block for that fragment

    // Example CF_HTML clipboard data:
    //
    // "Version:0.9\r\n"
    // "StartHTML:0000000105\r\n"
    // "EndHTML:0000000190\r\n"
    // "StartFragment:0000000141\r\n"
    // "EndFragment:0000000154\r\n"
    // "<HTML>\r\n"
    // "<BODY>\r\n"
    // "<!--StartFragment-->HTML_FRAGMENT<!--EndFragment-->\r\n"
    // "</BODY>\r\n"
    // "</HTML>"

    CStringA              sHtmlUtf8         = CUnicodeUtils::GetUTF8(sHtml);

    constexpr const char* descStartHTML     = "Version:0.9\r\nStartHTML:";
    constexpr const char* descEndHTML       = "\r\nEndHTML:";
    constexpr const char* descStartFragment = "\r\nStartFragment:";
    constexpr const char* descEndFragment   = "\r\nEndFragment:";
    constexpr const char* descSuffix        = "\r\n";
    constexpr const char* htmlPrefix        = "<HTML>\r\n<BODY>\r\n<!--StartFragment-->";
    constexpr const char* htmlSuffix        = "<!--EndFragment-->\r\n</BODY>\r\n</HTML>";
    constexpr size_t      numWidth          = 10;
    constexpr size_t      offsetStartHTML   = std::char_traits<char>::length(descStartHTML) + numWidth +
                                       std::char_traits<char>::length(descEndHTML) + numWidth +
                                       std::char_traits<char>::length(descStartFragment) + numWidth +
                                       std::char_traits<char>::length(descEndFragment) + numWidth +
                                       std::char_traits<char>::length(descSuffix);
    constexpr size_t offsetStartFragment = offsetStartHTML + std::char_traits<char>::length(htmlPrefix);
    size_t           offsetEndFragment   = offsetStartFragment + sHtmlUtf8.GetLength();
    size_t           offsetEndHTML       = offsetEndFragment + strlen(htmlSuffix);

    CStringA sHtmlData;
    sHtmlData.Format("%s%010u"
                     "%s%010u"
                     "%s%010u"
                     "%s%010u"
                     "%s"
                     "%s"
                     "%s"
                     "%s",
                     descStartHTML, static_cast<unsigned int>(offsetStartHTML),
                     descEndHTML, static_cast<unsigned int>(offsetEndHTML),
                     descStartFragment, static_cast<unsigned int>(offsetStartFragment),
                     descEndFragment, static_cast<unsigned int>(offsetEndFragment),
                     descSuffix,
                     htmlPrefix,
                     static_cast<const char*>(sHtmlUtf8),
                     htmlSuffix);

    UINT htmlFormat = RegisterClipboardFormat(L"HTML Format");
    if (htmlFormat == 0)
        return false;

    CClipboardHelper clipboardHelper;
    if (!clipboardHelper.Open(hOwningWnd))
        return false;

    EmptyClipboard();

    HGLOBAL hHtmlData = CClipboardHelper::GlobalAlloc(sHtmlData.GetLength() + 1);
    if (!hHtmlData)
        return false;

    char* pchHtmlData = static_cast<char*>(GlobalLock(hHtmlData));
    if (!pchHtmlData)
    {
        GlobalFree(hHtmlData);
        return false;
    }
    strcpy_s(pchHtmlData, sHtmlData.GetLength() + 1, static_cast<LPCSTR>(sHtmlData));
    GlobalUnlock(hHtmlData);

    if (!SetClipboardData(htmlFormat, hHtmlData))
    {
        GlobalFree(hHtmlData);
        return false;
    }
    // Also write a plain text block as CF_UNICODETEXT since most applications don't accept CF_HTML
    HGLOBAL hClipboardData = CClipboardHelper::GlobalAlloc((sPlainText.GetLength() + 1) * sizeof(WCHAR));
    if (!hClipboardData)
        return false;

    WCHAR* pchData = static_cast<WCHAR*>(GlobalLock(hClipboardData));
    if (!pchData)
    {
        GlobalFree(hClipboardData);
        return false;
    }

    _tcscpy_s(pchData, sPlainText.GetLength() + 1, static_cast<LPCWSTR>(sPlainText));
    GlobalUnlock(hClipboardData);
    if (!SetClipboardData(CF_UNICODETEXT, hClipboardData))
    {
        GlobalFree(hClipboardData);
        return false;
    }

    // no need to also set CF_TEXT : the OS does this
    // automatically.
    return true;
}

bool CStringUtils::WriteDiffToClipboard(const CStringA& sClipdata, HWND hOwningWnd)
{
    UINT cFormat = RegisterClipboardFormat(L"TSVN_UNIFIEDDIFF");
    if (cFormat == 0)
        return false;
    CClipboardHelper clipboardHelper;
    if (!clipboardHelper.Open(hOwningWnd))
        return false;

    EmptyClipboard();
    HGLOBAL hClipboardData = CClipboardHelper::GlobalAlloc(sClipdata.GetLength() + 1);
    if (!hClipboardData)
        return false;

    char* pchData = static_cast<char*>(GlobalLock(hClipboardData));
    if (!pchData)
    {
        GlobalFree(hClipboardData);
        return false;
    }

    strcpy_s(pchData, sClipdata.GetLength() + 1, static_cast<LPCSTR>(sClipdata));
    GlobalUnlock(hClipboardData);
    if (!SetClipboardData(cFormat, hClipboardData))
    {
        GlobalFree(hClipboardData);
        return false;
    }
    if (!SetClipboardData(CF_TEXT, hClipboardData))
        return false;

    CString sClipdataW      = CUnicodeUtils::GetUnicode(sClipdata);
    auto    hClipboardDataW = CClipboardHelper::GlobalAlloc((sClipdataW.GetLength() + 1) * sizeof(wchar_t));
    if (!hClipboardDataW)
        return false;

    wchar_t* pchDataW = static_cast<wchar_t*>(GlobalLock(hClipboardDataW));
    if (!pchDataW)
        return false;

    wcscpy_s(pchDataW, sClipdataW.GetLength() + 1, static_cast<LPCWSTR>(sClipdataW));
    GlobalUnlock(hClipboardDataW);
    if (!SetClipboardData(CF_UNICODETEXT, hClipboardDataW))
        return false;

    return true;
}

bool CStringUtils::ReadStringFromTextFile(const CString& path, CString& text)
{
    if (!PathFileExists(path))
        return false;
    try
    {
        CStdioFile file;
        if (!file.Open(path, CFile::modeRead | CFile::shareDenyWrite))
            return false;

        CStringA fileContent;
        UINT     filelength = static_cast<UINT>(file.GetLength());
        int      bytesRead  = static_cast<int>(file.Read(fileContent.GetBuffer(filelength), filelength));
        fileContent.ReleaseBuffer(bytesRead);
        text = CUnicodeUtils::GetUnicode(fileContent);
        file.Close();
    }
    catch (CFileException* pE)
    {
        text.Empty();
        pE->Delete();
    }
    return true;
}

#endif // #ifdef _MFC_VER

#if defined(CSTRING_AVAILABLE) || defined(_MFC_VER)
BOOL CStringUtils::WildCardMatch(const CString& wildcard, const CString& string)
{
    return _tcswildcmp(wildcard, string);
}

CString CStringUtils::LinesWrap(const CString& longstring, int limit /* = 80 */, bool bCompactPaths /* = true */, bool bForceWrap /* = false */, int tabSize /* = 4 */)
{
    CString retString;
    if ((longstring.GetLength() < limit) || (limit == 0))
        return longstring; // no wrapping needed.
    // now start breaking the string into lines

    int     linePos    = 0;
    int     linePosOld = 0;
    CString temp;
    while ((linePos = longstring.Find('\n', linePos)) >= 0)
    {
        temp = longstring.Mid(linePosOld, linePos - linePosOld);
        if ((linePos + 1) < longstring.GetLength())
            linePos++;
        else
            break;
        linePosOld = linePos;
        if (!retString.IsEmpty())
            retString += L"\n";
        retString += WordWrap(temp, limit, bCompactPaths, bForceWrap, tabSize);
    }
    temp = longstring.Mid(linePosOld);
    if (!temp.IsEmpty())
        retString += L"\n";
    retString += WordWrap(temp, limit, bCompactPaths, bForceWrap, tabSize);
    retString.Trim();
    return retString;
}

CString CStringUtils::WordWrap(const CString& longstring, int limit, bool bCompactPaths, bool bForceWrap, int tabSize)
{
    int     nLength = longstring.GetLength();
    CString retString;

    if (limit < 0)
        limit = 0;

    int nLineStart = 0;
    int nLineEnd   = 0;
    int tabOffset  = 0;
    for (int i = 0; i < nLength; ++i)
    {
        if (i - nLineStart + tabOffset >= limit)
        {
            if (nLineEnd == nLineStart)
            {
                if (bForceWrap && !bCompactPaths)
                    nLineEnd = i;
                else
                {
                    while ((i < nLength) && (longstring[i] != ' ') && (longstring[i] != '\t'))
                        ++i;
                    nLineEnd = i;
                }
            }
            if (bCompactPaths)
            {
                CString longLine  = longstring.Mid(nLineStart, nLineEnd - nLineStart).Left(MAX_PATH - 1);
                bool    compacted = false;
                if ((bCompactPaths) && (longLine.GetLength() < MAX_PATH))
                {
                    if (((!PathIsFileSpec(longLine)) && longLine.Find(':') < 3) || (PathIsURL(longLine) || (longLine.Find('^') < 3)))
                    {
                        wchar_t buf[MAX_PATH] = {0};
                        PathCompactPathEx(buf, longLine, limit + 1, 0);
                        longLine  = buf;
                        compacted = true;
                    }
                }
                if (!compacted && bForceWrap)
                {
                    nLineEnd = i;
                    retString += longstring.Mid(nLineStart, nLineEnd - nLineStart);
                }
                else
                    retString += longLine;
            }
            else
                retString += longstring.Mid(nLineStart, nLineEnd - nLineStart);
            retString += L"\n";
            tabOffset  = 0;
            nLineStart = nLineEnd;
        }
        if (longstring[i] == ' ')
            nLineEnd = i;
        if (longstring[i] == '\t')
        {
            tabOffset += (tabSize - i % tabSize);
            nLineEnd = i;
        }
    }
    if (bCompactPaths)
    {
        CString longLine = longstring.Mid(nLineStart).Left(MAX_PATH - 1);
        if ((bCompactPaths) && (longLine.GetLength() < MAX_PATH))
        {
            if (((!PathIsFileSpec(longLine)) && longLine.Find(':') < 3) || (PathIsURL(longLine)))
            {
                wchar_t buf[MAX_PATH] = {0};
                PathCompactPathEx(buf, longLine, limit + 1, 0);
                longLine = buf;
            }
        }
        retString += longLine;
    }
    else
        retString += longstring.Mid(nLineStart);

    return retString;
}

std::vector<CString> CStringUtils::WordWrap(const CString& longstring, int limit, int tabSize)
{
    int                  nLength = longstring.GetLength();
    std::vector<CString> retVec;

    if (limit < 0)
        limit = 0;

    int nLineStart = 0;
    int nLineEnd   = 0;
    int tabOffset  = 0;
    for (int i = 0; i < nLength; ++i)
    {
        if (i - nLineStart + tabOffset >= limit)
        {
            if (nLineEnd == nLineStart)
            {
                nLineEnd = i;
            }
            auto sMid = longstring.Mid(nLineStart, nLineEnd - nLineStart);
            retVec.push_back(sMid);

            tabOffset  = 0;
            nLineStart = nLineEnd;
        }
        if (longstring[i] == ' ')
            nLineEnd = i;
        if (longstring[i] == '\t')
        {
            tabOffset += (tabSize - i % tabSize);
            nLineEnd = i;
        }
    }
    auto sMid = longstring.Mid(nLineStart);
    retVec.push_back(sMid);

    return retVec;
}

int CStringUtils::GetMatchingLength(const CString& lhs, const CString& rhs)
{
    int lhsLength = lhs.GetLength();
    int rhsLength = rhs.GetLength();
    int maxResult = min(lhsLength, rhsLength);

    LPCWSTR pLhs = lhs;
    LPCWSTR pRhs = rhs;

    for (int i = 0; i < maxResult; ++i)
        if (pLhs[i] != pRhs[i])
            return i;

    return maxResult;
}

int CStringUtils::FastCompareNoCase(const CStringW& lhs, const CStringW& rhs)
{
    // attempt Latin-only comparison

    INT_PTR        count = min(lhs.GetLength(), rhs.GetLength() + 1);
    const wchar_t* left  = lhs;
    const wchar_t* right = rhs;
    for (const wchar_t* last = left + count + 1; left < last; ++left, ++right)
    {
        int leftChar  = *left;
        int rightChar = *right;

        int diff = leftChar - rightChar;
        if (diff != 0)
        {
            // case-sensitive comparison found a difference

            if ((leftChar | rightChar) >= 0x80)
            {
                // non-Latin char -> fall back to CRT code
                // (full comparison required as we might have
                // skipped special chars / UTF plane selectors)

                return _wcsicmp(lhs, rhs);
            }

            // normalize to lower case

            if ((leftChar >= 'A') && (leftChar <= 'Z'))
                leftChar += 'a' - 'A';
            if ((rightChar >= 'A') && (rightChar <= 'Z'))
                rightChar += 'a' - 'A';

            // compare again

            diff = leftChar - rightChar;
            if (diff != 0)
                return diff;
        }
    }

    // must be equal (both ended with a 0)

    return 0;
}

bool CStringUtils::WriteStringToTextFile(const std::wstring& path, const std::wstring& text, bool bUTF8 /* = true */)
{
    DWORD     dwWritten = 0;
    CAutoFile hFile     = CreateFile(path.c_str(), GENERIC_WRITE, FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (!hFile)
        return false;

    if (bUTF8)
    {
        std::string buf = CUnicodeUtils::StdGetUTF8(text);
        if (!WriteFile(hFile, buf.c_str(), static_cast<DWORD>(buf.length()), &dwWritten, nullptr))
        {
            return false;
        }
    }
    else
    {
        if (!WriteFile(hFile, text.c_str(), static_cast<DWORD>(text.length()), &dwWritten, nullptr))
        {
            return false;
        }
    }
    return true;
}

bool CStringUtils::WriteStringToTextFile(const std::wstring& path, const std::string& text)
{
    DWORD     dwWritten = 0;
    CAutoFile hFile     = CreateFile(path.c_str(), GENERIC_WRITE, FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (!hFile)
        return false;

    if (!WriteFile(hFile, text.c_str(), static_cast<DWORD>(text.length()), &dwWritten, nullptr))
    {
        return false;
    }
    return true;
}

bool CStringUtils::ValidateFormatString(LPCWSTR pszFormat, ...)
{
    __try
    {
        DWORD   dwResult = 0;
        LPWSTR  pszTemp  = nullptr;
        va_list args{};

        va_start(args, pszFormat);
        dwResult = FormatMessage(FORMAT_MESSAGE_FROM_STRING |
                                     FORMAT_MESSAGE_ALLOCATE_BUFFER,
                                 pszFormat, 0, 0,
                                 reinterpret_cast<LPWSTR>(&pszTemp),
                                 0,
                                 &args);
        va_end(args);
        if (dwResult == 0)
        {
            // invalid format string
            return false;
        }

        LocalFree(pszTemp);
    }
    __except (TRUE)
    {
        // invalid format string
        return false;
    }

    return true;
}

#endif // #if defined(CSTRING_AVAILABLE) || defined(_MFC_VER)

inline static void PipeToNull(wchar_t* ptr)
{
    if (*ptr == '|')
        *ptr = '\0';
}

void CStringUtils::PipesToNulls(wchar_t* buffer, size_t length)
{
    wchar_t* ptr = buffer + length;
    while (ptr != buffer)
    {
        PipeToNull(ptr);
        ptr--;
    }
}

void CStringUtils::PipesToNulls(wchar_t* buffer)
{
    wchar_t* ptr = buffer;
    while (*ptr != 0)
    {
        PipeToNull(ptr);
        ptr++;
    }
}

std::unique_ptr<char[]> CStringUtils::Decrypt(const char* text)
{
    DWORD dwLen = 0;
    if (CryptStringToBinaryA(text, static_cast<DWORD>(strlen(text)), CRYPT_STRING_HEX, nullptr, &dwLen, nullptr, nullptr) == FALSE)
        return nullptr;

    auto strIn = std::make_unique<BYTE[]>(dwLen + 1);
    if (CryptStringToBinaryA(text, static_cast<DWORD>(strlen(text)), CRYPT_STRING_HEX, strIn.get(), &dwLen, nullptr, nullptr) == FALSE)
        return nullptr;

    DATA_BLOB blobIn;
    blobIn.cbData     = dwLen;
    blobIn.pbData     = strIn.get();
    LPWSTR    descr   = nullptr;
    DATA_BLOB blobOut = {0};
    if (CryptUnprotectData(&blobIn, &descr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &blobOut) == FALSE)
        return nullptr;
    SecureZeroMemory(blobIn.pbData, blobIn.cbData);

    auto result = std::make_unique<char[]>(blobOut.cbData + 1);
    strncpy_s(result.get(), blobOut.cbData + 1, reinterpret_cast<const char*>(blobOut.pbData), blobOut.cbData);
    SecureZeroMemory(blobOut.pbData, blobOut.cbData);
    LocalFree(blobOut.pbData);
    LocalFree(descr);
    return result;
}

std::unique_ptr<wchar_t[]> CStringUtils::Decrypt(const wchar_t* text)
{
    DWORD dwLen = 0;
    if (CryptStringToBinaryW(text, static_cast<DWORD>(wcslen(text)), CRYPT_STRING_HEX, nullptr, &dwLen, nullptr, nullptr) == FALSE)
        return nullptr;

    auto strIn = std::make_unique<BYTE[]>(dwLen + 1);
    if (CryptStringToBinaryW(text, static_cast<DWORD>(wcslen(text)), CRYPT_STRING_HEX, strIn.get(), &dwLen, nullptr, nullptr) == FALSE)
        return nullptr;

    DATA_BLOB blobIn;
    blobIn.cbData     = dwLen;
    blobIn.pbData     = strIn.get();
    LPWSTR    descr   = nullptr;
    DATA_BLOB blobOut = {0};
    if (CryptUnprotectData(&blobIn, &descr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &blobOut) == FALSE)
        return nullptr;
    SecureZeroMemory(blobIn.pbData, blobIn.cbData);

    auto result = std::make_unique<wchar_t[]>(blobOut.cbData / sizeof(wchar_t) + 1);
    wcsncpy_s(result.get(), (blobOut.cbData) / sizeof(wchar_t) + 1, reinterpret_cast<const wchar_t*>(blobOut.pbData), blobOut.cbData / sizeof(wchar_t));
    SecureZeroMemory(blobOut.pbData, blobOut.cbData);
    LocalFree(blobOut.pbData);
    LocalFree(descr);
    return result;
}

CStringA CStringUtils::Encrypt(const char* text)
{
    DATA_BLOB blobIn  = {0};
    DATA_BLOB blobOut = {0};
    CStringA  result;

    blobIn.cbData = static_cast<DWORD>(strlen(text));
    blobIn.pbData = reinterpret_cast<BYTE*>(const_cast<LPSTR>(text));
    if (CryptProtectData(&blobIn, L"TSVNAuth", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &blobOut) == FALSE)
        return result;
    DWORD dwLen = 0;
    if (CryptBinaryToStringA(blobOut.pbData, blobOut.cbData, CRYPT_STRING_HEX | CRYPT_STRING_NOCRLF, nullptr, &dwLen) == FALSE)
        return result;
    auto strOut = std::make_unique<char[]>(dwLen + 1);
    if (CryptBinaryToStringA(blobOut.pbData, blobOut.cbData, CRYPT_STRING_HEX | CRYPT_STRING_NOCRLF, strOut.get(), &dwLen) == FALSE)
        return result;
    LocalFree(blobOut.pbData);

    result = strOut.get();

    return result;
}

CStringW CStringUtils::Encrypt(const wchar_t* text)
{
    DATA_BLOB blobIn  = {0};
    DATA_BLOB blobOut = {0};
    CStringW  result;

    blobIn.cbData = static_cast<DWORD>(wcslen(text)) * sizeof(wchar_t);
    blobIn.pbData = reinterpret_cast<BYTE*>(const_cast<LPWSTR>(text));
    if (CryptProtectData(&blobIn, L"TSVNAuth", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &blobOut) == FALSE)
        return result;
    DWORD dwLen = 0;
    if (CryptBinaryToStringW(blobOut.pbData, blobOut.cbData, CRYPT_STRING_HEX | CRYPT_STRING_NOCRLF, nullptr, &dwLen) == FALSE)
        return result;
    auto strOut = std::make_unique<wchar_t[]>(dwLen + 1);
    if (CryptBinaryToStringW(blobOut.pbData, blobOut.cbData, CRYPT_STRING_HEX | CRYPT_STRING_NOCRLF, strOut.get(), &dwLen) == FALSE)
        return result;
    LocalFree(blobOut.pbData);

    result = strOut.get();

    return result;
}

BYTE hexLookup[513] = {
    "000102030405060708090a0b0c0d0e0f"
    "101112131415161718191a1b1c1d1e1f"
    "202122232425262728292a2b2c2d2e2f"
    "303132333435363738393a3b3c3d3e3f"
    "404142434445464748494a4b4c4d4e4f"
    "505152535455565758595a5b5c5d5e5f"
    "606162636465666768696a6b6c6d6e6f"
    "707172737475767778797a7b7c7d7e7f"
    "808182838485868788898a8b8c8d8e8f"
    "909192939495969798999a9b9c9d9e9f"
    "a0a1a2a3a4a5a6a7a8a9aaabacadaeaf"
    "b0b1b2b3b4b5b6b7b8b9babbbcbdbebf"
    "c0c1c2c3c4c5c6c7c8c9cacbcccdcecf"
    "d0d1d2d3d4d5d6d7d8d9dadbdcdddedf"
    "e0e1e2e3e4e5e6e7e8e9eaebecedeeef"
    "f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff"};
BYTE decLookup[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // gap before first hex digit
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9,          // 0123456789
    0, 0, 0, 0, 0, 0, 0,                   // :;<=>?@ (gap)
    10, 11, 12, 13, 14, 15,                // ABCDEF
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // GHIJKLMNOPQRS (gap)
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // TUVWXYZ[/]^_` (gap)
    10, 11, 12, 13, 14, 15                 // abcdef
};

std::string CStringUtils::ToHexString(BYTE* pSrc, int nSrcLen)
{
    WORD* pwHex  = reinterpret_cast<WORD*>(hexLookup);
    auto  dest   = std::make_unique<char[]>((nSrcLen * 2) + 1);
    WORD* pwDest = reinterpret_cast<WORD*>(dest.get());
    for (int j = 0; j < nSrcLen; j++)
    {
        *pwDest = pwHex[*pSrc];
        pwDest++;
        pSrc++;
    }
    *reinterpret_cast<BYTE*>(pwDest) = 0; // terminate the string
    return std::string(dest.get());
}

bool CStringUtils::FromHexString(const std::string& src, BYTE* pDest)
{
    if (src.size() % 2)
        return false;
    for (auto it = src.cbegin(); it != src.cend(); ++it)
    {
        if ((*it < '0') || (*it > 'f'))
            return false;
        int d = decLookup[*it] << 4;
        // no bounds check necessary, since the 'if (src.size %2)' above
        // ensures that we have always one item more available
        ++it;
        d |= decLookup[*it];
        *pDest++ = static_cast<BYTE>(d);
    }
    return true;
}

std::string CStringUtils::Encrypt(const std::string& s, const std::string& password)
{
    std::string encryptedString;
    HCRYPTPROV  hProv = NULL;
    // Get handle to user default provider.
    if (CryptAcquireContext(&hProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT | CRYPT_SILENT))
    {
        HCRYPTHASH hHash = NULL;
        // Create hash object.
        if (CryptCreateHash(hProv, CALG_SHA_512, 0, 0, &hHash))
        {
            // Hash password string.
            DWORD dwLength = static_cast<DWORD>(password.size());
            if (CryptHashData(hHash, reinterpret_cast<BYTE*>(const_cast<char*>(password.c_str())), dwLength, 0))
            {
                // Create block cipher session key based on hash of the password.
                HCRYPTKEY hKey = NULL;
                if (CryptDeriveKey(hProv, CALG_AES_256, hHash, CRYPT_EXPORTABLE, &hKey))
                {
                    // Determine number of bytes to encrypt at a time.
                    std::string starname = "*";
                    starname += s;

                    dwLength    = static_cast<DWORD>(starname.size());
                    auto buffer = std::make_unique<BYTE[]>(dwLength + 1024);
                    memcpy(buffer.get(), starname.c_str(), dwLength);
                    // Encrypt data
                    if (CryptEncrypt(hKey, 0, true, 0, buffer.get(), &dwLength, dwLength + 1024))
                    {
                        encryptedString = CStringUtils::ToHexString(buffer.get(), dwLength);
                    }
                    CryptDestroyKey(hKey); // Release provider handle.
                }
            }
            CryptDestroyHash(hHash);
        }
        CryptReleaseContext(hProv, 0);
    }
    else
        DebugBreak();
    return encryptedString;
}

std::string CStringUtils::Decrypt(const std::string& s, const std::string& password)
{
    std::string decryptString;
    HCRYPTPROV  hProv = NULL;
    // Get handle to user default provider.
    if (CryptAcquireContext(&hProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT | CRYPT_SILENT))
    {
        HCRYPTHASH hHash = NULL;
        // Create hash object.
        if (CryptCreateHash(hProv, CALG_SHA_512, 0, 0, &hHash))
        {
            // Hash password string.
            DWORD dwLength = static_cast<DWORD>(password.size());
            if (CryptHashData(hHash, reinterpret_cast<BYTE*>(const_cast<char*>(password.c_str())), dwLength, 0))
            {
                HCRYPTKEY hKey = NULL;
                // Create block cipher session key based on hash of the password.
                if (CryptDeriveKey(hProv, CALG_AES_256, hHash, CRYPT_EXPORTABLE, &hKey))
                {
                    dwLength    = static_cast<DWORD>(s.size() + 1024); // 1024 bytes should be enough for padding
                    auto buffer = std::make_unique<BYTE[]>(dwLength);

                    auto strIn = std::make_unique<BYTE[]>(s.size() + 1);
                    if (buffer && strIn)
                    {
                        if (CStringUtils::FromHexString(s, strIn.get()))
                        {
                            // copy encrypted password to temporary buffer
                            memcpy(buffer.get(), strIn.get(), s.size());
                            dwLength = static_cast<DWORD>(s.size() / 2);
                            CryptDecrypt(hKey, 0, true, 0, static_cast<BYTE*>(buffer.get()), &dwLength);
                            decryptString = std::string(reinterpret_cast<char*>(buffer.get()), dwLength);
                            if (!decryptString.empty() && (decryptString[0] == '*'))
                            {
                                decryptString = decryptString.substr(1);
                            }
                            else
                                decryptString.clear();
                        }
                    }
                    CryptDestroyKey(hKey); // Release provider handle.
                }
            }
            CryptDestroyHash(hHash); // Destroy session key.
        }
        CryptReleaseContext(hProv, 0);
    }
    else
        DebugBreak();

    return decryptString;
}

#if defined(_DEBUG) && defined(_MFC_VER)
// Some test cases for these classes
[[maybe_unused]] static class StringUtilsTest
{
public:
    StringUtilsTest()
    {
        CString longline     = L"this is a test of how a string can be splitted into several lines";
        CString splittedline = CStringUtils::WordWrap(longline, 10, true, false, 4);
        ATLTRACE(L"WordWrap:\n%s\n", static_cast<LPCWSTR>(splittedline));
        splittedline = CStringUtils::LinesWrap(longline, 10, true);
        ATLTRACE(L"LinesWrap:\n%s\n", static_cast<LPCWSTR>(splittedline));
        longline     = L"c:\\this_is_a_very_long\\path_on_windows and of course some other words added to make the line longer";
        splittedline = CStringUtils::WordWrap(longline, 10, true, false, 4);
        ATLTRACE(L"WordWrap:\n%s\n", static_cast<LPCWSTR>(splittedline));
        splittedline = CStringUtils::LinesWrap(longline, 10);
        ATLTRACE(L"LinesWrap:\n%s\n", static_cast<LPCWSTR>(splittedline));
        longline     = L"Forced failure in https://myserver.com/a_long_url_to_split PROPFIND error";
        splittedline = CStringUtils::WordWrap(longline, 20, true, false, 4);
        ATLTRACE(L"WordWrap:\n%s\n", static_cast<LPCWSTR>(splittedline));
        splittedline = CStringUtils::LinesWrap(longline, 20, true);
        ATLTRACE(L"LinesWrap:\n%s\n", static_cast<LPCWSTR>(splittedline));
        longline     = L"Forced\nfailure in https://myserver.com/a_long_url_to_split PROPFIND\nerror";
        splittedline = CStringUtils::WordWrap(longline, 40, true, false, 4);
        ATLTRACE(L"WordWrap:\n%s\n", static_cast<LPCWSTR>(splittedline));
        splittedline = CStringUtils::LinesWrap(longline, 40);
        ATLTRACE(L"LinesWrap:\n%s\n", static_cast<LPCWSTR>(splittedline));
        longline     = L"Failed to add file\nc:\\export\\spare\\Devl-JBoss\\development\\head\\src\\something\\CoreApplication\\somethingelse\\src\\com\\yetsomthingelse\\shipper\\DAO\\ShipmentInfoDAO1.java\nc:\\export\\spare\\Devl-JBoss\\development\\head\\src\\something\\CoreApplication\\somethingelse\\src\\com\\yetsomthingelse\\shipper\\DAO\\ShipmentInfoDAO2.java";
        splittedline = CStringUtils::WordWrap(longline, 80, true, false, 4);
        ATLTRACE(L"WordWrap:\n%s\n", static_cast<LPCWSTR>(splittedline));
        splittedline = CStringUtils::LinesWrap(longline);
        ATLTRACE(L"LinesWrap:\n%s\n", static_cast<LPCWSTR>(splittedline));
        longline     = L"The commit comment is not properly formatted.\nFormat:\n  Field 1 : Field 2 : Field 3\nWhere:\nField 1 - Team Name|Triage|Merge|Goal\nField 2 - V1 Backlog Item ID|Triage Number|SVNBranch|Goal Name\nField 3 - Description of change\nExamples:\n\nTeam Gamma : B-12345 : Changed some code\n  Triage : 123 : Fixed production release bug\n  Merge : sprint0812 : Merged sprint0812 into prod\n  Goal : Implement Pre-Commit Hook : Commit message hook impl";
        splittedline = CStringUtils::LinesWrap(longline, 80);
        ATLTRACE(L"LinesWrap:\n%s\n", static_cast<LPCWSTR>(splittedline));
        CString widecrypt = CStringUtils::Encrypt(L"test");
        auto    wide      = CStringUtils::Decrypt(widecrypt);
        ATLASSERT(wcscmp(wide.get(), L"test") == 0);
        CStringA charcrypt = CStringUtils::Encrypt("test");
        auto     charnorm  = CStringUtils::Decrypt(charcrypt);
        ATLASSERT(strcmp(charnorm.get(), "test") == 0);

        std::string encrypted = CStringUtils::Encrypt("test", "test");
        std::string decrypted = CStringUtils::Decrypt(encrypted, "test");
        ATLASSERT(decrypted.compare("test") == 0);

        ATLASSERT(CStringUtils::ValidateFormatString(L"%1!ld! %2!s!", 0, L"test") == true);
    }
    // ReSharper disable once CppInconsistentNaming
} StringUtilsTest;

#endif
