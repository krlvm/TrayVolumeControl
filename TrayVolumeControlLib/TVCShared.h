#pragma once

#define WM_TRAYICONVOLUME 0x460

#define UID_TRAYICONVOLUME  100
#define GUID_TRAYICONVOLUME { 0x7820AE73, 0x23E3, 0x4229, { 0x82, 0xC1, 0xE4, 0x1C, 0xB6, 0x7D, 0x5B, 0x9C } };
//                          {   7820AE73 -  23E3 -  4229  -   82    C1 -  E4    1C    B6    7D    5B    9C   }

HWND FindTrayToolbarWindow()
{
	HWND hWnd = FindWindow(L"Shell_TrayWnd", NULL);
	if (hWnd)
	{
		hWnd = FindWindowEx(hWnd, NULL, L"TrayNotifyWnd", NULL);
		if (hWnd)
		{
			hWnd = FindWindowEx(hWnd, NULL, L"SysPager", NULL);
			if (hWnd)
			{
				hWnd = FindWindowEx(hWnd, NULL, L"ToolbarWindow32", NULL);
			}
		}
	}
	return hWnd;
}