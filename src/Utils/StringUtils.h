﻿// TortoiseSVN - a Windows shell extension for easy version control

// Copyright (C) 2003-2010, 2013-2014, 2018, 2020-2021, 2023 - TortoiseSVN

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
#pragma once
// ReSharper disable once CppUnusedIncludeDirective
#include <atlstr.h>
#include <memory>
#include <string>

#ifdef UNICODE
// ReSharper disable once CppInconsistentNaming
#    define _tcswildcmp wcswildcmp
#else
// ReSharper disable once CppInconsistentNaming
#    define _tcswildcmp strwildcmp
#endif

/**
 * \ingroup Utils
 * Performs a wild card compare of two strings.
 * \param wild the wild card string
 * \param string the string to compare the wild card to
 * \return TRUE if the wild card matches the string, 0 otherwise
 * \par example
 * \code
 * if (strwildcmp("bl?hblah.*", "bliblah.jpeg"))
 *  printf("success\n");
 * else
 *  printf("not found\n");
 * if (strwildcmp("bl?hblah.*", "blabblah.jpeg"))
 *  printf("success\n");
 * else
 *  printf("not found\n");
 * \endcode
 * The output of the above code would be:
 * \code
 * success
 * not found
 * \endcode
 */
int strwildcmp(const char* wild, const char* string);
int wcswildcmp(const wchar_t* wild, const wchar_t* string);

template <typename Container>
void stringtok(Container& container, const std::wstring& in, bool trim,
               const wchar_t* const delimiters = L"|", bool append = true)
{
    const std::wstring::size_type len = in.length();
    std::wstring::size_type       i   = 0;
    if (!append)
        container.clear();

    while (i < len)
    {
        if (trim)
        {
            // eat leading whitespace
            i = in.find_first_not_of(delimiters, i);
            if (i == std::wstring::npos)
                return; // nothing left but white space
        }

        // find the end of the token
        std::wstring::size_type j = in.find_first_of(delimiters, i);

        // push token
        if (j == std::wstring::npos)
        {
            if constexpr (std::is_same_v<typename Container::value_type, std::wstring>)
            {
                container.push_back(in.substr(i));
            }
            else
            {
                container.push_back(static_cast<typename Container::value_type>(_wtoi64(in.substr(i).c_str())));
            }
            return;
        }
        else
        {
            if constexpr (std::is_same_v<typename Container::value_type, std::wstring>)
            {
                container.push_back(in.substr(i, j - i));
            }
            else
            {
                container.push_back(static_cast<typename Container::value_type>(_wtoi64(in.substr(i, j - i).c_str())));
            }
        }

        // set up for next loop
        i = j + 1;
    }
}

template <typename Container>
void stringtokset(Container& container, const std::wstring& in, bool trim,
                  const wchar_t* const delimiters = L"|", bool append = false)
{
    const std::wstring::size_type len = in.length();
    std::wstring::size_type       i   = 0;
    if (!append)
        container.clear();

    while (i < len)
    {
        if (trim)
        {
            // eat leading whitespace
            i = in.find_first_not_of(delimiters, i);
            if (i == std::wstring::npos)
                return; // nothing left but white space
        }

        // find the end of the token
        std::wstring::size_type j = in.find_first_of(delimiters, i);

        // push token
        if (j == std::wstring::npos)
        {
            if constexpr (std::is_same_v<typename Container::value_type, std::wstring>)
            {
                container.insert(in.substr(i));
            }
            else
            {
                container.insert(static_cast<typename Container::value_type>(_wtoi64(in.substr(i).c_str())));
            }
            return;
        }
        else
        {
            if constexpr (std::is_same_v<typename Container::value_type, std::wstring>)
            {
                container.insert(in.substr(i, j - i));
            }
            else
            {
                container.insert(static_cast<typename Container::value_type>(_wtoi64(in.substr(i, j - i).c_str())));
            }
        }

        // set up for next loop
        i = j + 1;
    }
}

// append = true as the default: a default value should never lose data!
template <typename Container>
void stringtok(Container& container, const std::string& in, bool trim,
               const char* const delimiters = "|", bool append = true)
{
    const std::string::size_type len = in.length();
    std::string::size_type       i   = 0;
    if (!append)
        container.clear();

    while (i < len)
    {
        if (trim)
        {
            // eat leading whitespace
            i = in.find_first_not_of(delimiters, i);
            if (i == std::string::npos)
                return; // nothing left but white space
        }

        // find the end of the token
        std::string::size_type j = in.find_first_of(delimiters, i);

        // push token
        if (j == std::string::npos)
        {
            if constexpr (std::is_same_v<typename Container::value_type, std::string>)
            {
                container.push_back(in.substr(i));
            }
            else
            {
                container.push_back(static_cast<typename Container::value_type>(_atoi64(in.substr(i).c_str())));
            }
            return;
        }
        else
        {
            if constexpr (std::is_same_v<typename Container::value_type, std::string>)
            {
                container.push_back(in.substr(i, j - i));
            }
            else
            {
                container.push_back(static_cast<typename Container::value_type>(_atoi64(in.substr(i, j - i).c_str())));
            }
        }

        // set up for next loop
        i = j + 1;
    }
}

template <typename Container>
void stringtokset(Container& container, const std::string& in, bool trim,
                  const char* const delimiters = "|", bool append = false)
{
    const std::string::size_type len = in.length();
    std::string::size_type       i   = 0;
    if (!append)
        container.clear();

    while (i < len)
    {
        if (trim)
        {
            // eat leading whitespace
            i = in.find_first_not_of(delimiters, i);
            if (i == std::string::npos)
                return; // nothing left but white space
        }

        // find the end of the token
        std::string::size_type j = in.find_first_of(delimiters, i);

        // push token
        if (j == std::string::npos)
        {
            if constexpr (std::is_same_v<typename Container::value_type, std::string>)
            {
                container.insert(in.substr(i));
            }
            else
            {
                container.insert(static_cast<typename Container::value_type>(_atoi64(in.substr(i).c_str())));
            }
            return;
        }
        else
        {
            if constexpr (std::is_same_v<typename Container::value_type, std::string>)
            {
                container.insert(in.substr(i, j - i));
            }
            else
            {
                container.insert(static_cast<typename Container::value_type>(_atoi64(in.substr(i, j - i).c_str())));
            }
        }

        // set up for next loop
        i = j + 1;
    }
}

/**
 * \ingroup Utils
 * string helper functions
 */
class CStringUtils
{
public:
#ifdef _MFC_VER

    /**
     * Removes all '&' chars from a string.
     */
    static void RemoveAccelerators(CString& text);

    /**
     * Writes an ASCII CString to the clipboard in CF_TEXT format
     */
    static bool WriteAsciiStringToClipboard(const CStringA& sClipdata, LCID lcid, HWND hOwningWnd = nullptr);
    /**
     * Writes a String to the clipboard in both CF_UNICODETEXT and CF_TEXT format
     */
    static bool WriteAsciiStringToClipboard(const CStringW& sClipdata, HWND hOwningWnd = nullptr);

    /**
     * Writes a fragment of HTML to the clipboard in CF_HTML format for applications that accept HTML,
     * and as a plain text CString in CF_UNICODETEXT format since most applications don't accept CF_HTML.
     * The HTML fragment must be valid Unicode HTML and will be embedded inside <HTML><BODY> blocks as explained here:
     * https://learn.microsoft.com/en-us/windows/win32/dataxchg/html-clipboard-format
     */
    static bool WriteHtmlAndStringToClipboard(const CStringW& sHtml, const CStringW& sPlainText, HWND hOwningWnd = nullptr);

    /**
    * Writes an ASCII CString to the clipboard in TSVN_UNIFIEDDIFF format, which is basically the patch file
    * as a ASCII string.
    */
    static bool WriteDiffToClipboard(const CStringA& sClipdata, HWND hOwningWnd = nullptr);

    /**
     * Reads the string \text from the file \path in utf8 encoding.
     */
    static bool ReadStringFromTextFile(const CString& path, CString& text);

#endif
#if defined(CSTRING_AVAILABLE) || defined(_MFC_VER)
    static BOOL    WildCardMatch(const CString& wildcard, const CString& string);
    static CString LinesWrap(const CString& longstring, int limit = 80, bool bCompactPaths = false, bool bForceWrap = false, int tabSize = 4);
    static CString WordWrap(const CString& longstring, int limit, bool bCompactPaths, bool bForceWrap, int tabSize);
    static std::vector<CString> WordWrap(const CString& longstring, int limit, int tabSize);
    /**
     * Find and return the number n of starting characters equal between
     * \ref lhs and \ref rhs. (max n: lhs.Left(n) == rhs.Left(n))
     */
    static int GetMatchingLength(const CString& lhs, const CString& rhs);

    /**
     * Optimizing wrapper around CompareNoCase.
     */
    static int FastCompareNoCase(const CStringW& lhs, const CStringW& rhs);

    /**
     * Writes the string \text to the file \path, either in utf16 or utf8 encoding,
     * depending on the \c bUTF8 param.
     */
    static bool WriteStringToTextFile(const std::wstring& path, const std::wstring& text, bool bUTF8 = true);
    static bool WriteStringToTextFile(const std::wstring& path, const std::string& text);

    /* Attempts to format string with variable arguments and returns true if
     * format operations succedeed with SEH or errors. */
    static bool ValidateFormatString(LPCWSTR pszFormat, ...);
#endif

    /**
     * Replace all pipe (|) character in the string with a NULL character. Used
     * for passing into Win32 functions that require such representation
     */
    static void PipesToNulls(wchar_t* buffer, size_t length);
    static void PipesToNulls(wchar_t* buffer);

    static std::unique_ptr<char[]>    Decrypt(const char* text);
    static CStringA                   Encrypt(const char* text);
    static std::unique_ptr<wchar_t[]> Decrypt(const wchar_t* text);
    static CStringW                   Encrypt(const wchar_t* text);

    static std::string Encrypt(const std::string& s, const std::string& password);
    static std::string Decrypt(const std::string& s, const std::string& password);
    static std::string ToHexString(BYTE* pSrc, int nSrcLen);
    static bool        FromHexString(const std::string& src, BYTE* pDest);
};
