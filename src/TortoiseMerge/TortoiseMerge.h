﻿// TortoiseMerge - a Diff/Patch program

// Copyright (C) 2006-2008, 2010-2012, 2016, 2021, 2023 - TortoiseSVN

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

#ifndef __AFXWIN_H__
#    error include 'stdafx.h' before including this file for PCH
#endif

/**
 * \ingroup TortoiseMerge
 * The application class of TortoiseMerge.
 */
class CTortoiseMergeApp : public CWinAppEx
{
public:
    CTortoiseMergeApp();

    // Overrides
public:
    BOOL InitInstance() override;
    int  ExitInstance() override;
    void OnClosingMainFrame(CFrameImpl* pFrameImpl) override;

protected:
    BOOL LoadWindowPlacement(CRect& rectNormalPosition, int& nFflags, int& nShowCmd) override;
    afx_msg void OnAppAbout();
    DECLARE_MESSAGE_MAP()
private:
    static bool HasClipboardPatch();
    static bool TrySavePatchFromClipboard(std::wstring& resultFile);
    bool        m_hasConflicts;
};

extern CTortoiseMergeApp theApp;
extern CString           g_sGroupingUuid;
