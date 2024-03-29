﻿// TortoiseSVN - a Windows shell extension for easy version control

// Copyright (C) 2003-2018, 2020-2021, 2024 - TortoiseSVN

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
#include "RevisionGraphWnd.h"
#include "SVN.h"
#include "AppUtils.h"
#include "PathUtils.h"
#include "StringUtils.h"
#include "UnicodeUtils.h"
#include "TSVNPath.h"
#include "SVNInfo.h"
#include "RevisionGraphDlg.h"
#include "RepositoryInfo.h"
#include "BrowseFolder.h"
#include "SVNProgressDlg.h"
#include "ChangedDlg.h"
#include "RevisionGraph/StandardLayout.h"
#include "RevisionGraph/UpsideDownLayout.h"
#include "FormatMessageWrapper.h"
#include "DPIAware.h"
#include <strsafe.h>

#ifdef _DEBUG
// ReSharper disable once CppInconsistentNaming
#    define new DEBUG_NEW
#    undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

using namespace Gdiplus;

enum RevisionGraphContextMenuCommands
{
    // needs to start with 1, since 0 is the return value if *nothing* is clicked on in the context menu
    GROUP_MASK = 0xff00,
    ID_SHOWLOG = 1,
    ID_CFM     = 2,
    ID_BROWSEREPO,
    ID_CHECKOUT,
    ID_COMPAREREVS = 0x100,
    ID_COMPAREHEADS,
    ID_UNIDIFFREVS,
    ID_UNIDIFFHEADS,
    ID_MERGETO = 0x300,
    ID_UPDATE,
    ID_SWITCHTOHEAD,
    ID_SWITCH,
    ID_COPYURL    = 0x400,
    ID_EXPAND_ALL = 0x500,
    ID_JOIN_ALL,
    ID_GRAPH_EXPANDCOLLAPSE_ABOVE = 0x600,
    ID_GRAPH_EXPANDCOLLAPSE_RIGHT,
    ID_GRAPH_EXPANDCOLLAPSE_BELOW,
    ID_GRAPH_SPLITJOIN_ABOVE,
    ID_GRAPH_SPLITJOIN_RIGHT,
    ID_GRAPH_SPLITJOIN_BELOW,
};

CRevisionGraphWnd::CRevisionGraphWnd()
    : CWnd()
    , m_ullTicks(0)
    , m_bShowOverview(false)
    , m_parent(nullptr)
    , m_selectedEntry1(nullptr)
    , m_selectedEntry2(nullptr)
    , m_nFontSize(12)
    , m_pDlgTip(nullptr)
    , m_fZoomFactor(DEFAULT_ZOOM)
    , m_bTweakTrunkColors(true)
    , m_bTweakTagsColors(true)
    , m_bIsRubberBand(false)
    , m_ptRubberStart(0, 0)
    , m_ptRubberEnd(0, 0)
    , m_previewWidth(0)
    , m_previewHeight(0)
    , m_previewZoom(1)
    , m_hoverIndex(static_cast<index_t>(NO_INDEX))
    , m_hoverGlyphs(0)
    , m_tooltipIndex(static_cast<index_t>(NO_INDEX))
    , m_showHoverGlyphs(false)
{
    SecureZeroMemory(&m_lfBaseFont, sizeof(LOGFONT));
    std::fill_n(m_apFonts, MAXFONTS, static_cast<CFont*>(nullptr));

    WNDCLASS  wndCls;
    HINSTANCE hInst = AfxGetInstanceHandle();
#define REVGRAPH_CLASSNAME L"Revgraph_windowclass"
    if (!(::GetClassInfo(hInst, REVGRAPH_CLASSNAME, &wndCls)))
    {
        // otherwise we need to register a new class
        wndCls.style       = CS_DBLCLKS | CS_OWNDC;
        wndCls.lpfnWndProc = ::DefWindowProc;
        wndCls.cbClsExtra = wndCls.cbWndExtra = 0;
        wndCls.hInstance                      = hInst;
        wndCls.hIcon                          = nullptr;
        wndCls.hCursor                        = AfxGetApp()->LoadStandardCursor(IDC_ARROW);
        wndCls.hbrBackground                  = reinterpret_cast<HBRUSH>((COLOR_WINDOW + 1));
        wndCls.lpszMenuName                   = nullptr;
        wndCls.lpszClassName                  = REVGRAPH_CLASSNAME;

        RegisterClass(&wndCls);
    }

    m_bTweakTrunkColors = CRegDWORD(L"Software\\TortoiseSVN\\RevisionGraph\\TweakTrunkColors", TRUE) != FALSE;
    m_bTweakTagsColors  = CRegDWORD(L"Software\\TortoiseSVN\\RevisionGraph\\TweakTagsColors", TRUE) != FALSE;
    m_szTip[0]          = 0;
    m_wszTip[0]         = 0;
}

CRevisionGraphWnd::~CRevisionGraphWnd()
{
    DeleteFonts();
    delete m_pDlgTip;
}

void CRevisionGraphWnd::DeleteFonts()
{
    for (int i = 0; i < MAXFONTS; i++)
    {
        if (m_apFonts[i] != nullptr)
        {
            m_apFonts[i]->DeleteObject();
            delete m_apFonts[i];
        }
        m_apFonts[i] = nullptr;
    }
}

void CRevisionGraphWnd::DoDataExchange(CDataExchange* pDX)
{
    CWnd::DoDataExchange(pDX);
}

ULONG CRevisionGraphWnd::GetGestureStatus(CPoint /*ptTouch*/)
{
    return 0;
}

BEGIN_MESSAGE_MAP(CRevisionGraphWnd, CWnd)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_HSCROLL()
    ON_WM_VSCROLL()
    ON_WM_SIZE()
    ON_WM_LBUTTONDOWN()
    ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTW, 0, 0xFFFF, OnToolTipNotify)
    ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTA, 0, 0xFFFF, OnToolTipNotify)
    ON_WM_MOUSEWHEEL()
    ON_WM_MOUSEHWHEEL()
    ON_WM_CONTEXTMENU()
    ON_WM_MOUSEMOVE()
    ON_WM_LBUTTONUP()
    ON_WM_SETCURSOR()
    ON_WM_TIMER()
    ON_MESSAGE(WM_WORKERTHREADDONE, OnWorkerThreadDone)
    ON_WM_CAPTURECHANGED()
END_MESSAGE_MAP()

void CRevisionGraphWnd::Init(CWnd* pParent, LPRECT rect)
{
    WNDCLASS  wndCls;
    HINSTANCE hInst = AfxGetInstanceHandle();
#define REVGRAPH_CLASSNAME L"Revgraph_windowclass"
    if (!(::GetClassInfo(hInst, REVGRAPH_CLASSNAME, &wndCls)))
    {
        // otherwise we need to register a new class
        wndCls.style       = CS_DBLCLKS | CS_OWNDC;
        wndCls.lpfnWndProc = ::DefWindowProc;
        wndCls.cbClsExtra = wndCls.cbWndExtra = 0;
        wndCls.hInstance                      = hInst;
        wndCls.hIcon                          = nullptr;
        wndCls.hCursor                        = AfxGetApp()->LoadStandardCursor(IDC_ARROW);
        wndCls.hbrBackground                  = reinterpret_cast<HBRUSH>((COLOR_WINDOW + 1));
        wndCls.lpszMenuName                   = nullptr;
        wndCls.lpszClassName                  = REVGRAPH_CLASSNAME;

        RegisterClass(&wndCls);
    }

    if (!IsWindow(m_hWnd))
        CreateEx(WS_EX_CLIENTEDGE, REVGRAPH_CLASSNAME, L"RevGraph", WS_CHILD | WS_VISIBLE | WS_TABSTOP, *rect, pParent, 0);
    m_pDlgTip = new CToolTipCtrl;
    if (!m_pDlgTip->Create(this))
    {
        CTraceToOutputDebugString::Instance()(__FUNCTION__ ": Unable to add tooltip!\n");
    }
    EnableToolTips();

    SecureZeroMemory(&m_lfBaseFont, sizeof(m_lfBaseFont));
    m_lfBaseFont.lfHeight         = 0;
    m_lfBaseFont.lfWeight         = FW_NORMAL;
    m_lfBaseFont.lfItalic         = FALSE;
    m_lfBaseFont.lfCharSet        = DEFAULT_CHARSET;
    m_lfBaseFont.lfOutPrecision   = OUT_DEFAULT_PRECIS;
    m_lfBaseFont.lfClipPrecision  = CLIP_DEFAULT_PRECIS;
    m_lfBaseFont.lfQuality        = DEFAULT_QUALITY;
    m_lfBaseFont.lfPitchAndFamily = DEFAULT_PITCH;

    m_ullTicks = GetTickCount64();

    m_parent = dynamic_cast<CRevisionGraphDlg*>(pParent);
}

CPoint CRevisionGraphWnd::GetLogCoordinates(CPoint point) const
{
    // translate point into logical coordinates

    int nVScrollPos = GetScrollPos(SB_VERT);
    int nHScrollPos = GetScrollPos(SB_HORZ);

    return CPoint(static_cast<int>((point.x + nHScrollPos) / m_fZoomFactor), static_cast<int>((point.y + nVScrollPos) / m_fZoomFactor));
}

index_t CRevisionGraphWnd::GetHitNode(CPoint point, CSize border) const
{
    // any nodes at all?

    CSyncPointer<const ILayoutNodeList> nodeList(m_state.GetNodes());
    if (!nodeList)
        return static_cast<index_t>(NO_INDEX);

    // search the nodes for one at that grid position

    return nodeList->GetAt(GetLogCoordinates(point), border);
}

DWORD CRevisionGraphWnd::GetHoverGlyphs(CPoint point) const
{
    // if there is no layout, there will be no nodes,
    // hence, no glyphs

    CSyncPointer<const ILayoutNodeList> nodeList(m_state.GetNodes());
    if (!nodeList)
        return 0;

    // get node at point or node that is close enough
    // so that point may hit a glyph area

    auto    glyphSize = CDPIAware::Instance().Scale(GetSafeHwnd(), GLYPH_SIZE);
    index_t nodeIndex = GetHitNode(point);
    if (nodeIndex == NO_INDEX)
        nodeIndex = GetHitNode(point, CSize(glyphSize, glyphSize / 2));

    if (nodeIndex >= nodeList->GetCount())
        return 0;

    auto                     node = nodeList->GetNode(nodeIndex);
    const CVisibleGraphNode* base = node.node;

    // what glyphs should be shown depending on position of point
    // relative to the node rect?

    CPoint logCoordinates = GetLogCoordinates(point);
    CRect  r              = node.rect;
    CPoint center         = r.CenterPoint();

    CRect rightGlyphArea(r.right - glyphSize, center.y - glyphSize / 2, r.right + glyphSize, center.y + glyphSize / 2);
    CRect topGlyphArea(center.x - glyphSize, r.top - glyphSize / 2, center.x + glyphSize, r.top + glyphSize / 2);
    CRect bottomGlyphArea(center.x - glyphSize, r.bottom - glyphSize / 2, center.x + glyphSize, r.bottom + glyphSize / 2);

    bool upsideDown = m_state.GetOptions()->GetOption<CUpsideDownLayout>()->IsActive();

    if (upsideDown)
    {
        std::swap(topGlyphArea.top, bottomGlyphArea.top);
        std::swap(topGlyphArea.bottom, bottomGlyphArea.bottom);
    }

    DWORD result = 0;
    if (rightGlyphArea.PtInRect(logCoordinates))
        result = base->GetFirstCopyTarget() != nullptr
                     ? CGraphNodeStates::COLLAPSED_RIGHT | CGraphNodeStates::SPLIT_RIGHT
                     : 0;

    if (topGlyphArea.PtInRect(logCoordinates))
        result = base->GetSource() != nullptr
                     ? CGraphNodeStates::COLLAPSED_ABOVE | CGraphNodeStates::SPLIT_ABOVE
                     : 0;

    if (bottomGlyphArea.PtInRect(logCoordinates))
        result = base->GetNext() != nullptr
                     ? CGraphNodeStates::COLLAPSED_BELOW | CGraphNodeStates::SPLIT_BELOW
                     : 0;

    // if some nodes have already been split, don't allow collapsing etc.

    CSyncPointer<const CGraphNodeStates> nodeStates(m_state.GetNodeStates());
    if (result & nodeStates->GetFlags(base))
        result = 0;

    return result;
}

const CRevisionGraphState::SVisibleGlyph* CRevisionGraphWnd::GetHitGlyph(CPoint point) const
{
    float glyphSize = CDPIAware::Instance().Scale(GetSafeHwnd(), GLYPH_SIZE) * m_fZoomFactor;

    CSyncPointer<const CRevisionGraphState::TVisibleGlyphs>
        visibleGlyphs(m_state.GetVisibleGlyphs());

    for (size_t i = 0, count = visibleGlyphs->size(); i < count; ++i)
    {
        const CRevisionGraphState::SVisibleGlyph* entry = &(*visibleGlyphs)[i];

        float xRel = point.x - entry->leftTop.X;
        float yRel = point.y - entry->leftTop.Y;

        if ((xRel >= 0) && (xRel < glyphSize) && (yRel >= 0) && (yRel < glyphSize))
        {
            return entry;
        }
    }

    return nullptr;
}

void CRevisionGraphWnd::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
    SCROLLINFO sInfo = {0};
    sInfo.cbSize     = sizeof(SCROLLINFO);
    GetScrollInfo(SB_HORZ, &sInfo);

    // Determine the new position of scroll box.
    switch (nSBCode)
    {
        case SB_LEFT: // Scroll to far left.
            sInfo.nPos = sInfo.nMin;
            break;
        case SB_RIGHT: // Scroll to far right.
            sInfo.nPos = sInfo.nMax;
            break;
        case SB_ENDSCROLL: // End scroll.
            break;
        case SB_LINELEFT: // Scroll left.
            if (sInfo.nPos > sInfo.nMin)
                sInfo.nPos--;
            break;
        case SB_LINERIGHT: // Scroll right.
            if (sInfo.nPos < sInfo.nMax)
                sInfo.nPos++;
            break;
        case SB_PAGELEFT: // Scroll one page left.
        {
            if (sInfo.nPos > sInfo.nMin)
                sInfo.nPos = max(sInfo.nMin, sInfo.nPos - static_cast<int>(sInfo.nPage));
        }
        break;
        case SB_PAGERIGHT: // Scroll one page right.
        {
            if (sInfo.nPos < sInfo.nMax)
                sInfo.nPos = min(sInfo.nMax, sInfo.nPos + static_cast<int>(sInfo.nPage));
        }
        break;
        case SB_THUMBPOSITION:            // Scroll to absolute position. nPos is the position
            sInfo.nPos = sInfo.nTrackPos; // of the scroll box at the end of the drag operation.
            break;
        case SB_THUMBTRACK:               // Drag scroll box to specified position. nPos is the
            sInfo.nPos = sInfo.nTrackPos; // position that the scroll box has been dragged to.
            break;
    }
    SetScrollInfo(SB_HORZ, &sInfo);
    Invalidate(FALSE);
    __super::OnHScroll(nSBCode, nPos, pScrollBar);
}

void CRevisionGraphWnd::OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
    SCROLLINFO sInfo = {0};
    sInfo.cbSize     = sizeof(SCROLLINFO);
    GetScrollInfo(SB_VERT, &sInfo);

    // Determine the new position of scroll box.
    switch (nSBCode)
    {
        case SB_LEFT: // Scroll to far left.
            sInfo.nPos = sInfo.nMin;
            break;
        case SB_RIGHT: // Scroll to far right.
            sInfo.nPos = sInfo.nMax;
            break;
        case SB_ENDSCROLL: // End scroll.
            break;
        case SB_LINELEFT: // Scroll left.
            if (sInfo.nPos > sInfo.nMin)
                sInfo.nPos--;
            break;
        case SB_LINERIGHT: // Scroll right.
            if (sInfo.nPos < sInfo.nMax)
                sInfo.nPos++;
            break;
        case SB_PAGELEFT: // Scroll one page left.
        {
            if (sInfo.nPos > sInfo.nMin)
                sInfo.nPos = max(sInfo.nMin, sInfo.nPos - static_cast<int>(sInfo.nPage));
        }
        break;
        case SB_PAGERIGHT: // Scroll one page right.
        {
            if (sInfo.nPos < sInfo.nMax)
                sInfo.nPos = min(sInfo.nMax, sInfo.nPos + static_cast<int>(sInfo.nPage));
        }
        break;
        case SB_THUMBPOSITION:            // Scroll to absolute position. nPos is the position
            sInfo.nPos = sInfo.nTrackPos; // of the scroll box at the end of the drag operation.
            break;
        case SB_THUMBTRACK:               // Drag scroll box to specified position. nPos is the
            sInfo.nPos = sInfo.nTrackPos; // position that the scroll box has been dragged to.
            break;
    }
    SetScrollInfo(SB_VERT, &sInfo);
    Invalidate(FALSE);
    __super::OnVScroll(nSBCode, nPos, pScrollBar);
}

void CRevisionGraphWnd::OnSize(UINT nType, int cx, int cy)
{
    __super::OnSize(nType, cx, cy);
    SetScrollbars(GetScrollPos(SB_VERT), GetScrollPos(SB_HORZ));
    Invalidate(FALSE);
}

void CRevisionGraphWnd::OnLButtonDown(UINT nFlags, CPoint point)
{
    if (IsUpdateJobRunning())
        return __super::OnLButtonDown(nFlags, point);

    CSyncPointer<const ILayoutNodeList> nodeList(m_state.GetNodes());

    SetFocus();
    bool bHit      = false;
    bool bControl  = !!(GetKeyState(VK_CONTROL) & 0x8000);
    bool bOverview = m_bShowOverview && m_overviewRect.PtInRect(point);
    if (!bOverview)
    {
        const CRevisionGraphState::SVisibleGlyph* hitGlyph = GetHitGlyph(point);

        if (hitGlyph != nullptr)
        {
            ToggleNodeFlag(hitGlyph->node, hitGlyph->state);
            return __super::OnLButtonDown(nFlags, point);
        }
        index_t nodeIndex = GetHitNode(point);
        if (nodeIndex != NO_INDEX)
        {
            const CVisibleGraphNode* revEntry = nodeList->GetNode(nodeIndex).node;
            if (bControl)
            {
                if (m_selectedEntry1 == revEntry)
                {
                    if (m_selectedEntry2)
                    {
                        m_selectedEntry1 = m_selectedEntry2;
                        m_selectedEntry2 = nullptr;
                    }
                    else
                        m_selectedEntry1 = nullptr;
                }
                else if (m_selectedEntry2 == revEntry)
                    m_selectedEntry2 = nullptr;
                else if (m_selectedEntry1)
                    m_selectedEntry2 = revEntry;
                else
                    m_selectedEntry1 = revEntry;
            }
            else
            {
                if (m_selectedEntry1 == revEntry)
                    m_selectedEntry1 = nullptr;
                else
                    m_selectedEntry1 = revEntry;
                m_selectedEntry2 = nullptr;
            }
            bHit = true;
            Invalidate(FALSE);
        }
    }

    if ((!bHit) && (!bControl) && (!bOverview))
    {
        m_selectedEntry1 = nullptr;
        m_selectedEntry2 = nullptr;
        m_bIsRubberBand  = true;
        Invalidate(FALSE);
        if (m_bShowOverview && m_overviewRect.PtInRect(point))
            m_bIsRubberBand = false;
    }
    m_ptRubberStart = point;

    UINT uEnable = MF_BYCOMMAND;
    if ((m_selectedEntry1 != nullptr) && (m_selectedEntry2 != nullptr))
        uEnable |= MF_ENABLED;
    else
        uEnable |= MF_GRAYED;

    auto hMenu = GetParent()->GetMenu()->m_hMenu;
    EnableMenuItem(hMenu, ID_VIEW_COMPAREREVISIONS, uEnable);
    EnableMenuItem(hMenu, ID_VIEW_COMPAREHEADREVISIONS, uEnable);
    EnableMenuItem(hMenu, ID_VIEW_UNIFIEDDIFF, uEnable);
    EnableMenuItem(hMenu, ID_VIEW_UNIFIEDDIFFOFHEADREVISIONS, uEnable);

    __super::OnLButtonDown(nFlags, point);
}

void CRevisionGraphWnd::OnCaptureChanged(CWnd* pWnd)
{
    m_bIsRubberBand = false;
    m_ptRubberEnd   = CPoint(0, 0);

    __super::OnCaptureChanged(pWnd);
}

void CRevisionGraphWnd::OnLButtonUp(UINT nFlags, CPoint point)
{
    if (!m_bIsRubberBand)
        return; // we don't have a rubberband, so no zooming necessary

    m_bIsRubberBand = false;
    ReleaseCapture();
    if (IsUpdateJobRunning())
        return __super::OnLButtonUp(nFlags, point);

    // zooming is finished
    m_ptRubberEnd = CPoint(0, 0);
    CRect rect    = GetClientRect();
    int   x       = abs(m_ptRubberStart.x - point.x);
    int   y       = abs(m_ptRubberStart.y - point.y);

    if ((x < 20) && (y < 20))
    {
        // too small zoom rectangle
        // assume zooming by accident
        Invalidate(FALSE);
        __super::OnLButtonUp(nFlags, point);
        return;
    }

    float xfact = static_cast<float>(rect.Width()) / static_cast<float>(x);
    float yfact = static_cast<float>(rect.Height()) / static_cast<float>(y);
    float fact  = max(yfact, xfact);

    // find out where to scroll to
    x = min(m_ptRubberStart.x, point.x) + GetScrollPos(SB_HORZ);
    y = min(m_ptRubberStart.y, point.y) + GetScrollPos(SB_VERT);

    float fZoomFactor = m_fZoomFactor * fact;
    if (fZoomFactor > 10 * MAX_ZOOM)
    {
        // with such a big zoomfactor, the user
        // most likely zoomed by accident
        Invalidate(FALSE);
        __super::OnLButtonUp(nFlags, point);
        return;
    }
    if (fZoomFactor > MAX_ZOOM)
    {
        fZoomFactor = MAX_ZOOM;
        fact        = fZoomFactor / m_fZoomFactor;
    }

    CRevisionGraphDlg* pDlg = static_cast<CRevisionGraphDlg*>(GetParent());
    if (pDlg)
    {
        m_fZoomFactor = fZoomFactor;
        pDlg->DoZoom(m_fZoomFactor);
        SetScrollbars(static_cast<int>(static_cast<float>(y) * fact), static_cast<int>(static_cast<float>(x) * fact));
    }
    __super::OnLButtonUp(nFlags, point);
}

bool CRevisionGraphWnd::CancelMouseZoom()
{
    bool bRet = m_bIsRubberBand;
    ReleaseCapture();
    if (m_bIsRubberBand)
        Invalidate(FALSE);
    m_bIsRubberBand = false;
    m_ptRubberEnd   = CPoint(0, 0);
    return bRet;
}

INT_PTR CRevisionGraphWnd::OnToolHitTest(CPoint point, TOOLINFO* pTi) const
{
    if (IsUpdateJobRunning())
        return -1;

    index_t nodeIndex = GetHitNode(point);
    if (m_tooltipIndex != nodeIndex)
    {
        // force tooltip to be updated

        m_tooltipIndex = nodeIndex;
        return -1;
    }

    if (nodeIndex == NO_INDEX)
        return -1;

    if ((GetHoverGlyphs(point) != 0) || (GetHitGlyph(point) != nullptr))
        return -1;

    pTi->hwnd = this->m_hWnd;
    CWnd::GetClientRect(&pTi->rect);
    pTi->uFlags |= TTF_ALWAYSTIP | TTF_IDISHWND;
    pTi->uId      = reinterpret_cast<UINT_PTR>(m_hWnd);
    pTi->lpszText = LPSTR_TEXTCALLBACK;

    return 1;
}

BOOL CRevisionGraphWnd::OnToolTipNotify(UINT /*id*/, NMHDR* pNMHDR, LRESULT* pResult)
{
    if (pNMHDR->idFrom != reinterpret_cast<UINT_PTR>(m_hWnd))
        return FALSE;

    POINT point;
    DWORD ptW = GetMessagePos();
    point.x   = GET_X_LPARAM(ptW);
    point.y   = GET_Y_LPARAM(ptW);
    ScreenToClient(&point);

    CString strTipText = TooltipText(GetHitNode(point));

    *pResult = 0;
    if (strTipText.IsEmpty())
        return TRUE;

    CSize tooltipSize = UsableTooltipRect();
    strTipText        = DisplayableText(strTipText, tooltipSize);

    // need to handle both ANSI and UNICODE versions of the message
    if (pNMHDR->code == TTN_NEEDTEXTA)
    {
        TOOLTIPTEXTA* pTTTA = reinterpret_cast<NMTTDISPINFOA*>(pNMHDR);
        ::SendMessage(pNMHDR->hwndFrom, TTM_SETMAXTIPWIDTH, 0, tooltipSize.cx);
        pTTTA->lpszText = m_szTip;
        WideCharToMultiByte(CP_ACP, 0, strTipText, -1, m_szTip, strTipText.GetLength() + 1, nullptr, nullptr);
    }
    else
    {
        TOOLTIPTEXTW* pTTTW = reinterpret_cast<NMTTDISPINFOW*>(pNMHDR);
        ::SendMessage(pNMHDR->hwndFrom, TTM_SETMAXTIPWIDTH, 0, tooltipSize.cx);
        StringCchCopy(m_wszTip, _countof(m_wszTip), strTipText);
        pTTTW->lpszText = m_wszTip;
    }

    // show the tooltip for 32 seconds. A higher value than 32767 won't work
    // even though it's nowhere documented!
    ::SendMessage(pNMHDR->hwndFrom, TTM_SETDELAYTIME, TTDT_AUTOPOP, 32767);
    return TRUE; // message was handled
}

CSize CRevisionGraphWnd::UsableTooltipRect() const
{
    // get screen size

    int screenWidth  = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    // get current mouse position

    CPoint cursorPos;
    if (GetCursorPos(&cursorPos) == FALSE)
    {
        // we could not determine the mouse position
        // use screen / 2 minus some safety margin

        return CSize(screenWidth / 2 - 20, screenHeight / 2 - 20);
    }

    // tool tip will display in the biggest sector beside the cursor
    // deduct some safety margin (for the mouse cursor itself

    CSize biggestSector(max(screenWidth - cursorPos.x - 40, cursorPos.x - 24), max(screenHeight - cursorPos.y - 40, cursorPos.y - 24));

    return biggestSector;
}

CString CRevisionGraphWnd::DisplayableText(const CString& wholeText, const CSize& tooltipSize)
{
    CDC* dc = GetDC();
    if (dc == nullptr)
    {
        // no access to the device context -> truncate hard at 1000 chars

        return wholeText.GetLength() >= MAX_TT_LENGTH_DEFAULT
                   ? wholeText.Left(MAX_TT_LENGTH_DEFAULT - 4) + L" ..."
                   : wholeText;
    }

    // select the tooltip font

    NONCLIENTMETRICS metrics;
    metrics.cbSize = sizeof(metrics);
    SystemParametersInfo(SPI_GETNONCLIENTMETRICS, metrics.cbSize, &metrics, 0);

    CFont font;
    font.CreateFontIndirect(&metrics.lfStatusFont);
    CFont* pOldFont = dc->SelectObject(&font);

    // split into lines and fill the tooltip rect

    CString result;

    int remainingHeight = tooltipSize.cy;
    int pos             = 0;
    while (pos < wholeText.GetLength())
    {
        // extract a whole line

        int nextPos = wholeText.Find('\n', pos);
        if (nextPos < 0)
            nextPos = wholeText.GetLength();

        CString line = wholeText.Mid(pos, nextPos - pos + 1);

        // find a way to make it fit

        CSize size = dc->GetTextExtent(line);
        while (size.cx > tooltipSize.cx)
        {
            line.Delete(line.GetLength() - 1);
            int nextPos2 = line.ReverseFind(' ');
            if (nextPos2 < 0)
                break;

            line.Delete(nextPos2 + 1, line.GetLength() - pos - 1);
            size = dc->GetTextExtent(line);
        }

        // enough room for the new line?

        remainingHeight -= size.cy;
        if (remainingHeight <= size.cy)
        {
            result += L"...";
            break;
        }

        // add the line

        result += line;
        pos += line.GetLength();
    }

    // relase temp. resources

    dc->SelectObject(pOldFont);
    ReleaseDC(dc);

    // ready

    return result;
}

CString CRevisionGraphWnd::TooltipText(index_t index) const
{
    if (index != NO_INDEX)
    {
        CSyncPointer<const ILayoutNodeList> nodeList(m_state.GetNodes());
        return nodeList->GetToolTip(index);
    }

    return CString();
}

void CRevisionGraphWnd::SaveGraphAs(CString sSavePath)
{
    CString extension = CPathUtils::GetFileExtFromPath(sSavePath);
    if (extension.CompareNoCase(L".wmf") == 0)
    {
        // save the graph as an enhanced metafile
        CMetaFileDC wmfDC;
        wmfDC.CreateEnhanced(nullptr, sSavePath, nullptr, L"TortoiseSVN\0Revision Graph\0\0");
        float fZoom   = m_fZoomFactor;
        m_fZoomFactor = DEFAULT_ZOOM;
        DoZoom(m_fZoomFactor);
        CRect rect;
        rect = GetViewRect();
        GraphicsDevice dev;
        dev.pDC = &wmfDC;
        DrawGraph(dev, rect, 0, 0, true);
        HENHMETAFILE hEmf = wmfDC.CloseEnhanced();
        DeleteEnhMetaFile(hEmf);
        m_fZoomFactor = fZoom;
        DoZoom(m_fZoomFactor);
    }
    else if (extension.CompareNoCase(L".svg") == 0)
    {
        // save the graph as a scalable vector graphic
        SVG   svg;
        float fZoom   = m_fZoomFactor;
        m_fZoomFactor = DEFAULT_ZOOM;
        DoZoom(m_fZoomFactor);
        CRect rect;
        rect = GetViewRect();
        svg.SetViewSize(rect.Width(), rect.Height());
        GraphicsDevice dev;
        dev.pSvg = &svg;
        DrawGraph(dev, rect, 0, 0, true);
        svg.Save(sSavePath);
        m_fZoomFactor = fZoom;
        DoZoom(m_fZoomFactor);
    }

    else
    {
        // save the graph as a pixel picture instead of a vector picture
        // create dc to paint on
        try
        {
            CString   sErrorMessage;
            CWindowDC ddc(this);
            CDC       dc;
            if (!dc.CreateCompatibleDC(&ddc))
            {
                CFormatMessageWrapper errorDetails;
                if (errorDetails)
                    MessageBox(errorDetails, L"Error", MB_OK | MB_ICONINFORMATION);

                return;
            }
            CRect rect;
            rect           = GetGraphRect();
            rect.bottom    = static_cast<LONG>(static_cast<float>(rect.Height()) * m_fZoomFactor);
            rect.right     = static_cast<LONG>(static_cast<float>(rect.Width()) * m_fZoomFactor);
            BITMAPINFO bmi = {0};
            HBITMAP    hbm;
            LPBYTE     pBits;
            // Fill out the fields you care about.
            bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth       = rect.Width();
            bmi.bmiHeader.biHeight      = rect.Height();
            bmi.bmiHeader.biPlanes      = 1;
            bmi.bmiHeader.biBitCount    = 24;
            bmi.bmiHeader.biCompression = BI_RGB;

            // Create the surface.
            hbm = CreateDIBSection(ddc.m_hDC, &bmi, DIB_RGB_COLORS, reinterpret_cast<void**>(&pBits), nullptr, 0);
            if (hbm == nullptr)
            {
                TaskDialog(GetSafeHwnd(), AfxGetResourceHandle(), MAKEINTRESOURCE(IDS_APPNAME), MAKEINTRESOURCE(IDS_ERR_ERROROCCURED), MAKEINTRESOURCE(IDS_REVGRAPH_ERR_NOMEMORY), TDCBF_OK_BUTTON, TD_ERROR_ICON, nullptr);
                return;
            }
            HBITMAP oldBm = static_cast<HBITMAP>(dc.SelectObject(hbm));
            // paint the whole graph
            GraphicsDevice dev;
            dev.pDC = &dc;
            DrawGraph(dev, rect, 0, 0, true);
            // now use GDI+ to save the picture
            CLSID encoderClsid;
            {
                Bitmap bitmap(hbm, nullptr);
                if (bitmap.GetLastStatus() == Ok)
                {
                    // Get the CLSID of the encoder.
                    int ret = 0;
                    if (CPathUtils::GetFileExtFromPath(sSavePath).CompareNoCase(L".png") == 0)
                        ret = GetEncoderClsid(L"image/png", &encoderClsid);
                    else if (CPathUtils::GetFileExtFromPath(sSavePath).CompareNoCase(L".jpg") == 0)
                        ret = GetEncoderClsid(L"image/jpeg", &encoderClsid);
                    else if (CPathUtils::GetFileExtFromPath(sSavePath).CompareNoCase(L".jpeg") == 0)
                        ret = GetEncoderClsid(L"image/jpeg", &encoderClsid);
                    else if (CPathUtils::GetFileExtFromPath(sSavePath).CompareNoCase(L".bmp") == 0)
                        ret = GetEncoderClsid(L"image/bmp", &encoderClsid);
                    else if (CPathUtils::GetFileExtFromPath(sSavePath).CompareNoCase(L".gif") == 0)
                        ret = GetEncoderClsid(L"image/gif", &encoderClsid);
                    else
                    {
                        sSavePath += L".jpg";
                        ret = GetEncoderClsid(L"image/jpeg", &encoderClsid);
                    }
                    if (ret >= 0)
                    {
                        CStringW tFile = CStringW(sSavePath);
                        bitmap.Save(tFile, &encoderClsid, nullptr);
                    }
                    else
                    {
                        sErrorMessage.Format(IDS_REVGRAPH_ERR_NOENCODER, static_cast<LPCWSTR>(CPathUtils::GetFileExtFromPath(sSavePath)));
                    }
                }
                else
                {
                    sErrorMessage.LoadString(IDS_REVGRAPH_ERR_NOBITMAP);
                }
            }
            dc.SelectObject(oldBm);
            DeleteObject(hbm);
            dc.DeleteDC();
            if (!sErrorMessage.IsEmpty())
            {
                ::MessageBox(m_hWnd, sErrorMessage, L"TortoiseSVN", MB_ICONERROR);
            }
        }
        catch (CException* pE)
        {
            wchar_t szErrorMsg[2048] = {0};
            pE->GetErrorMessage(szErrorMsg, 2048);
            pE->Delete();
            ::MessageBox(m_hWnd, szErrorMsg, L"TortoiseSVN", MB_ICONERROR);
        }
    }
}

BOOL CRevisionGraphWnd::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
    if (IsUpdateJobRunning())
        return __super::OnMouseWheel(nFlags, zDelta, pt);

    if (GetKeyState(VK_CONTROL) & 0x8000)
    {
        float newZoom = m_fZoomFactor * (zDelta < 0 ? ZOOM_STEP : 1.0f / ZOOM_STEP);
        DoZoom(max(MIN_ZOOM, min(MAX_ZOOM, newZoom)));
    }
    else
    {
        int orientation = (GetKeyState(VK_SHIFT) & 0x8000) ? SB_HORZ : SB_VERT;
        int pos         = GetScrollPos(orientation);
        pos -= (zDelta);
        SetScrollPos(orientation, pos);
        Invalidate(FALSE);
    }
    return __super::OnMouseWheel(nFlags, zDelta, pt);
}

void CRevisionGraphWnd::OnMouseHWheel(UINT nFlags, short zDelta, CPoint pt)
{
    if (IsUpdateJobRunning())
        return __super::OnMouseHWheel(nFlags, zDelta, pt);

    int orientation = (GetKeyState(VK_SHIFT) & 0x8000) ? SB_VERT : SB_HORZ;
    int pos         = GetScrollPos(orientation);
    pos -= (zDelta);
    SetScrollPos(orientation, pos);
    Invalidate(FALSE);

    return __super::OnMouseHWheel(nFlags, zDelta, pt);
}

bool CRevisionGraphWnd::UpdateSelectedEntry(const CVisibleGraphNode* clickedentry)
{
    if ((m_selectedEntry1 == nullptr) && (clickedentry == nullptr))
        return false;

    if (m_selectedEntry1 == nullptr)
    {
        m_selectedEntry1 = clickedentry;
        Invalidate(FALSE);
    }
    if ((m_selectedEntry2 == nullptr) && (clickedentry != m_selectedEntry1))
    {
        m_selectedEntry1 = clickedentry;
        Invalidate(FALSE);
    }
    if (m_selectedEntry1 && m_selectedEntry2)
    {
        if ((m_selectedEntry2 != clickedentry) && (m_selectedEntry1 != clickedentry))
            return false;
    }
    if (m_selectedEntry1 == nullptr)
        return false;

    return true;
}

void CRevisionGraphWnd::AppendMenu(CMenu& popup, UINT title, UINT command, UINT flags)
{
    // separate different groups / section within the context menu

    if (popup.GetMenuItemCount() > 0)
    {
        UINT lastCommand = popup.GetMenuItemID(popup.GetMenuItemCount() - 1);
        if ((lastCommand & GROUP_MASK) != (command & GROUP_MASK))
            popup.AppendMenu(MF_SEPARATOR, NULL);
    }

    // actually add the new item

    CString titleString;
    titleString.LoadString(title);
    popup.AppendMenu(MF_STRING | flags, command, titleString);
}

void CRevisionGraphWnd::AddSVNOps(CMenu& popup) const
{
    bool bothPresent = (m_selectedEntry1 != nullptr) &&
                       !m_selectedEntry1->GetClassification().Is(CNodeClassification::IS_DELETED) &&
                       (m_selectedEntry2 != nullptr) &&
                       !m_selectedEntry2->GetClassification().Is(CNodeClassification::IS_DELETED);

    bool bSameURL = (m_selectedEntry2 && m_selectedEntry1 && (m_selectedEntry1->GetPath() == m_selectedEntry2->GetPath()));

    if (m_selectedEntry1 && (m_selectedEntry2 == nullptr))
    {
        AppendMenu(popup, IDS_REPOBROWSE_SHOWLOG, ID_SHOWLOG);
        if (!m_selectedEntry1->GetClassification().Is(CNodeClassification::IS_MODIFIED_WC))
            AppendMenu(popup, IDS_LOG_BROWSEREPO, ID_BROWSEREPO);
        if (PathIsDirectory(m_sPath))
        {
            if (m_selectedEntry1->GetClassification().Is(CNodeClassification::IS_MODIFIED_WC))
                AppendMenu(popup, IDS_REVGRAPH_POPUP_CFM, ID_CFM);
            else
                AppendMenu(popup, IDS_LOG_POPUP_MERGEREV, ID_MERGETO);
            if (!m_selectedEntry1->GetClassification().Is(CNodeClassification::IS_DELETED))
                AppendMenu(popup, IDS_MENUCHECKOUT, ID_CHECKOUT);
        }

        if (!CTSVNPath(m_sPath).IsUrl())
        {
            if (GetWCURL() == GetSelectedURL())
            {
                AppendMenu(popup, IDS_REVGRAPH_POPUP_UPDATE, ID_UPDATE);
            }
            else
            {
                AppendMenu(popup, IDS_REVGRAPH_POPUP_SWITCHTOHEAD, ID_SWITCHTOHEAD);
                AppendMenu(popup, IDS_REVGRAPH_POPUP_SWITCH, ID_SWITCH);
            }
        }
        AppendMenu(popup, IDS_REPOBROWSE_URLTOCLIPBOARD, ID_COPYURL);
    }

    if (bothPresent)
    {
        if (!m_selectedEntry2->GetClassification().Is(CNodeClassification::IS_MODIFIED_WC))
        {
            // TODO: TSVN currently can't compare URL -> WC, but only vice versa)

            AppendMenu(popup, IDS_REVGRAPH_POPUP_COMPAREREVS, ID_COMPAREREVS);
            if (!bSameURL)
                AppendMenu(popup, IDS_REVGRAPH_POPUP_COMPAREHEADS, ID_COMPAREHEADS);
        }

        AppendMenu(popup, IDS_REVGRAPH_POPUP_UNIDIFFREVS, ID_UNIDIFFREVS);
        if (!bSameURL)
            AppendMenu(popup, IDS_REVGRAPH_POPUP_UNIDIFFHEADS, ID_UNIDIFFHEADS);
    }
}

void CRevisionGraphWnd::AddGraphOps(CMenu& popup, const CVisibleGraphNode* node)
{
    CSyncPointer<CGraphNodeStates> nodeStates(m_state.GetNodeStates());

    if (node == nullptr)
    {
        DWORD state = nodeStates->GetCombinedFlags();
        if (state != 0)
        {
            if (state & CGraphNodeStates::COLLAPSED_ALL)
                AppendMenu(popup, IDS_REVGRAPH_POPUP_EXPAND_ALL, ID_EXPAND_ALL);

            if (state & CGraphNodeStates::SPLIT_ALL)
                AppendMenu(popup, IDS_REVGRAPH_POPUP_JOIN_ALL, ID_JOIN_ALL);
        }
    }
    else
    {
        DWORD state = nodeStates->GetFlags(node);

        if (node->GetSource() || (state & CGraphNodeStates::COLLAPSED_ABOVE))
            AppendMenu(popup, (state & CGraphNodeStates::COLLAPSED_ABOVE) ? IDS_REVGRAPH_POPUP_EXPAND_ABOVE : IDS_REVGRAPH_POPUP_COLLAPSE_ABOVE, ID_GRAPH_EXPANDCOLLAPSE_ABOVE);

        if (node->GetFirstCopyTarget() || (state & CGraphNodeStates::COLLAPSED_RIGHT))
            AppendMenu(popup, (state & CGraphNodeStates::COLLAPSED_RIGHT) ? IDS_REVGRAPH_POPUP_EXPAND_RIGHT : IDS_REVGRAPH_POPUP_COLLAPSE_RIGHT, ID_GRAPH_EXPANDCOLLAPSE_RIGHT);

        if (node->GetNext() || (state & CGraphNodeStates::COLLAPSED_BELOW))
            AppendMenu(popup, (state & CGraphNodeStates::COLLAPSED_BELOW) ? IDS_REVGRAPH_POPUP_EXPAND_BELOW : IDS_REVGRAPH_POPUP_COLLAPSE_BELOW, ID_GRAPH_EXPANDCOLLAPSE_BELOW);

        if (node->GetSource() || (state & CGraphNodeStates::SPLIT_ABOVE))
            AppendMenu(popup, (state & CGraphNodeStates::SPLIT_ABOVE) ? IDS_REVGRAPH_POPUP_JOIN_ABOVE : IDS_REVGRAPH_POPUP_SPLIT_ABOVE, ID_GRAPH_SPLITJOIN_ABOVE);

        if (node->GetFirstCopyTarget() || (state & CGraphNodeStates::SPLIT_RIGHT))
            AppendMenu(popup, (state & CGraphNodeStates::SPLIT_RIGHT) ? IDS_REVGRAPH_POPUP_JOIN_RIGHT : IDS_REVGRAPH_POPUP_SPLIT_RIGHT, ID_GRAPH_SPLITJOIN_RIGHT);

        if (node->GetNext() || (state & CGraphNodeStates::SPLIT_BELOW))
            AppendMenu(popup, (state & CGraphNodeStates::SPLIT_BELOW) ? IDS_REVGRAPH_POPUP_JOIN_BELOW : IDS_REVGRAPH_POPUP_SPLIT_BELOW, ID_GRAPH_SPLITJOIN_BELOW);
    }
}

CString CRevisionGraphWnd::GetSelectedURL() const
{
    if (m_selectedEntry1 == nullptr)
        return CString();

    CString URL = m_state.GetRepositoryRoot() + CUnicodeUtils::GetUnicode(m_selectedEntry1->GetPath().GetPath().c_str());
    URL         = CUnicodeUtils::GetUnicode(CPathUtils::PathEscape(CUnicodeUtils::GetUTF8(URL)));

    return URL;
}

CString CRevisionGraphWnd::GetWCURL() const
{
    CTSVNPath path(m_sPath);
    if (path.IsUrl())
        return CString();

    SVNInfo            info;
    const SVNInfoData* status = info.GetFirstFileInfo(path, SVNRev(), SVNRev());

    return status == nullptr ? CString() : status->url;
}

void CRevisionGraphWnd::DoShowLog() const
{
    CString URL = GetSelectedURL();

    CString sCmd;
    sCmd.Format(L"/command:log /path:\"%s\" /startrev:%ld /pegrev:%ld",
                static_cast<LPCWSTR>(URL),
                m_selectedEntry1->GetRevision(), m_selectedEntry1->GetRevision());

    if (!SVN::PathIsURL(CTSVNPath(m_sPath)))
    {
        sCmd += L" /propspath:\"";
        sCmd += m_sPath;
        sCmd += L"\"";
    }

    CAppUtils::RunTortoiseProc(sCmd);
}

void CRevisionGraphWnd::DoCheckout() const
{
    CString URL = GetSelectedURL();

    CString sCmd;
    sCmd.Format(L"/command:checkout /url:\"%s\" /revision:%ld",
                static_cast<LPCWSTR>(URL),
                m_selectedEntry1->GetRevision());

    CAppUtils::RunTortoiseProc(sCmd);
}

void CRevisionGraphWnd::DoCheckForModification() const
{
    CChangedDlg dlg;
    dlg.m_pathList = CTSVNPathList(CTSVNPath(m_sPath));
    dlg.DoModal();
}

void CRevisionGraphWnd::DoMergeTo() const
{
    CString       URL  = GetSelectedURL();
    CString       path = m_sPath;
    CBrowseFolder folderBrowser;
    folderBrowser.SetInfo(CString(MAKEINTRESOURCE(IDS_LOG_MERGETO)));
    if (folderBrowser.Show(GetSafeHwnd(), path, path) == CBrowseFolder::OK)
    {
        CSVNProgressDlg dlg;
        dlg.SetCommand(CSVNProgressDlg::SVNProgress_Merge);
        dlg.SetPathList(CTSVNPathList(CTSVNPath(path)));
        dlg.SetUrl(URL);
        dlg.SetSecondUrl(URL);
        SVNRevRangeArray revArray;
        revArray.AddRevRange(m_selectedEntry1->GetRevision() - 1, static_cast<svn_revnum_t>(m_selectedEntry1->GetRevision()));
        dlg.SetRevisionRanges(revArray);
        dlg.DoModal();
    }
}

void CRevisionGraphWnd::DoUpdate() const
{
    CRegDWORD       updateExternals(L"Software\\TortoiseSVN\\IncludeExternals", true);
    int             options = static_cast<DWORD>(updateExternals) ? 0 : ProgOptIgnoreExternals;
    CSVNProgressDlg progDlg;
    progDlg.SetCommand(CSVNProgressDlg::SVNProgress_Update);
    progDlg.SetOptions(options);
    progDlg.SetPathList(CTSVNPathList(CTSVNPath(m_sPath)));
    progDlg.SetRevision(m_selectedEntry1->GetRevision());
    progDlg.SetDepth();
    progDlg.DoModal();

    if (m_state.GetFetchedWCState())
        m_parent->UpdateFullHistory();
}

void CRevisionGraphWnd::DoSwitch() const
{
    CSVNProgressDlg progDlg;
    progDlg.SetCommand(CSVNProgressDlg::SVNProgress_Switch);
    progDlg.SetPathList(CTSVNPathList(CTSVNPath(m_sPath)));
    progDlg.SetUrl(GetSelectedURL());
    progDlg.SetRevision(m_selectedEntry1->GetRevision());
    progDlg.DoModal();

    if (m_state.GetFetchedWCState())
        m_parent->UpdateFullHistory();
}

void CRevisionGraphWnd::DoSwitchToHead() const
{
    CSVNProgressDlg progDlg;
    progDlg.SetCommand(CSVNProgressDlg::SVNProgress_Switch);
    progDlg.SetPathList(CTSVNPathList(CTSVNPath(m_sPath)));
    progDlg.SetUrl(GetSelectedURL());
    progDlg.SetRevision(SVNRev::REV_HEAD);
    progDlg.SetPegRevision(m_selectedEntry1->GetRevision());
    progDlg.DoModal();

    if (m_state.GetFetchedWCState())
        m_parent->UpdateFullHistory();
}

void CRevisionGraphWnd::DoBrowseRepo() const
{
    CString sCmd;
    sCmd.Format(L"/command:repobrowser /path:\"%s\" /rev:%d",
                static_cast<LPCWSTR>(GetSelectedURL()), m_selectedEntry1->GetRevision());

    CAppUtils::RunTortoiseProc(sCmd);
}

void CRevisionGraphWnd::ResetNodeFlags(DWORD flags)
{
    m_state.GetNodeStates()->ResetFlags(flags);
    m_parent->StartWorkerThread();
}

void CRevisionGraphWnd::ToggleNodeFlag(const CVisibleGraphNode* node, DWORD flag)
{
    CSyncPointer<CGraphNodeStates> nodeStates(m_state.GetNodeStates());

    if (nodeStates->GetFlags(node) & flag)
        nodeStates->ResetFlags(node, flag);
    else
        nodeStates->SetFlags(node, flag);

    m_parent->StartWorkerThread();
}

void CRevisionGraphWnd::DoCopyUrl() const
{
    CStringUtils::WriteAsciiStringToClipboard(GetSelectedURL(), m_hWnd);
}

void CRevisionGraphWnd::OnContextMenu(CWnd* /*pWnd*/, CPoint point)
{
    if (IsUpdateJobRunning())
        return;

    CSyncPointer<const ILayoutNodeList> nodeList(m_state.GetNodes());

    CPoint clientPoint = point;
    this->ScreenToClient(&clientPoint);

    index_t                  nodeIndex    = GetHitNode(clientPoint);
    const CVisibleGraphNode* clickedentry = nullptr;
    if (nodeIndex != NO_INDEX)
    {
        clickedentry = nodeList->GetNode(nodeIndex).node;
    }

    if (!UpdateSelectedEntry(clickedentry) && !m_state.GetNodeStates()->GetCombinedFlags())
    {
        return;
    }

    CMenu popup;
    if (!popup.CreatePopupMenu())
        return;

    AddSVNOps(popup);
    AddGraphOps(popup, clickedentry);

    // if the context menu is invoked through the keyboard, we have to use
    // a calculated position on where to anchor the menu on
    if ((point.x == -1) && (point.y == -1))
    {
        CRect rect = GetWindowRect();
        point      = rect.CenterPoint();
    }

    int cmd = popup.TrackPopupMenu(TPM_RETURNCMD | TPM_LEFTALIGN | TPM_NONOTIFY | TPM_RIGHTBUTTON, point.x, point.y, this, nullptr);
    switch (cmd)
    {
        case ID_COMPAREREVS:
            if (m_selectedEntry1 != nullptr)
                CompareRevs(false);
            break;
        case ID_COMPAREHEADS:
            if (m_selectedEntry1 != nullptr)
                CompareRevs(true);
            break;
        case ID_UNIDIFFREVS:
            if (m_selectedEntry1 != nullptr)
                UnifiedDiffRevs(false);
            break;
        case ID_UNIDIFFHEADS:
            if (m_selectedEntry1 != nullptr)
                UnifiedDiffRevs(true);
            break;
        case ID_SHOWLOG:
            DoShowLog();
            break;
        case ID_CHECKOUT:
            DoCheckout();
            break;
        case ID_CFM:
            DoCheckForModification();
            break;
        case ID_MERGETO:
            DoMergeTo();
            break;
        case ID_UPDATE:
            DoUpdate();
            break;
        case ID_SWITCHTOHEAD:
            DoSwitchToHead();
            break;
        case ID_SWITCH:
            DoSwitch();
            break;
        case ID_COPYURL:
            DoCopyUrl();
            break;
        case ID_BROWSEREPO:
            DoBrowseRepo();
            break;
        case ID_EXPAND_ALL:
            ResetNodeFlags(CGraphNodeStates::COLLAPSED_ALL);
            break;
        case ID_JOIN_ALL:
            ResetNodeFlags(CGraphNodeStates::SPLIT_ALL);
            break;
        case ID_GRAPH_EXPANDCOLLAPSE_ABOVE:
            ToggleNodeFlag(clickedentry, CGraphNodeStates::COLLAPSED_ABOVE);
            break;
        case ID_GRAPH_EXPANDCOLLAPSE_RIGHT:
            ToggleNodeFlag(clickedentry, CGraphNodeStates::COLLAPSED_RIGHT);
            break;
        case ID_GRAPH_EXPANDCOLLAPSE_BELOW:
            ToggleNodeFlag(clickedentry, CGraphNodeStates::COLLAPSED_BELOW);
            break;
        case ID_GRAPH_SPLITJOIN_ABOVE:
            ToggleNodeFlag(clickedentry, CGraphNodeStates::SPLIT_ABOVE);
            break;
        case ID_GRAPH_SPLITJOIN_RIGHT:
            ToggleNodeFlag(clickedentry, CGraphNodeStates::SPLIT_RIGHT);
            break;
        case ID_GRAPH_SPLITJOIN_BELOW:
            ToggleNodeFlag(clickedentry, CGraphNodeStates::SPLIT_BELOW);
            break;
    }
}

void CRevisionGraphWnd::OnMouseMove(UINT nFlags, CPoint point)
{
    if (IsUpdateJobRunning())
    {
        return __super::OnMouseMove(nFlags, point);
    }
    if (!m_bIsRubberBand)
    {
        if (m_bShowOverview && (m_overviewRect.PtInRect(point)) && (nFlags & MK_LBUTTON))
        {
            // scrolling
            int x = static_cast<int>((point.x - m_overviewRect.left - (m_overviewPosRect.Width() / 2)) / m_previewZoom * m_fZoomFactor);
            int y = static_cast<int>((point.y - (m_overviewPosRect.Height() / 2)) / m_previewZoom * m_fZoomFactor);
            x     = max(0, x);
            y     = max(0, y);
            SetScrollbars(y, x);
            Invalidate(FALSE);
            return __super::OnMouseMove(nFlags, point);
        }
        else
        {
            // update screen if we hover over a different
            // node than during the last redraw

            CPoint clientPoint = point;
            GetCursorPos(&clientPoint);
            ScreenToClient(&clientPoint);

            const CRevisionGraphState::SVisibleGlyph* hitGlyph  = GetHitGlyph(clientPoint);
            const CFullGraphNode*                     glyphNode = hitGlyph ? hitGlyph->node->GetBase() : nullptr;

            const CFullGraphNode* hoverNode = nullptr;
            if (m_hoverIndex != NO_INDEX)
            {
                CSyncPointer<const ILayoutNodeList> nodeList(m_state.GetNodes());
                if (m_hoverIndex < nodeList->GetCount())
                    hoverNode = nodeList->GetNode(m_hoverIndex).node->GetBase();
            }

            bool onHoverNodeGlyph = (hoverNode != nullptr) && (glyphNode == hoverNode);
            if (!onHoverNodeGlyph && ((m_hoverIndex != GetHitNode(clientPoint)) || (m_hoverGlyphs != GetHoverGlyphs(clientPoint))))
            {
                m_showHoverGlyphs = false;

                KillTimer(GLYPH_HOVER_EVENT);
                SetTimer(GLYPH_HOVER_EVENT, GLYPH_HOVER_DELAY, nullptr);

                Invalidate(FALSE);
            }

            return __super::OnMouseMove(nFlags, point);
        }
    }

    if ((abs(m_ptRubberStart.x - point.x) < 2) && (abs(m_ptRubberStart.y - point.y) < 2))
    {
        return __super::OnMouseMove(nFlags, point);
    }

    SetCapture();

    if ((m_ptRubberEnd.x != 0) || (m_ptRubberEnd.y != 0))
        DrawRubberBand();
    m_ptRubberEnd   = point;
    CRect rect      = GetClientRect();
    m_ptRubberEnd.x = max(m_ptRubberEnd.x, rect.left);
    m_ptRubberEnd.x = min(m_ptRubberEnd.x, rect.right);
    m_ptRubberEnd.y = max(m_ptRubberEnd.y, rect.top);
    m_ptRubberEnd.y = min(m_ptRubberEnd.y, rect.bottom);
    DrawRubberBand();

    __super::OnMouseMove(nFlags, point);
}

BOOL CRevisionGraphWnd::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
    CRect viewRect = GetViewRect();

    LPWSTR    cursorID       = IDC_ARROW;
    HINSTANCE resourceHandle = nullptr;

    if ((nHitTest == HTCLIENT) && (pWnd == this) && (viewRect.Width()) && (viewRect.Height()) && (message))
    {
        POINT pt;
        if (GetCursorPos(&pt))
        {
            ScreenToClient(&pt);
            if (m_overviewPosRect.PtInRect(pt))
            {
                resourceHandle = AfxGetResourceHandle();
                cursorID       = (GetKeyState(VK_LBUTTON) & 0x8000)
                                     ? MAKEINTRESOURCE(IDC_PANCURDOWN)
                                     : MAKEINTRESOURCE(IDC_PANCUR);
            }
        }
    }

    HCURSOR hCur = LoadCursor(resourceHandle, cursorID);
    if (GetCursor() != hCur)
        SetCursor(hCur);

    return TRUE;
}

void CRevisionGraphWnd::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == GLYPH_HOVER_EVENT)
    {
        KillTimer(GLYPH_HOVER_EVENT);

        m_showHoverGlyphs = true;
        Invalidate(FALSE);
    }
    else
    {
        __super::OnTimer(nIDEvent);
    }
}

LRESULT CRevisionGraphWnd::OnWorkerThreadDone(WPARAM, LPARAM)
{
    // handle potential race condition between PostMessage and leaving job:
    // the background job may not have exited, yet

    if (updateJob.get())
        updateJob->GetResult();

    InitView();
    BuildPreview();
    Invalidate(FALSE);

    SVN                        svn;
    LogCache::CRepositoryInfo& cachedProperties = svn.GetLogCachePool()->GetRepositoryInfo();

    CSyncPointer<const CFullHistory> fullHistoy(m_state.GetFullHistory());
    if (fullHistoy.get() != nullptr)
    {
        bool doRetry = false;
        SetDlgTitle(cachedProperties.IsOffline(fullHistoy->GetRepositoryUUID(), fullHistoy->GetRepositoryRoot(), false, L"", doRetry));
    }

    if (m_parent && !m_parent->GetOutputFile().IsEmpty())
    {
        // save the graph to the output file and exit
        SaveGraphAs(m_parent->GetOutputFile());
        PostQuitMessage(0);
    }

    return 0;
}

void CRevisionGraphWnd::SetDlgTitle(bool offline)
{
    if (m_sTitle.IsEmpty())
        GetParent()->GetWindowText(m_sTitle);

    CString newTitle;
    if (offline)
        newTitle.Format(IDS_REVGRAPH_DLGTITLEOFFLINE, static_cast<LPCWSTR>(m_sTitle));
    else
        newTitle = m_sTitle;

    CAppUtils::SetWindowTitle(GetParent()->GetSafeHwnd(), m_sPath, newTitle);
}
