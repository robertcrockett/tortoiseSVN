﻿// TortoiseSVN - a Windows shell extension for easy version control

// Copyright (C) 2008-2014, 2017-2018, 2021-2022 - TortoiseSVN

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
#include "ConflictEditorCommand.h"
#include "SVNStatus.h"
#include "SVNInfo.h"
#include "AppUtils.h"
#include "TreeConflictEditorDlg.h"
#include "PropConflictEditorDlg.h"

bool ConflictEditorCommand::Execute()
{
    CTSVNPath merge            = cmdLinePath;
    CTSVNPath directory        = merge.GetDirectory();
    bool      bRet             = false;
    bool      bAlternativeTool = !!parser.HasKey(L"alternative");
    SVN       svn;

    // Use Subversion 1.10 API to resolve possible tree conlifcts.
    SVNConflictInfo conflict;
    if (!conflict.Get(merge))
    {
        conflict.ShowErrorDialog(GetExplorerHWND());
        return false;
    }
    // Resolve tree conflicts first.
    while (conflict.HasTreeConflict())
    {
        CProgressDlg progressDlg;
        progressDlg.SetTitle(IDS_PROC_EDIT_TREE_CONFLICTS);
        CString sProgressLine;
        sProgressLine.LoadString(IDS_PROGRS_FETCHING_TREE_CONFLICT_INFO);
        progressDlg.SetLine(1, sProgressLine);
        progressDlg.SetShowProgressBar(false);
        progressDlg.ShowModal(GetExplorerHWND(), FALSE);
        conflict.SetProgressDlg(&progressDlg);
        if (!conflict.FetchTreeDetails())
        {
            // Ignore errors while fetching additional tree conflict information.
            // User still may want to resolve it manually.
            conflict.ClearSVNError();
        }
        progressDlg.Stop();
        conflict.SetProgressDlg(nullptr);

        svn_client_conflict_option_id_t recommendedOptionId = conflict.GetRecommendedOptionId();
        bool                            bAutoResolved       = false;

        if (recommendedOptionId != svn_client_conflict_option_unspecified)
        {
            if (svn.ResolveTreeConflictById(conflict, recommendedOptionId))
            {
                bAutoResolved = true;
            }
            else
            {
                apr_status_t rootCause = svn_error_root_cause(const_cast<svn_error_t*>(svn.GetSVNError()))->apr_err;
                if (rootCause == SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE ||
                    rootCause == SVN_ERR_WC_OBSTRUCTED_UPDATE ||
                    rootCause == SVN_ERR_WC_FOUND_CONFLICT)
                {
                    svn.ClearSVNError();
                }
                else
                {
                    svn.ShowErrorDialog(GetExplorerHWND());
                    return false;
                }
            }
        }

        // If auto resolution is not available, try to resolve manually.
        if (!bAutoResolved)
        {
            CTreeConflictEditorDlg dlg;
            dlg.SetConflictInfo(&conflict);
            dlg.SetSVNContext(&svn);
            dlg.DoModal(GetExplorerHWND());
            if (dlg.IsCancelled())
                return false;

            if (dlg.GetResult() == svn_client_conflict_option_postpone)
                return false;
        }

        // Send notififcation that status may be changed. We cannot use
        // '/resolvemsghwnd' here because satus of multiple files may be changed
        // during tree conflict resolution.
        if (parser.HasVal(L"refreshmsghwnd"))
        {
            HWND refreshMsgWnd         = reinterpret_cast<HWND>(parser.GetLongLongVal(L"refreshmsghwnd"));
            UINT WM_REFRESH_STATUS_MSG = RegisterWindowMessage(L"TORTOISESVN_REFRESH_STATUS_MSG");
            ::PostMessage(refreshMsgWnd, WM_REFRESH_STATUS_MSG, 0, 0);
        }

        if (!conflict.Get(merge))
        {
            conflict.ShowErrorDialog(GetExplorerHWND());
            return false;
        }
    }

    if (conflict.HasTextConflict())
    {
        CTSVNPath theirs, mine, base;
        conflict.GetTextContentFiles(base, theirs, mine);
        if (mine.IsEmpty())
            mine = merge;

        CString filename, n1, n2, n3, n4;
        filename = merge.GetUIFileOrDirectoryName();
        n1.Format(IDS_DIFF_BASENAME, static_cast<LPCWSTR>(filename));
        n2.Format(IDS_DIFF_REMOTENAME, static_cast<LPCWSTR>(filename));
        n3.Format(IDS_DIFF_WCNAME, static_cast<LPCWSTR>(filename));
        n4.Format(IDS_DIFF_MERGEDNAME, static_cast<LPCWSTR>(filename));

        bRet = !!CAppUtils::StartExtMerge(CAppUtils::MergeFlags().AlternativeTool(bAlternativeTool),
                                          base, theirs, mine, merge, true, n1, n2, n3, n4, filename);
    }

    for (int i = 0; i < conflict.GetPropConflictCount(); ++i)
    {
        CPropConflictEditorDlg dlg;
        dlg.SetConflictInfo(&conflict);
        dlg.SetSVNContext(&svn);
        dlg.DoModal(GetExplorerHWND(), i);
        if (dlg.IsCancelled())
            return false;

        if (dlg.GetResult() == svn_client_conflict_option_postpone)
            return false;

        // Send notififcation that status may be changed. We cannot use
        // '/resolvemsghwnd' here because satus of multiple files may be changed
        // during tree conflict resolution.
        if (parser.HasVal(L"refreshmsghwnd"))
        {
            HWND refreshMsgWnd         = reinterpret_cast<HWND>(parser.GetLongLongVal(L"refreshmsghwnd"));
            UINT WM_REFRESH_STATUS_MSG = RegisterWindowMessage(L"TORTOISESVN_REFRESH_STATUS_MSG");
            ::PostMessage(refreshMsgWnd, WM_REFRESH_STATUS_MSG, 0, 0);
        }
    }

    return bRet;
}
