﻿// TortoiseSVN - a Windows shell extension for easy version control

// Copyright (C) 2016-2018, 2020-2022 - TortoiseSVN

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
#include "TreeConflictEditorDlg.h"
#include "CommonAppUtils.h"
#include "SVN.h"
#include "StringUtils.h"

CTreeConflictEditorDlg::CTreeConflictEditorDlg()
    : m_conflictInfo(nullptr)
    , m_choice(svn_client_conflict_option_undefined)
    , m_svn(nullptr)
    , m_bCancelled(false)
{
}

CTreeConflictEditorDlg::~CTreeConflictEditorDlg()
{
}

HRESULT CALLBACK CTreeConflictEditorDlg::TaskDialogCallback(HWND hWnd, UINT uNotification, WPARAM wParam, LPARAM lParam, LONG_PTR dwRefData)
{
    CTreeConflictEditorDlg *pThis = reinterpret_cast<CTreeConflictEditorDlg *>(dwRefData);
    return pThis->OnNotify(hWnd, uNotification, wParam, lParam);
}

HRESULT CTreeConflictEditorDlg::OnNotify(HWND hWnd, UINT uNotification, WPARAM wParam, LPARAM)
{
    switch (uNotification)
    {
        case TDN_DIALOG_CONSTRUCTED:
            return OnDialogConstructed(hWnd);
        case TDN_BUTTON_CLICKED:
            return OnButtonClicked(hWnd, static_cast<int>(wParam));
        case TDN_HELP:
            if (!CCommonAppUtils::StartHtmlHelp(IDD_CONFLICTRESOLVE + 0x20000))
            {
                AfxMessageBox(AFX_IDP_FAILED_TO_LAUNCH_HELP);
            }
            break;
    }
    return S_OK;
}

void CTreeConflictEditorDlg::AddCommandButton(int id, const CString &text)
{
    TASKDIALOG_BUTTON btn = {0};
    m_buttonTexts.push_back(text);
    btn.nButtonID     = id;
    btn.pszButtonText = m_buttonTexts.back().GetString();

    m_buttons.push_back(btn);
}

int CTreeConflictEditorDlg::GetButtonIDFromConflictOption(SVNConflictOption *option)
{
    int buttonID = 100 + option->GetId();
    if (option->GetPreferredMovedRelTargetIdx() >= 0)
        buttonID += (1000 * (option->GetPreferredMovedRelTargetIdx() + 1));
    if (option->GetPreferredMovedTargetIdx() >= 0)
        buttonID += (10000 * (option->GetPreferredMovedTargetIdx() + 1));
    return buttonID;
}

HRESULT CTreeConflictEditorDlg::OnDialogConstructed(HWND hWnd)
{
    CCommonAppUtils::MarkWindowAsUnpinnable(hWnd);

    HICON hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
    ::SendMessage(hWnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hIcon));
    ::SendMessage(hWnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIcon));
    return S_OK;
}

HRESULT CTreeConflictEditorDlg::OnButtonClicked(HWND hWnd, int id)
{
    for (SVNConflictOptions::const_iterator it = m_options.begin(); it != m_options.end(); ++it)
    {
        svn_client_conflict_option_id_t optionId = (*it)->GetId();
        int                             buttonID = GetButtonIDFromConflictOption(it->get());

        if (buttonID == id)
        {
            if (m_svn)
            {
                if (!m_svn->ResolveTreeConflict(*m_conflictInfo, *it->get(), it->get()->GetPreferredMovedTargetIdx(), it->get()->GetPreferredMovedRelTargetIdx()))
                {
                    m_svn->ShowErrorDialog(hWnd);
                    return S_FALSE;
                }
            }
            else
            {
                SVN svn;
                if (!svn.ResolveTreeConflict(*m_conflictInfo, *it->get(), it->get()->GetPreferredMovedTargetIdx(), it->get()->GetPreferredMovedRelTargetIdx()))
                {
                    svn.ShowErrorDialog(hWnd);
                    return S_FALSE;
                }
            }
            m_choice = optionId;
            return S_OK;
        }
    }

    return S_OK;
}

void CTreeConflictEditorDlg::DoModal(HWND parent)
{
    auto    path = m_conflictInfo->GetPath().GetFileOrDirectoryName();
    CString sDialogTitle;
    sDialogTitle.LoadString(IDS_PROC_EDIT_TREE_CONFLICTS);
    sDialogTitle = CCommonAppUtils::FormatWindowTitle(path, sDialogTitle);

    if (!m_conflictInfo->GetTreeResolutionOptions(m_options))
    {
        m_conflictInfo->ShowErrorDialog(parent);
    }

    CString sMainInstruction = m_conflictInfo->GetIncomingChangeSummary();
    CString sDetailedInfo    = m_conflictInfo->GetDetailedIncomingChangeSummary();
    CString sContent         = m_conflictInfo->GetLocalChangeSummary();

    sMainInstruction = CStringUtils::LinesWrap(sMainInstruction, 80, true, true);
    sDetailedInfo    = CStringUtils::LinesWrap(sDetailedInfo, 80, true, true);
    sContent         = CStringUtils::LinesWrap(sContent, 80, true, true);

    int button;

    for (SVNConflictOptions::const_iterator it = m_options.begin(); it != m_options.end(); ++it)
    {
        CString optLabel = (*it)->GetLabel();

        CString optDescription((*it)->GetDescription());
        optDescription.SetAt(0, towupper(optDescription[0]));
        optDescription = CStringUtils::LinesWrap(optDescription, 80, true, true);

        int buttonID = GetButtonIDFromConflictOption(it->get());
        AddCommandButton(buttonID, optLabel + L"\n" + optDescription);
    }

    svn_client_conflict_option_id_t recommendedOptionId = m_conflictInfo->GetRecommendedOptionId();
    int                             defaultButtonID     = 0;

    if (recommendedOptionId != svn_client_conflict_option_unspecified)
    {
        SVNConflictOption *recommendedOption = m_options.FindOptionById(recommendedOptionId);

        if (recommendedOption)
        {
            defaultButtonID = GetButtonIDFromConflictOption(recommendedOption);
        }
    }

    TASKDIALOGCONFIG taskConfig       = {0};
    taskConfig.cbSize                 = sizeof(taskConfig);
    taskConfig.hwndParent             = parent;
    taskConfig.dwFlags                = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW | TDF_EXPANDED_BY_DEFAULT | TDF_SIZE_TO_CONTENT;
    taskConfig.lpCallbackData         = reinterpret_cast<LONG_PTR>(this);
    taskConfig.pfCallback             = TaskDialogCallback;
    taskConfig.pszWindowTitle         = sDialogTitle;
    taskConfig.pszMainInstruction     = sMainInstruction;
    taskConfig.pszExpandedInformation = sDetailedInfo;
    taskConfig.pszContent             = sContent;
    taskConfig.pButtons               = &m_buttons.front();
    taskConfig.cButtons               = static_cast<int>(m_buttons.size());
    taskConfig.nDefaultButton         = defaultButtonID;
    taskConfig.dwCommonButtons        = TDCBF_CANCEL_BUTTON;
    TaskDialogIndirect(&taskConfig, &button, nullptr, nullptr);
    if (button == IDCANCEL)
    {
        m_bCancelled = true;
    }
}
