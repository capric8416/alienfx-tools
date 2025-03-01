#include <pdh.h>
#include <pdhmsg.h>
#include <Psapi.h>
#include <shlwapi.h>
#pragma comment(lib, "pdh.lib")

#include "alienfx-gui.h"
#include "EventHandler.h"

// debug print
#ifdef _DEBUG
#define DebugPrint(_x_) OutputDebugString(_x_);
#else
#define DebugPrint(_x_)
#endif

extern AlienFan_SDK::Control* acpi;
extern EventHandler* eve;
extern MonHelper* mon;

extern void SetTrayTip();

DWORD WINAPI CEventProc(LPVOID);
VOID CALLBACK CForegroundProc(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);
VOID CALLBACK CCreateProc(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);
LRESULT CALLBACK KeyProc(int nCode, WPARAM wParam, LPARAM lParam);

EventHandler::EventHandler()
{
	eve = this;
	ChangePowerState();
	//StartFanMon();
	StartEffects();
}

EventHandler::~EventHandler()
{
	StopProfiles();
	StopEffects();
	StopFanMon();
}

void EventHandler::ChangePowerState()
{
	SYSTEM_POWER_STATUS state;
	GetSystemPowerStatus(&state);
	bool sameState = true;
	if (conf->statePower = state.ACLineStatus) {
		// AC line
		if (state.BatteryFlag & 8)
			// charging
			sameState = fxhl->SetPowerMode(MODE_CHARGE);
		else
			sameState = fxhl->SetPowerMode(MODE_AC);
	}
	else {
		// Battery - check BatteryFlag for details
		if (state.BatteryFlag & 6)
			sameState = fxhl->SetPowerMode(MODE_LOW);
		else
			sameState = fxhl->SetPowerMode(MODE_BAT);
	}
	if (!sameState) {
		DebugPrint(("Power state changed to " + to_string(conf->statePower) + "\n").c_str());
		fxhl->ChangeState();
		ScanTaskList();
		fxhl->Refresh();
	}
}

void EventHandler::ChangeScreenState(DWORD state)
{
	if (conf->lightsOn && (conf->offWithScreen || capt)) {
		if (state == 2) {
			// Dim display
			conf->dimmedScreen = true;
			conf->stateScreen = true;
		}
		else {
			conf->stateScreen = state;
			conf->dimmedScreen = false;
		}
		DebugPrint("Display state changed\n");
	} else {
		conf->dimmedScreen = false;
		conf->stateScreen = true;
	}
	fxhl->ChangeState();
}

void EventHandler::SwitchActiveProfile(profile* newID)
{
	if (keyboardSwitchActive) return;
	if (!newID) newID = conf->FindDefaultProfile();
	if (newID->id != conf->activeProfile->id) {
		// reset effects
		fxhl->UpdateGlobalEffect(NULL, true);
		modifyProfile.lock();
		conf->activeProfile = newID;
		conf->active_set = &newID->lightsets;
		conf->fan_conf->lastProf = newID->flags & PROF_FANS ? &newID->fansets : &conf->fan_conf->prof;
		modifyProfile.unlock();

		if (mon && acpi->GetDeviceFlags() & DEV_FLAG_GMODE && acpi->GetGMode() != conf->fan_conf->lastProf->gmode)
				acpi->SetGMode(conf->fan_conf->lastProf->gmode);

		fxhl->ChangeState();
		ChangeEffectMode();

		DebugPrint((string("Profile switched to ") + to_string(newID->id) + " (" + newID->name + ")\n").c_str());
	}
#ifdef _DEBUG
	else
		DebugPrint((string("Same profile \"") + newID->name + "\", skipping switch.\n").c_str());
#endif
}

void EventHandler::StartEvents()
{
	if (!dwHandle) {
		fxhl->RefreshMon();
		// start thread...

		DebugPrint("Event thread start.\n");

		stopEvents = CreateEvent(NULL, true, false, NULL);
		dwHandle = CreateThread(NULL, 0, CEventProc, this, 0, NULL);
	}
}

void EventHandler::StopEvents()
{
	if (dwHandle) {
		DebugPrint("Event thread stop.\n");

		SetEvent(stopEvents);
		WaitForSingleObject(dwHandle, 3000);
		CloseHandle(dwHandle);
		CloseHandle(stopEvents);
		dwHandle = 0;
	}
}

void EventHandler::ChangeEffectMode() {
	if (conf->enableMon && conf->stateOn) {
		if (conf->GetEffect() != effMode)
			StopEffects();
		else
			fxhl->Refresh();
		StartEffects();
	}
	else
		StopEffects();
	SetTrayTip();
}

void EventHandler::StopEffects() {
	switch (effMode) {
	case 1:	StopEvents(); break; // Events
	case 2: if (capt) { // Ambient
		delete capt; capt = NULL;
	} break;
	case 3: if (audio) { // Haptics
		delete audio; audio = NULL;
	} break;
	case 4: if (grid) {
		delete grid; grid = NULL;
	} break;
	}
	effMode = 0;
	fxhl->Refresh(true);
}

void EventHandler::StartEffects() {
	if (conf->enableMon) {
		// start new mode...
		switch (effMode = conf->GetEffect()) {
		case 1:
			StartEvents();
			break;
		case 2:
			if (!capt) capt = new CaptureHelper();
			break;
		case 3:
			if (!audio) audio = new WSAudioIn();
			break;
		case 4:
			if (!grid) grid = new GridHelper();
			break;
		}
	}
}

void EventHandler::StartFanMon() {
	if (acpi && !mon)
		mon = new MonHelper(conf->fan_conf);
}

void EventHandler::StopFanMon() {
	if (mon) {
		delete mon;
		mon = NULL;
	}
}

void EventHandler::ScanTaskList() {
	DWORD maxProcess=256, cbNeeded, cProcesses;
	DWORD* aProcesses = new DWORD[maxProcess];
	static char szProcessName[32768];

	profile* newp = NULL, *finalP = NULL;

	if (EnumProcesses(aProcesses, maxProcess * sizeof(DWORD), &cbNeeded))
	{
		while ((cProcesses = cbNeeded/sizeof(DWORD))==maxProcess) {
			maxProcess=maxProcess<<1;
			delete[] aProcesses;
			aProcesses=new DWORD[maxProcess];
			EnumProcesses(aProcesses, maxProcess * sizeof(DWORD), &cbNeeded);
		}

		for (UINT i = 0; i < cProcesses; i++)
		{
			if (aProcesses[i])
			{
				HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION |
					PROCESS_VM_READ,
					FALSE, aProcesses[i]);
				if (hProcess && GetProcessImageFileName(hProcess, szProcessName, 32767))
				{
					PathStripPath(szProcessName);
					// is it related to profile?
					if ((newp = conf->FindProfileByApp(string(szProcessName))) && (!finalP || !(finalP->flags & PROF_PRIORITY)))
						finalP = newp;
					CloseHandle(hProcess);
				}
			}
		}
	}
	delete[] aProcesses;
	SwitchActiveProfile(finalP);
}

void EventHandler::CheckProfileWindow(HWND hwnd) {

	static char szProcessName[32768];

	if (hwnd) {
		DWORD prcId;
		GetWindowThreadProcessId(hwnd, &prcId);
		HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, prcId);

		if (hProcess && GetProcessImageFileName(hProcess, szProcessName, 32767)) {
			CloseHandle(hProcess);
			PathStripPath(szProcessName);

			DebugPrint((string("Foreground switched to ") + szProcessName + "\n").c_str());

			if (!conf->noDesktop || (szProcessName != string("ShellExperienceHost.exe")
				&& szProcessName != string("explorer.exe")
				&& szProcessName != string("SearchApp.exe")
#ifdef _DEBUG
				&& szProcessName != string("devenv.exe")
#endif
				)) {

				profile* newProf;

				if (newProf = conf->FindProfileByApp(szProcessName, true)) {
					if (conf->IsPriorityProfile(newProf) || !conf->IsPriorityProfile(conf->activeProfile))
						SwitchActiveProfile(newProf);
				}
				else
					ScanTaskList();

			}
#ifdef _DEBUG
			else {
				DebugPrint("Forbidden app, switch blocked!\n");
			}
#endif
		}
#ifdef _DEBUG
		else {
			DebugPrint("Foreground app unknown\n");
		}
#endif
	}
	else
		ScanTaskList();
}

void EventHandler::StartProfiles()
{
	if (conf->enableProf && !cEvent) {

		DebugPrint("Profile hooks starting.\n");

		hEvent = SetWinEventHook(EVENT_SYSTEM_FOREGROUND,
								 EVENT_SYSTEM_FOREGROUND, NULL,
								 CForegroundProc, 0, 0,
								 WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

		cEvent = SetWinEventHook(EVENT_OBJECT_CREATE,
			EVENT_OBJECT_DESTROY, NULL,
			CCreateProc, 0, 0,
			WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

#ifndef _DEBUG
		kEvent = SetWindowsHookExW(WH_KEYBOARD_LL, KeyProc, NULL, 0);
#endif

		// Need to switch if already running....
		CheckProfileWindow(GetForegroundWindow());

		//SetForegroundWindow(GetForegroundWindow());
	}
}

void EventHandler::StopProfiles()
{
	if (cEvent) {
		DebugPrint("Profile hooks stop.\n");
		UnhookWinEvent(hEvent);
		UnhookWinEvent(cEvent);
		UnhookWindowsHookEx(kEvent);
		cEvent = 0;
		keyboardSwitchActive = false;
	}
}

DWORD counterSize = sizeof(PDH_FMT_COUNTERVALUE_ITEM);
PDH_FMT_COUNTERVALUE_ITEM* counterValues = new PDH_FMT_COUNTERVALUE_ITEM[1], * counterValuesMax = new PDH_FMT_COUNTERVALUE_ITEM[1];

int GetValuesArray(HCOUNTER counter, byte& maxVal, int delta = 0, int divider = 1, HCOUNTER c2 = NULL) {
	PDH_STATUS pdhStatus;
	DWORD count;
	int retVal = 0;

	if (c2) {
		counterSize = sizeof(counterValuesMax);
		while ((pdhStatus = PdhGetFormattedCounterArray(c2, PDH_FMT_LONG, &counterSize, &count, counterValuesMax)) == PDH_MORE_DATA) {
			delete[] counterValuesMax;
			counterValuesMax = new PDH_FMT_COUNTERVALUE_ITEM[counterSize / sizeof(PDH_FMT_COUNTERVALUE_ITEM) + 1];
		}

		//if (pdhStatus != ERROR_SUCCESS) {
		//	return -1;
		//}
	}

	counterSize = sizeof(counterValues);
	while ((pdhStatus = PdhGetFormattedCounterArray( counter, PDH_FMT_LONG, &counterSize, &count, counterValues)) == PDH_MORE_DATA) {
		delete[] counterValues;
		counterValues = new PDH_FMT_COUNTERVALUE_ITEM[counterSize/sizeof(PDH_FMT_COUNTERVALUE_ITEM) + 1];
	}

	//if (pdhStatus != ERROR_SUCCESS) {
	//	return -1;
	//}

	for (DWORD i = 0; i < count; i++) {
		int cval = c2 && counterValuesMax[i].FmtValue.longValue ?
			counterValues[i].FmtValue.longValue * 800 / counterValuesMax[i].FmtValue.longValue :
			counterValues[i].FmtValue.longValue / divider - delta;
		retVal = max(retVal, cval);
	}

	maxVal = max(maxVal, retVal);

	return retVal;
}

// Callback and event processing hooks
// Create - Check process ID, switch if found and no foreground active.
// Foreground - Check process ID, switch if found, clear foreground if not.
// Close - Check process list, switch if found and no foreground active.

static VOID CALLBACK CCreateProc(HWINEVENTHOOK hWinEventHook, DWORD dwEvent, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime) {

	static char szProcessName[32768];
	HANDLE hThread;
	if ((dwEvent == EVENT_OBJECT_CREATE || dwEvent == EVENT_OBJECT_DESTROY) &&
		(hThread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, dwEventThread))) {
		DWORD prcId = GetProcessIdOfThread(hThread);
		if (prcId && idChild == CHILDID_SELF) {
			HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, prcId);
			if (GetProcessImageFileName(hProcess, szProcessName, 32767)) {
				PathStripPath(szProcessName);
				if (conf->FindProfileByApp(string(szProcessName)))
					eve->ScanTaskList();
			}
			CloseHandle(hProcess);
		}
		CloseHandle(hThread);
	}
}

static VOID CALLBACK CForegroundProc(HWINEVENTHOOK hWinEventHook, DWORD dwEvent, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime) {
	eve->CheckProfileWindow(hwnd);
}

LRESULT CALLBACK KeyProc(int nCode, WPARAM wParam, LPARAM lParam) {

	LRESULT res = CallNextHookEx(NULL, nCode, wParam, lParam);

	switch (wParam) {
	case WM_KEYDOWN: case WM_SYSKEYDOWN:
		if (!eve->keyboardSwitchActive) {
			auto pos = find_if(conf->profiles.begin(), conf->profiles.end(),
				[lParam](auto cp) {
					return ((LPKBDLLHOOKSTRUCT)lParam)->vkCode == cp->triggerkey && conf->SamePower(cp->triggerFlags, true);
				});
			if (pos != conf->profiles.end()) {
				eve->SwitchActiveProfile(*pos);
				eve->keyboardSwitchActive = true;
			}
		}
		break;
	case WM_KEYUP: case WM_SYSKEYUP:
		if (eve->keyboardSwitchActive && ((LPKBDLLHOOKSTRUCT)lParam)->vkCode == conf->activeProfile->triggerkey) {
			eve->keyboardSwitchActive = false;
			eve->CheckProfileWindow(GetForegroundWindow());
		}
		break;
	}

	return res;
}

static DWORD WINAPI CEventProc(LPVOID param)
{
	EventHandler* src = (EventHandler*)param;

	// locales block
	HKL* locIDs = new HKL[10];

	LPCTSTR COUNTER_PATH_CPU = "\\Processor Information(_Total)\\% Processor Time",
		COUNTER_PATH_NET = "\\Network Interface(*)\\Bytes Total/sec",
		COUNTER_PATH_NETMAX = "\\Network Interface(*)\\Current BandWidth",
		COUNTER_PATH_GPU = "\\GPU Engine(*)\\Utilization Percentage",
		COUNTER_PATH_HOT = "\\Thermal Zone Information(*)\\Temperature",
		COUNTER_PATH_HOT2 = "\\EsifDeviceInformation(*)\\Temperature",
		COUNTER_PATH_PWR = "\\EsifDeviceInformation(*)\\RAPL Power",
		COUNTER_PATH_HDD = "\\PhysicalDisk(_Total)\\% Idle Time";

	HQUERY hQuery = NULL;
	HCOUNTER hCPUCounter, hHDDCounter, hNETCounter, hNETMAXCounter, hGPUCounter, hTempCounter, hTempCounter2, hPwrCounter;

	MEMORYSTATUSEX memStat{ sizeof(MEMORYSTATUSEX) };

	SYSTEM_POWER_STATUS state;

	EventData cData;

	// Set data source...

	if (PdhOpenQuery(NULL, 0, &hQuery) == ERROR_SUCCESS)
	{

		PdhAddCounter(hQuery, COUNTER_PATH_CPU, 0, &hCPUCounter);
		PdhAddCounter(hQuery, COUNTER_PATH_HDD, 0, &hHDDCounter);
		PdhAddCounter(hQuery, COUNTER_PATH_NET, 0, &hNETCounter);
		PdhAddCounter(hQuery, COUNTER_PATH_NETMAX, 0, &hNETMAXCounter);
		PdhAddCounter(hQuery, COUNTER_PATH_GPU, 0, &hGPUCounter);
		PdhAddCounter(hQuery, COUNTER_PATH_HOT, 0, &hTempCounter);
		PdhAddCounter(hQuery, COUNTER_PATH_HOT2, 0, &hTempCounter2);
		PdhAddCounter(hQuery, COUNTER_PATH_PWR, 0, &hPwrCounter);

		PDH_FMT_COUNTERVALUE cCPUVal, cHDDVal;
		DWORD cType = 0;

		while (WaitForSingleObject(src->stopEvents, conf->monDelay) == WAIT_TIMEOUT) {
			// get indicators...

			if (!fxhl->updateThread)
				continue;

			PdhCollectQueryData(hQuery);

			cData = { 0 };

			PdhGetFormattedCounterValue(hCPUCounter, PDH_FMT_LONG, &cType, &cCPUVal);
			PdhGetFormattedCounterValue(hHDDCounter, PDH_FMT_LONG, &cType, &cHDDVal);

			// Network load
			cData.NET = GetValuesArray(hNETCounter, fxhl->maxData.NET, 0, 1, hNETMAXCounter);

			// GPU load
			cData.GPU = GetValuesArray(hGPUCounter, fxhl->maxData.GPU);

			// Temperatures
			cData.Temp = GetValuesArray(hTempCounter, fxhl->maxData.Temp, 273);

			if (mon) {
				// Check fan RPMs
				for (unsigned i = 0; i < mon->fanRpm.size(); i++) {
					cData.Fan = max(cData.Fan, acpi->GetFanPercent(i));
				}
			}

			// Now other temp sensor block and power block...
			short totalPwr = 0;
			if (conf->esif_temp) {
				if (mon) {
					// Let's get temperatures from fan sensors
					for (unsigned i = 0; i < mon->senValues.size(); i++)
						cData.Temp = max(cData.Temp, mon->senValues[i]);
				}
				else {
					// ESIF temps (already in fans)
					cData.Temp = max(cData.Temp, GetValuesArray(hTempCounter2, fxhl->maxData.Temp));
				}

				// Powers
				cData.PWR = GetValuesArray(hPwrCounter, fxhl->maxData.PWR, 0, 10);
			}

			GlobalMemoryStatusEx(&memStat);
			GetSystemPowerStatus(&state);

			HKL curLocale = GetKeyboardLayout(GetWindowThreadProcessId(GetForegroundWindow(), NULL));

			if (curLocale) {
				for (int i = GetKeyboardLayoutList(10, locIDs); i >= 0; i--) {
					if (curLocale == locIDs[i]) {
						cData.KBD = i > 0 ? 100 : 0;
						break;
					}
				}
			}

			// Leveling...
			cData.Temp = min(100, max(0, cData.Temp));
			cData.Batt = /*state.BatteryLifePercent > 100 ? 0 : */state.BatteryLifePercent;
			cData.HDD = (byte)max(0, 99 - cHDDVal.longValue);
			cData.Fan = min(100, cData.Fan);
			cData.CPU = (byte)cCPUVal.longValue;
			cData.RAM = (byte)memStat.dwMemoryLoad;
			//cData.NET = (byte) totalNet ? max(totalNet * 100 / fxhl->maxData.NET, 1) : 0;
			cData.PWR = (byte)cData.PWR * 100 / fxhl->maxData.PWR;
			//fxhl->maxData.NET = max(fxhl->maxData.NET, cData.NET);
			//fxhl->maxData.GPU = max(fxhl->maxData.GPU, cData.GPU);
			//fxhl->maxData.Temp = max(fxhl->maxData.Temp, cData.Temp);
			fxhl->maxData.RAM = max(fxhl->maxData.RAM, cData.RAM);
			fxhl->maxData.CPU = max(fxhl->maxData.CPU, cData.CPU);

			src->modifyProfile.lock();
			if (src->grid)
				src->grid->UpdateEvent(&cData);
			else
				fxhl->SetCounterColor(&cData);
			src->modifyProfile.unlock();

			/*DebugPrint((string("Counters: Temp=") + to_string(cData.Temp) +
						", Power=" + to_string(cData.PWR) +
						", Max. power=" + to_string(maxPower) + "\n").c_str());*/
		}
		PdhCloseQuery(hQuery);
	}

	delete[] locIDs;

	return 0;
}