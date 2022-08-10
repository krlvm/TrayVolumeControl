#include "pch.h"

#define WM_TRAYICONVOLUME 0x460

#define UID_TRAYICONVOLUME  100
#define GUID_TRAYICONVOLUME { 0x7820AE73, 0x23E3, 0x4229, { 0x82, 0xC1, 0xE4, 0x1C, 0xB6, 0x7D, 0x5B, 0x9C } };
//                          {   7820AE73 -  23E3 -  4229  -   82    C1 -  E4    1C    B6    7D    5B    9C   }

thread_local int nSign = 0;
thread_local unsigned short nVolume = 0;

thread_local UINT_PTR uTooltipTimerId;

thread_local bool bIsListeningInput = false;
thread_local bool bSuspendListeningInput = false;
thread_local HWND hWndTooltip = NULL;

bool CheckIfCursorIsInTrayIconBounds(HWND hWnd);

typedef HRESULT (*AudioEndpointVolumeHandler)(IAudioEndpointVolume* endpointVolume);
HRESULT ConfigureAudioEndpointVolume(AudioEndpointVolumeHandler handler)
{
    HRESULT hr = S_OK;

    hr = CoInitialize(NULL);
    if (FAILED(hr)) return hr;

    IMMDeviceEnumerator* deviceEnumerator = NULL;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator), (LPVOID*)&deviceEnumerator);
    if (FAILED(hr)) return hr;
    
    IMMDevice* defaultDevice = NULL;
    hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &defaultDevice);
    if (FAILED(hr)) return hr;

    deviceEnumerator->Release();
    deviceEnumerator = NULL;

    IAudioEndpointVolume* endpointVolume = NULL;
    hr = defaultDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, NULL, (LPVOID*)&endpointVolume);
    if (FAILED(hr)) return hr;

    defaultDevice->Release();
    defaultDevice = NULL;

    hr = handler(endpointVolume);

    endpointVolume->Release();

    CoUninitialize();

    return hr;
}

#pragma region "Tooltip Management"
void DestroyTooltipTimerProc(
    HWND hWnd,
    UINT uMsg,
    UINT_PTR idEvent,
    DWORD dwTime
)
{
    if (!hWndTooltip) return;
    if (!DestroyWindow(hWndTooltip)) return;
    hWndTooltip = NULL;
}
void ShowVolumeTooltip()
{
    KillTimer(NULL, uTooltipTimerId);

    wchar_t text[5];
    wsprintf(text, L"%d%%", nVolume);

    TOOLINFO ti;
    ZeroMemory(&ti, sizeof(TOOLINFO));
    ti.cbSize = sizeof(TOOLINFO);
    ti.uId = (UINT)hWndTooltip;
    ti.lpszText = text;

    if (!hWndTooltip)
    {
        hWndTooltip = CreateWindowEx(WS_EX_TOPMOST,
            TOOLTIPS_CLASS, NULL, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            NULL, NULL, NULL, NULL);
        ti.uId = (UINT)hWndTooltip;

        SendMessage(hWndTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);
    }
    else
    {
        SendMessage(hWndTooltip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&ti);
        SendMessage(hWndTooltip, TTM_UPDATETIPTEXT, 0, (LPARAM)&ti);
    }

    SendMessage(hWndTooltip, TTM_TRACKACTIVATE, TRUE, (LPARAM)&ti);
    SendMessage(hWndTooltip, TTM_POPUP, 0, 0);

    uTooltipTimerId = SetTimer(NULL, 0, 500, &DestroyTooltipTimerProc);
}
#pragma endregion

LRESULT CALLBACK SubclassProc(
    HWND hWnd,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam,
    UINT_PTR uIdSubclass,
    DWORD_PTR dwRefData
)
{
    if (bIsListeningInput && uMsg == WM_INPUT && CheckIfCursorIsInTrayIconBounds(hWnd) && !bSuspendListeningInput)
    {
        RAWINPUT raw;
        ZeroMemory(&raw, sizeof(RAWINPUT));

        UINT cbSize = sizeof(RAWINPUT);
        GetRawInputData((HRAWINPUT)lParam, RID_INPUT, &raw, &cbSize, sizeof(RAWINPUTHEADER));

        if (raw.header.dwType == RIM_TYPEMOUSE && ((raw.data.mouse.usButtonFlags & RI_MOUSE_WHEEL) == RI_MOUSE_WHEEL))
        {
            // short delta = ((short)raw.data.mouse.usButtonData) / WHEEL_DELTA;
            nSign = ((short)raw.data.mouse.usButtonData) > 0 ? 1 : -1;

            HRESULT hr = ConfigureAudioEndpointVolume([](IAudioEndpointVolume* endpointVolume) {
                float currentVolume = 0;
                endpointVolume->GetMasterVolumeLevelScalar(&currentVolume);

                float delta = 0.01 * nSign;
                currentVolume = max(0, min(1.0f, currentVolume + delta));
                
                nVolume = (short)(currentVolume*100);

                BOOL bIsMute;
                endpointVolume->GetMute(&bIsMute);
                if (!bIsMute && currentVolume == 0)
                {
                    endpointVolume->SetMute(true, NULL);
                }
                else if (bIsMute && currentVolume != 0)
                {
                    endpointVolume->SetMute(false, NULL);
                }

                return endpointVolume->SetMasterVolumeLevelScalar(currentVolume, NULL);
            });

            if (SUCCEEDED(hr))
            {
                ShowVolumeTooltip();
            }
        }
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

extern "C" __declspec(dllexport) LRESULT CALLBACK CallWndProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode >= HC_ACTION)
    {
        LPCWPSTRUCT cwps = (LPCWPSTRUCT)lParam;

        if (cwps->message == WM_TRAYICONVOLUME)
        {
            switch (LOWORD(cwps->lParam))
            {
                case WM_LBUTTONUP:
                {
                    // Avoid changing volume twice:
                    // from Windows 10 volume flyout, which captures mouse scroll,
                    // and from the tray icon, where mouse scroll is also captured by us
                    // TODO: disable this on Windows 11 with classic taskbar and modern flyout
                    bSuspendListeningInput = true;
                    break;
                }
                case WM_MBUTTONUP:
                {
                    ConfigureAudioEndpointVolume([](IAudioEndpointVolume* endpointVolume) {
                        BOOL bIsMute;
                        endpointVolume->GetMute(&bIsMute);
                        return endpointVolume->SetMute(!bIsMute, NULL);
                    });
                    break;
                }
                case WM_MOUSEMOVE:
                {
                    CheckIfCursorIsInTrayIconBounds(cwps->hwnd);
                    break;
                }
            }
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

bool CheckIfCursorIsInTrayIconBounds(HWND hWnd)
{
    NOTIFYICONIDENTIFIER niid;
    ZeroMemory(&niid, sizeof(NOTIFYICONIDENTIFIER));
    niid.cbSize = sizeof(NOTIFYICONIDENTIFIER);
    niid.hWnd = hWnd;
    niid.uID = UID_TRAYICONVOLUME;
    niid.guidItem = GUID_TRAYICONVOLUME;

    POINT ptCursor;
    RECT rcIcon;

    if (GetCursorPos(&ptCursor) && SUCCEEDED(Shell_NotifyIconGetRect(&niid, &rcIcon)))
    {
        bool bIsInBounds = PtInRect(&rcIcon, ptCursor);
        if (bIsInBounds && !bIsListeningInput)
        {
            RAWINPUTDEVICE rid = {
                HID_USAGE_PAGE_GENERIC,
                HID_USAGE_GENERIC_MOUSE,
                RIDEV_INPUTSINK,
                hWnd
            };
            if (RegisterRawInputDevices(&rid, 1, sizeof(rid)))
            {
                bIsListeningInput = true;
                bSuspendListeningInput = false;
                SetWindowSubclass(hWnd, SubclassProc, 0, 0);
            }
        }
        else if (!bIsInBounds && bIsListeningInput)
        {
            RAWINPUTDEVICE rid = {
                HID_USAGE_PAGE_GENERIC,
                HID_USAGE_GENERIC_MOUSE,
                RIDEV_REMOVE
            };
            if (RegisterRawInputDevices(&rid, 1, sizeof(rid)))
            {
                RemoveWindowSubclass(hWnd, SubclassProc, 0);
                bIsListeningInput = false;
            }
            return false;
        }

        return true;
    }

    return false;
}

BOOL APIENTRY DllMain(
    HMODULE hModule,
    DWORD  ul_reason_for_call,
    LPVOID lpReserved
)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}