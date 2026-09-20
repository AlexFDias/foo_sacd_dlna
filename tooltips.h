#pragma once
#include "stdafx.h"

class sacd_tooltips {
public:
    sacd_tooltips() = default;
    ~sacd_tooltips() { destroy(); }

    void create(HWND parent) {
        if (m_hwnd) return;
        m_hwnd = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            parent, nullptr, core_api::get_my_instance(), nullptr);
        if (!m_hwnd) return;
        SendMessageW(m_hwnd, TTM_SETMAXTIPWIDTH, 0, 560);
        SendMessageW(m_hwnd, TTM_SETDELAYTIME, TTDT_AUTOPOP, 15000);
        SendMessageW(m_hwnd, TTM_SETDELAYTIME, TTDT_INITIAL, 450);
    }

    void add(HWND parent, int id, const char* text) {
        if (!m_hwnd) return;
        HWND control = GetDlgItem(parent, id);
        if (!control) return;
        TOOLINFOA ti{};
        ti.cbSize = sizeof(ti);
        ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        ti.hwnd = parent;
        ti.uId = reinterpret_cast<UINT_PTR>(control);
        ti.lpszText = const_cast<char*>(text);
        SendMessageA(m_hwnd, TTM_ADDTOOLA, 0, reinterpret_cast<LPARAM>(&ti));
    }

    void destroy() {
        if (m_hwnd) {
            DestroyWindow(m_hwnd);
            m_hwnd = nullptr;
        }
    }

private:
    HWND m_hwnd = nullptr;
};
