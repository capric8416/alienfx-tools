#include <windows.h>
#include <powrprof.h>
#include <string>
#include "Resource.h"
#include "alienfan-SDK.h"
#include "ConfigFan.h"
#include "MonHelper.h"
#include "common.h"
#include <windowsx.h>

#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
//#pragma comment(lib,"Version.lib")
#pragma comment(lib,"PowrProf.lib")

using namespace std;

#define MAX_LOADSTRING 100

// Global Variables:
HINSTANCE hInst;                                // current instance

AlienFan_SDK::Control* acpi = NULL;             // ACPI control object
ConfigFan* fan_conf = NULL;                     // Config...
MonHelper* mon = NULL;                          // Monitoring object

UINT newTaskBar = RegisterWindowMessage(TEXT("TaskbarCreated"));
HWND mDlg = NULL, fanWindow = NULL, tipWindow = NULL;

const vector<string> pModes{ "Off", "Enabled", "Aggressive", "Efficient", "Efficient aggressive" };

extern HWND toolTip;

GUID* sch_guid, perfset;

NOTIFYICONDATA* niData = NULL;

bool isNewVersion = false;
bool needUpdateFeedback = false;
bool needRemove = false;

void UpdateFanUI(LPVOID);
ThreadHelper* fanThread = NULL;

// Forward declarations of functions included in this code module:
//ATOM                MyRegisterClass(HINSTANCE hInstance);
HWND                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    FanDialog(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    FanCurve(HWND, UINT, WPARAM, LPARAM);

//extern void ResetDPIScale();

extern void ReloadFanView(HWND list);
extern void ReloadPowerList(HWND list);
extern void ReloadTempView(HWND list);
HWND CreateToolTip(HWND hwndParent, HWND oldTip);
extern void SetCurrentGmode();

extern bool fanMode;
extern HANDLE ocStopEvent;
DWORD WINAPI CheckFanOverboost(LPVOID lpParam);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    //UNREFERENCED_PARAMETER(lpCmdLine);
    //UNREFERENCED_PARAMETER(nCmdShow);

    ResetDPIScale(lpCmdLine);

    MSG msg{0};

    fan_conf = new ConfigFan();
    fan_conf->wasAWCC = DoStopService(fan_conf->awcc_disable, true);

    NOTIFYICONDATA fanIcon{ sizeof(NOTIFYICONDATA), 0, IDI_ALIENFANGUI, NIF_ICON | NIF_MESSAGE | NIF_TIP, WM_APP + 1,
        (HICON)LoadImage(GetModuleHandle(NULL),
            MAKEINTRESOURCE(IDI_ALIENFANGUI),
            IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON),
            GetSystemMetrics(SM_CYSMICON),
            LR_DEFAULTCOLOR) };

    niData = &fanIcon;

    Shell_NotifyIcon(NIM_ADD, niData);

    acpi = new AlienFan_SDK::Control();

    if (acpi->Probe()) {
        //Shell_NotifyIcon(NIM_DELETE, niData);
        fan_conf->SetBoostsAndNames(acpi);
        mon = new MonHelper(fan_conf);

        if (!(InitInstance(hInstance, fan_conf->startMinimized ? SW_HIDE : SW_NORMAL))) {
            return FALSE;
        }

        // Main message loop:
        while ((GetMessage(&msg, 0, 0, 0)) != 0) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        delete mon;
    }
    else {
        ShowNotification(niData, "Error", "Compatible hardware not found!", false);
    }

    if (!acpi->GetDeviceFlags()) {
        WindowsStartSet(fan_conf->startWithWindows = false, "AlienFan-GUI");
        Sleep(5000);
    }
    Shell_NotifyIcon(NIM_DELETE, niData);

    DoStopService(fan_conf->wasAWCC, false);

    delete acpi;
    delete fan_conf;

    return 0;
}

HWND InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance; // Store instance handle in our global variable

    if (mDlg = CreateDialog(hInstance, MAKEINTRESOURCE(IDD_MAIN_VIEW), NULL, (DLGPROC)FanDialog)) {

        SendMessage(mDlg, WM_SETICON, ICON_BIG, (LPARAM)LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ALIENFANGUI)));
        SendMessage(mDlg, WM_SETICON, ICON_SMALL, (LPARAM)LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ALIENFANGUI), IMAGE_ICON, 16, 16, 0));

        //power mode hotkeys
        for (int i = 0; i < 6; i++)
            RegisterHotKey(mDlg, 20 + i, MOD_CONTROL | MOD_ALT, 0x30 + i);
        RegisterHotKey(mDlg, 6, 0, VK_F17);

        ShowWindow(mDlg, nCmdShow);

    }

    return mDlg;
}

void StartOverboost(HWND hDlg, int fan) {
    EnableWindow(GetDlgItem(hDlg, IDC_COMBO_POWER), false);
    CreateThread(NULL, 0, CheckFanOverboost, (LPVOID)&fan, 0, NULL);
    SetWindowText(GetDlgItem(hDlg, IDC_BUT_OVER), "Stop Overboost");
}

void RestoreApp() {
    ShowWindow(fanWindow, SW_RESTORE);
    SendMessage(fanWindow, WM_PAINT, 0, 0);
    ShowWindow(mDlg, SW_RESTORE);
    SetForegroundWindow(mDlg);
}

LRESULT CALLBACK FanDialog(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    HWND power_list = GetDlgItem(hDlg, IDC_COMBO_POWER),
        g_mode = GetDlgItem(mDlg, IDC_CHECK_GMODE);

    if (message == newTaskBar) {
        // Started/restarted explorer...
        AddTrayIcon(niData, fan_conf->updateCheck);
        //if (Shell_NotifyIcon(NIM_ADD, niData) && fan_conf->updateCheck)
        //    CreateThread(NULL, 0, CUpdateCheck, niData, 0, NULL);
        //return true;
    }

    switch (message) {
    case WM_INITDIALOG:
    {
        niData->hWnd = hDlg;
        AddTrayIcon(niData, fan_conf->updateCheck);
        //if (Shell_NotifyIcon(NIM_ADD, niData) && fan_conf->updateCheck)
        //    CreateThread(NULL, 0, CUpdateCheck, niData, 0, NULL);

        // set PerfBoost lists...
        IIDFromString(L"{be337238-0d82-4146-a960-4f3749d470c7}", &perfset);
        PowerGetActiveScheme(NULL, &sch_guid);
        DWORD acMode, dcMode;
        PowerReadACValueIndex(NULL, sch_guid, &GUID_PROCESSOR_SETTINGS_SUBGROUP, &perfset, &acMode);
        PowerReadDCValueIndex(NULL, sch_guid, &GUID_PROCESSOR_SETTINGS_SUBGROUP, &perfset, &dcMode);

        UpdateCombo(GetDlgItem(hDlg, IDC_AC_BOOST), pModes, acMode);
        UpdateCombo(GetDlgItem(hDlg, IDC_DC_BOOST), pModes, dcMode);;

        // So open fan control window...
        RECT cDlg;
        GetWindowRect(hDlg, &cDlg);
        int wh = cDlg.bottom - cDlg.top;// -2 * GetSystemMetrics(SM_CYBORDER);
        tipWindow = fanWindow = CreateWindow("STATIC", "Fan curve", WS_CAPTION | WS_POPUP | WS_SIZEBOX,//WS_OVERLAPPED,
                                 cDlg.right, cDlg.top, wh, wh,
                                 hDlg, NULL, hInst, 0);
        SetWindowLongPtr(fanWindow, GWLP_WNDPROC, (LONG_PTR) FanCurve);
        toolTip = CreateToolTip(fanWindow, NULL);
        ShowWindow(fanWindow, fan_conf->startMinimized ? SW_HIDE : SW_SHOWNA);

        ReloadPowerList(power_list);
        ReloadTempView(GetDlgItem(hDlg, IDC_TEMP_LIST));
        ReloadFanView(GetDlgItem(hDlg, IDC_FAN_LIST));

        fanThread = new ThreadHelper(UpdateFanUI, hDlg, 500);

        EnableWindow(g_mode, acpi->GetDeviceFlags() & DEV_FLAG_GMODE);
        Button_SetCheck(g_mode, fan_conf->lastProf->gmode);

        CheckMenuItem(GetMenu(hDlg), IDM_SETTINGS_STARTWITHWINDOWS, fan_conf->startWithWindows ? MF_CHECKED : MF_UNCHECKED);
        CheckMenuItem(GetMenu(hDlg), IDM_SETTINGS_STARTMINIMIZED, fan_conf->startMinimized ? MF_CHECKED : MF_UNCHECKED);
        CheckMenuItem(GetMenu(hDlg), IDM_SETTINGS_UPDATE, fan_conf->updateCheck ? MF_CHECKED : MF_UNCHECKED);
        CheckMenuItem(GetMenu(hDlg), IDM_DISABLEAWCC, fan_conf->awcc_disable ? MF_CHECKED : MF_UNCHECKED);

        if (!fan_conf->obCheck && MessageBox(NULL, "Fan overboost values not defined!\nDo you want to set it now (it will took some minutes)?", "Question",
            MB_YESNO | MB_ICONINFORMATION) == IDYES) {
            // ask for boost check
            StartOverboost(hDlg, -1);
        }
        fan_conf->obCheck = 1;

        return true;
    } break;
    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);

        switch (wmId) {
        case IDC_AC_BOOST: case IDC_DC_BOOST: {
            switch (HIWORD(wParam)) {
            case CBN_SELCHANGE:
            {
                int cBst = ComboBox_GetCurSel(GetDlgItem(hDlg, wmId));
                if (wmId == IDC_AC_BOOST)
                    PowerWriteACValueIndex(NULL, sch_guid, &GUID_PROCESSOR_SETTINGS_SUBGROUP, &perfset, cBst);
                else
                    PowerWriteDCValueIndex(NULL, sch_guid, &GUID_PROCESSOR_SETTINGS_SUBGROUP, &perfset, cBst);
                PowerSetActiveScheme(NULL, sch_guid);
            } break;
            }
        } break;
        case IDOK: {
            HWND senList = GetDlgItem(hDlg, IDC_TEMP_LIST), editC = ListView_GetEditControl(senList);
            if (editC) {
                RECT rect;
                ListView_GetSubItemRect(senList, ListView_GetSelectionMark(senList), 1, LVIR_LABEL, &rect);
                SetWindowPos(editC, HWND_TOP, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, 0);
            }
        } break;
        case IDC_COMBO_POWER:
        {
            switch (HIWORD(wParam)) {
            case CBN_SELCHANGE:
            {
                fan_conf->lastProf->powerStage = (WORD)ComboBox_GetCurSel(power_list);
                acpi->SetPower(acpi->powers[fan_conf->lastProf->powerStage]);
                //fan_conf->Save();
            } break;
            case CBN_EDITCHANGE:
            {
                char buffer[MAX_PATH];
                GetWindowText(power_list, buffer, MAX_PATH);
                if (fan_conf->lastProf->powerStage > 0) {
                    fan_conf->powers.find(acpi->powers[fan_conf->lastProf->powerStage])->second = buffer;
                    //fan_conf->Save();
                    ComboBox_DeleteString(power_list, fan_conf->lastProf->powerStage);
                    ComboBox_InsertString(power_list, fan_conf->lastProf->powerStage, buffer);
                }
                break;
            }
            }
        } break;
        case IDC_BUT_MINIMIZE:
            ShowWindow(fanWindow, SW_HIDE);
            ShowWindow(hDlg, SW_HIDE);
            break;
        case IDM_ABOUT:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hDlg, About);
            break;
        case IDC_BUT_CLOSE: case IDM_EXIT:
            SendMessage(hDlg, WM_CLOSE, 0, 0);
            break;
        case IDM_SETTINGS_STARTWITHWINDOWS:
        {
            fan_conf->startWithWindows = !fan_conf->startWithWindows;
            CheckMenuItem(GetMenu(hDlg), IDM_SETTINGS_STARTWITHWINDOWS, fan_conf->startWithWindows ? MF_CHECKED : MF_UNCHECKED);
            WindowsStartSet(fan_conf->startWithWindows, "AlienFan-GUI");
        } break;
        case IDM_SETTINGS_STARTMINIMIZED:
        {
            fan_conf->startMinimized = !fan_conf->startMinimized;
            CheckMenuItem(GetMenu(hDlg), IDM_SETTINGS_STARTMINIMIZED, fan_conf->startMinimized ? MF_CHECKED : MF_UNCHECKED);
        } break;
        case IDM_SETTINGS_UPDATE: {
            fan_conf->updateCheck = !fan_conf->updateCheck;
            CheckMenuItem(GetMenu(hDlg), IDM_SETTINGS_UPDATE, fan_conf->updateCheck ? MF_CHECKED : MF_UNCHECKED);
            if (fan_conf->updateCheck)
                CreateThread(NULL, 0, CUpdateCheck, niData, 0, NULL);
        } break;
        case IDM_DISABLEAWCC: {
            fan_conf->awcc_disable = !fan_conf->awcc_disable;
            CheckMenuItem(GetMenu(hDlg), IDM_DISABLEAWCC, fan_conf->updateCheck ? MF_CHECKED : MF_UNCHECKED);
            fan_conf->wasAWCC = DoStopService((bool)fan_conf->awcc_disable != fan_conf->wasAWCC, fan_conf->wasAWCC);
        } break;
        case IDC_BUT_RESET:
        {
            temp_block *cur = fan_conf->FindSensor(fan_conf->lastSelectedSensor);
            if (cur) {
                fan_block *fan = fan_conf->FindFanBlock(cur, fan_conf->lastSelectedFan);
                if (fan) {
                    fan->points = {{0,0},{100,100}};
                    SendMessage(fanWindow, WM_PAINT, 0, 0);
                }
            }
        } break;
        case IDC_MAX_RESET:
            mon->maxTemps = mon->senValues;
            ReloadTempView(GetDlgItem(hDlg, IDC_TEMP_LIST));
            break;
        case IDC_BUT_OVER:
            if (fanMode) {
                StartOverboost(hDlg, fan_conf->lastSelectedFan);
            }
            else {
                SetEvent(ocStopEvent);
                SetWindowText(GetDlgItem(hDlg, IDC_BUT_OVER), "Overboost");
            }
            break;
        case IDC_CHECK_GMODE:
            fan_conf->lastProf->gmode = (IsDlgButtonChecked(hDlg, wmId) == BST_CHECKED);
            SetCurrentGmode();
            break;
        }
    }
    break;
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) {
            // go to tray...
            ShowWindow(hDlg, SW_HIDE);
        }
        break;
    case WM_MOVE:
    {
        // Reposition child...
        RECT cDlg;
        GetWindowRect(hDlg, &cDlg);
        SetWindowPos(fanWindow, hDlg, cDlg.right, cDlg.top, 0, 0, SWP_NOSIZE | SWP_NOREDRAW | SWP_NOACTIVATE);
    } break;
    case WM_APP + 1:
    {
        switch (lParam) {
        case WM_RBUTTONUP: case WM_CONTEXTMENU:
        {
            POINT lpClickPoint;
            HMENU tMenu = LoadMenu(hInst, MAKEINTRESOURCEA(IDR_TRAYMENU));
            tMenu = GetSubMenu(tMenu, 0);
            MENUINFO mi{ sizeof(MENUINFO), MIM_STYLE, MNS_NOTIFYBYPOS };
            SetMenuInfo(tMenu, &mi);
            MENUITEMINFO mInfo{ sizeof(MENUITEMINFO), MIIM_STRING | MIIM_ID | MIIM_STATE };
            HMENU pMenu;
            pMenu = CreatePopupMenu();
            mInfo.wID = ID_TRAYMENU_POWER_SELECTED;
            for (int i = 0; i < acpi->powers.size(); i++) {
                mInfo.dwTypeData = (LPSTR)fan_conf->powers.find(acpi->powers[i])->second.c_str();
                mInfo.fState = i == fan_conf->lastProf->powerStage ? MF_CHECKED : MF_UNCHECKED;
                InsertMenuItem(pMenu, i, false, &mInfo);
            }
            ModifyMenu(tMenu, ID_MENU_POWER, MF_BYCOMMAND | MF_STRING | MF_POPUP, (UINT_PTR)pMenu, ("Power mode - " +
                fan_conf->powers.find(acpi->powers[fan_conf->lastProf->powerStage])->second).c_str());
            EnableMenuItem(tMenu, ID_MENU_GMODE, acpi->GetDeviceFlags() & DEV_FLAG_GMODE ? MF_ENABLED : MF_DISABLED);
            CheckMenuItem(tMenu, ID_MENU_GMODE, fan_conf->lastProf->gmode ? MF_CHECKED : MF_UNCHECKED);

            GetCursorPos(&lpClickPoint);
            SetForegroundWindow(hDlg);
            int res = TrackPopupMenu(tMenu, TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_BOTTOMALIGN,
                lpClickPoint.x, lpClickPoint.y, 0, hDlg, NULL);
            int i = 0;
        } break;
        case WM_LBUTTONDBLCLK:
        case WM_LBUTTONUP:
            RestoreApp();
            break;
        case NIN_BALLOONHIDE: case NIN_BALLOONTIMEOUT:
            if (!isNewVersion && needRemove) {
                Shell_NotifyIcon(NIM_DELETE, niData);
                Shell_NotifyIcon(NIM_ADD, niData);
                needRemove = false;
            }
            else
                isNewVersion = false;
            break;
        case NIN_BALLOONUSERCLICK:
        {
            if (isNewVersion) {
                ShellExecute(NULL, "open", "https://github.com/T-Troll/alienfx-tools/releases", NULL, NULL, SW_SHOWNORMAL);
                isNewVersion = false;
            }
        } break;
        }
        break;
    } break;
    case WM_HOTKEY: {
        if (wParam > 19 && wParam - 20 < acpi->powers.size()) {
            fan_conf->lastProf->powerStage = (WORD)wParam - 20;
            acpi->SetPower(acpi->powers[fan_conf->lastProf->powerStage]);
            ComboBox_SetCurSel(power_list, fan_conf->lastProf->powerStage);
        }
        switch (wParam) {
        case 6: // G-key for Dell G-series power switch
            fan_conf->lastProf->gmode = !fan_conf->lastProf->gmode;
            SetCurrentGmode();
            Button_SetCheck(g_mode, fan_conf->lastProf->gmode);
            break;
        }
    } break;
    case WM_MENUCOMMAND: {
        int idx = LOWORD(wParam);
        switch (GetMenuItemID((HMENU)lParam, idx)) {
        case ID_MENU_EXIT:
            SendMessage(hDlg, WM_CLOSE, 0, 0);
            return true;
        case ID_MENU_RESTORE:
            RestoreApp();
            break;
        case ID_MENU_GMODE:
            fan_conf->lastProf->gmode = !fan_conf->lastProf->gmode;
            SetCurrentGmode();
            Button_SetCheck(g_mode, fan_conf->lastProf->gmode);
            break;
        case ID_TRAYMENU_POWER_SELECTED:
            if (idx != fan_conf->lastProf->powerStage) {
                fan_conf->lastProf->powerStage = idx;
                acpi->SetPower(acpi->powers[idx]);
                ComboBox_SetCurSel(power_list, fan_conf->lastProf->powerStage);
            }
            break;
        }
    } break;
    case WM_NOTIFY:
        switch (((NMHDR*)lParam)->idFrom) {
        case IDC_FAN_LIST:
            switch (((NMHDR*)lParam)->code) {
            case LVN_ITEMCHANGED:
            {
                NMLISTVIEW* lPoint = (LPNMLISTVIEW)lParam;
                if (lPoint->uNewState & LVIS_SELECTED && lPoint->iItem != -1) {
                    // Select other fan....
                    fan_conf->lastSelectedFan = lPoint->iItem;
                    // Redraw fans
                    SendMessage(fanWindow, WM_PAINT, 0, 0);
                    break;
                }
                if (fan_conf->lastSelectedSensor != -1 && lPoint->uNewState & 0x3000) {
                    temp_block* sen = fan_conf->FindSensor(fan_conf->lastSelectedSensor);
                    switch (lPoint->uNewState & 0x3000) {
                    case 0x1000:
                        // Remove fan
                        if (sen) { // remove sensor block
                            for (auto iFan = sen->fans.begin(); iFan < sen->fans.end(); iFan++)
                                if (iFan->fanIndex == lPoint->iItem) {
                                    sen->fans.erase(iFan);
                                    break;
                                }
                            if (!sen->fans.size()) // remove sensor block!
                                for (auto iSen = fan_conf->lastProf->fanControls.begin();
                                    iSen < fan_conf->lastProf->fanControls.end(); iSen++)
                                    if (iSen->sensorIndex == sen->sensorIndex) {
                                        fan_conf->lastProf->fanControls.erase(iSen);
                                        break;
                                    }
                        }
                        break;
                    case 0x2000:
                        // add fan
                        if (!sen) { // add new sensor block
                            fan_conf->lastProf->fanControls.push_back({ (short)fan_conf->lastSelectedSensor });
                            sen = &fan_conf->lastProf->fanControls.back();
                        }
                        if (!fan_conf->FindFanBlock(sen, lPoint->iItem)) {
                            sen->fans.push_back({ (short)lPoint->iItem,{{0,0},{100,100}} });
                        }
                        break;
                    }
                    ListView_SetItemState(((NMHDR*)lParam)->hwndFrom, -1, 0, LVIS_SELECTED);
                    ListView_SetItemState(((NMHDR*)lParam)->hwndFrom, lPoint->iItem, LVIS_SELECTED, LVIS_SELECTED);
                }
            } break;
            }
            break;
        case IDC_TEMP_LIST: {
            HWND tempList = GetDlgItem(hDlg, IDC_TEMP_LIST);
            switch (((NMHDR*)lParam)->code) {
            case LVN_BEGINLABELEDIT: {
                fanThread->Stop();
                NMLVDISPINFO* sItem = (NMLVDISPINFO*)lParam;
                auto pwr = fan_conf->sensors.find((byte)sItem->item.lParam);
                Edit_SetText(ListView_GetEditControl(tempList), (pwr != fan_conf->sensors.end() ? pwr->second : acpi->sensors[sItem->item.lParam].name).c_str());
            } break;
            case LVN_ITEMACTIVATE: case NM_RETURN:
            {
                int pos = ListView_GetHotItem(tempList);
                RECT rect;
                ListView_GetSubItemRect(tempList, pos, 1, LVIR_BOUNDS, &rect);
                SetWindowPos(ListView_EditLabel(tempList, pos), HWND_TOP, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, 0);
            } break;
            case LVN_ENDLABELEDIT:
            {
                NMLVDISPINFO* sItem = (NMLVDISPINFO*)lParam;
                if (sItem->item.pszText && (unsigned)sItem->item.lParam < acpi->sensors.size()) {
                    auto pwr = fan_conf->sensors.find(MAKEWORD(acpi->sensors[sItem->item.lParam].senIndex,
                        acpi->sensors[sItem->item.lParam].type));
                    if (pwr == fan_conf->sensors.end()) {
                        if (strlen(sItem->item.pszText))
                            fan_conf->sensors.emplace((byte)sItem->item.lParam, sItem->item.pszText);
                    }
                    else {
                        if (strlen(sItem->item.pszText))
                            pwr->second = sItem->item.pszText;
                        else
                            fan_conf->sensors.erase(pwr);
                    }
                }
                ReloadTempView(tempList);
                fanThread->Start();
            } break;
            case LVN_ITEMCHANGED:
            {
                NMLISTVIEW* lPoint = (LPNMLISTVIEW)lParam;
                if (lPoint->uNewState & LVIS_FOCUSED) {
                    // Select other item...
                    if (lPoint->iItem != -1) {
                        // Select other fan....
                        fan_conf->lastSelectedSensor = lPoint->iItem;
                        // Redraw fans
                        ReloadFanView(GetDlgItem(hDlg, IDC_FAN_LIST));
                        SendMessage(fanWindow, WM_PAINT, 0, 0);
                    }
                }
            } break;
            }
        } break;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hDlg);
        break;
    case WM_DESTROY:
        delete fanThread;
        LocalFree(sch_guid);
        PostQuitMessage(0);
        break;
    case WM_ENDSESSION:
        // Shutdown/restart scheduled....
        fan_conf->Save();
        delete fanThread;
        delete mon;
        LocalFree(sch_guid);
        return 0;
    case WM_POWERBROADCAST:
        switch (wParam) {
        case PBT_APMRESUMEAUTOMATIC:
            mon->Start();
            fanThread->Start();
            if (fan_conf->updateCheck) {
                needUpdateFeedback = false;
                CreateThread(NULL, 0, CUpdateCheck, niData, 0, NULL);
            }
            break;
        case PBT_APMSUSPEND:
            // Sleep initiated.
            fanThread->Stop();
            mon->Stop();
            fan_conf->Save();
            break;
        }
    }
    return 0;
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG: {
        SetDlgItemText(hDlg, IDC_STATIC_VERSION, ("Version: " + GetAppVersion()).c_str());
        return (INT_PTR)TRUE;
    } break;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK: case IDCANCEL:
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        } break;
        }
        break;
    case WM_NOTIFY:
        switch (LOWORD(wParam)) {
        case IDC_SYSLINK_HOMEPAGE:
            switch (((LPNMHDR)lParam)->code)
            {
            case NM_CLICK:
            case NM_RETURN:
            {
                ShellExecute(NULL, "open", "https://github.com/T-Troll/alienfx-tools", NULL, NULL, SW_SHOWNORMAL);
            } break;
            } break;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

void UpdateFanUI(LPVOID lpParam) {
    HWND tempList = GetDlgItem((HWND)lpParam, IDC_TEMP_LIST),
        fanList = GetDlgItem((HWND)lpParam, IDC_FAN_LIST),
        power_list = GetDlgItem((HWND)lpParam, IDC_COMBO_POWER);
    static bool wasBoostMode = false;
    if (!fanMode) wasBoostMode = true;
    if (fanMode && wasBoostMode) {
        EnableWindow(power_list, true);
        SetWindowText(GetDlgItem((HWND)lpParam, IDC_BUT_OVER), "Overboost");
        wasBoostMode = false;
    }
    if (mon) {
        if (!mon->monThread) {
            for (int i = 0; i < acpi->sensors.size(); i++) {
                mon->senValues[i] = acpi->GetTempValue(i);
                if (mon->senValues[i] > mon->maxTemps[i])
                    mon->maxTemps[i] = mon->senValues[i];
            }
            for (int i = 0; i < acpi->fans.size(); i++)
                mon->fanRpm[i] = acpi->GetFanRPM(i);
        }
        if (IsWindowVisible((HWND)lpParam)) {
            //DebugPrint("Fans UI update...\n");
            for (int i = 0; i < acpi->sensors.size(); i++) {
                string name = to_string(mon->senValues[i]) + " (" + to_string(mon->maxTemps[i]) + ")";
                ListView_SetItemText(tempList, i, 0, (LPSTR)name.c_str());
            }
            RECT cArea;
            GetClientRect(tempList, &cArea);
            ListView_SetColumnWidth(tempList, 0, LVSCW_AUTOSIZE);
            ListView_SetColumnWidth(tempList, 1, cArea.right - ListView_GetColumnWidth(tempList, 0));
            for (int i = 0; i < acpi->fans.size(); i++) {
                string name = "Fan " + to_string(i + 1) + " (" + to_string(mon->fanRpm[i]) + ")";
                ListView_SetItemText(fanList, i, 0, (LPSTR)name.c_str());
            }
            SendMessage(fanWindow, WM_PAINT, 0, 0);
        }
        string name = "Power mode: ";
        if (fan_conf->lastProf->gmode)
            name += "G-mode";
        else {
            //if (fan_conf->lastProf->powerStage) {
            //auto pwr = fan_conf->powers.find(acpi->powers[fan_conf->lastProf->powerStage]);
            //name += (pwr != fan_conf->powers.end() ? pwr->second : "Level " + to_string(fan_conf->lastProf->powerStage));
            //}
            //else
            //    name += "Manual";
            name += fan_conf->powers.find(acpi->powers[fan_conf->lastProf->powerStage])->second;
        }
        for (int i = 0; i < acpi->fans.size(); i++) {
            name += "\nFan " + to_string(i + 1) + ": " + to_string(mon->fanRpm[i]) + " RPM";
        }
        strcpy_s(niData->szTip, 127, name.c_str());
        Shell_NotifyIcon(NIM_MODIFY, niData);
    }
}

