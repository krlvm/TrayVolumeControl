#include <iostream>
#include <Windows.h>
#include <CommCtrl.h>
#pragma comment(lib, "comctl32.lib")

#define UID_TRAYICONVOLUME  100

const LPCWSTR szWindowClass = L"TRAYVOLCTRL";
const UINT WM_TASKBARCREATED = RegisterWindowMessage(L"TaskbarCreated");

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

struct TRAYDATA
{
	HWND hwnd;
	UINT uID;
	UINT uCallbackMessage;
	DWORD Reserved[2];
	HICON hIcon;
};

bool InjectHook()
{
	HWND hWndTray = FindTrayToolbarWindow();
	if (!hWndTray) return false;

	int count = (int)SendMessage(hWndTray, TB_BUTTONCOUNT, 0, 0);

	DWORD dwProcessID;
	GetWindowThreadProcessId(hWndTray, &dwProcessID);
	HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, dwProcessID);

	if (!hProcess) return false;

	bool bResult = false;

	for (int i = count; i >= 0; i--)
	{
		size_t dwBytesRead;

		TBBUTTON tbButton;
		ZeroMemory(&tbButton, sizeof(TBBUTTON));
		void* lpButton = VirtualAllocEx(hProcess, NULL, sizeof(TBBUTTON), MEM_COMMIT, PAGE_READWRITE);

		if (!WriteProcessMemory(hProcess, lpButton, &tbButton, sizeof(TBBUTTON), &dwBytesRead)) continue;
		SendMessage(hWndTray, TB_GETBUTTON, i, (LPARAM)lpButton);
		if (!ReadProcessMemory(hProcess, lpButton, &tbButton, sizeof(TBBUTTON), &dwBytesRead)) continue;

		TRAYDATA trayData;
		ReadProcessMemory(hProcess, (void*)tbButton.dwData, &trayData, sizeof(TRAYDATA), &dwBytesRead);

		VirtualFreeEx(hProcess, lpButton, 0, MEM_RELEASE);

		if (trayData.uID != UID_TRAYICONVOLUME) continue;

		DWORD dwIconThreadID = GetWindowThreadProcessId(trayData.hwnd, NULL);

		HINSTANCE hLibrary = LoadLibrary(L".\\TrayVolumeControlLib.dll");
		if (!hLibrary) break;

		HOOKPROC hHookProc = (HOOKPROC)GetProcAddress(hLibrary, "CallWndProc");
		if (!hHookProc) break;

		if (SetWindowsHookEx(WH_CALLWNDPROC, hHookProc, hLibrary, dwIconThreadID))
		{
			bResult = true;
		}

		break;
	}

	CloseHandle(hProcess);

	return bResult;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	LRESULT lResult = DefWindowProc(hWnd, message, wParam, lParam);
	if (message == WM_TASKBARCREATED)
	{
		Sleep(2500);
		InjectHook();
	}
	return lResult;
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR    lpCmdLine,
	_In_ int       nCmdShow)
{
	HANDLE hMutex = CreateMutex(NULL, TRUE, szWindowClass);
	if (!hMutex || ERROR_ALREADY_EXISTS == GetLastError())
	{
		return ERROR_ALREADY_EXISTS;
	}

	InjectHook();


	WNDCLASSEX wcex = {};
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.lpfnWndProc = WndProc;
	wcex.hInstance = hInstance;
	wcex.lpszClassName = szWindowClass;
	if (!RegisterClassEx(&wcex))
	{
		return 2;
	}
	CreateWindowEx(0, szWindowClass, nullptr, 0, 0, 0, 0, 0, nullptr, NULL, NULL, NULL);

	
	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	ReleaseMutex(hMutex);
	CloseHandle(hMutex);

	return (int)msg.wParam;
}