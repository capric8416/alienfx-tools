#include "alienfx-gui.h"
#include "common.h"

extern groupset* FindMapping(int mid, vector<groupset>* set = conf->active_set);
extern bool SetColor(HWND hDlg, int id, AlienFX_SDK::Colorcode*);
extern void RedrawButton(HWND hDlg, unsigned id, AlienFX_SDK::Colorcode*);

extern int eItem;

void UpdateEffectInfo(HWND hDlg, groupset* mmap) {
	if (mmap) {
		CheckDlgButton(hDlg, IDC_CHECK_SPECTRUM, mmap->flags & GAUGE_GRADIENT);
		CheckDlgButton(hDlg, IDC_CHECK_REVERSE, mmap->flags & GAUGE_REVERSE);
		CheckDlgButton(hDlg, IDC_CHECK_CIRCLE, mmap->effect.flags & GE_FLAG_CIRCLE);
		CheckDlgButton(hDlg, IDC_CHECK_ZONELIGHTS, mmap->effect.flags & GE_FLAG_ZONE);
		ComboBox_SetCurSel(GetDlgItem(hDlg, IDC_COMBO_GAUGE), mmap->gauge);
		ComboBox_SetCurSel(GetDlgItem(hDlg, IDC_COMBO_TRIGGER), mmap->effect.trigger);
		ComboBox_SetCurSel(GetDlgItem(hDlg, IDC_COMBO_GEFFTYPE), mmap->effect.type);
		if (!mmap->effect.size) {
			zonemap* zone = conf->FindZoneMap(mmap->group);
			if (zone)
				mmap->effect.size = max(zone->gMaxX - zone->gMinX, zone->gMaxY - zone->gMinY);
		}
		SendMessage(GetDlgItem(hDlg, IDC_SLIDER_SIZE), TBM_SETPOS, true, mmap->effect.size);
		SendMessage(GetDlgItem(hDlg, IDC_SLIDER_SPEED), TBM_SETPOS, true, mmap->effect.speed - 80);
		SendMessage(GetDlgItem(hDlg, IDC_SLIDER_WIDTH), TBM_SETPOS, true, mmap->effect.width);
		SetSlider(sTip1, mmap->effect.size);
		SetSlider(sTip2, mmap->effect.speed - 80);
		SetSlider(sTip3, mmap->effect.width);
	}
	EnableWindow(GetDlgItem(hDlg, IDC_CHECK_SPECTRUM), mmap && mmap->gauge);
	EnableWindow(GetDlgItem(hDlg, IDC_CHECK_REVERSE), mmap && mmap->gauge);
	RedrawButton(hDlg, IDC_BUTTON_GEFROM, mmap ? &mmap->effect.from : NULL);
	RedrawButton(hDlg, IDC_BUTTON_GETO, mmap ? &mmap->effect.to : NULL);
}

BOOL CALLBACK TabGridDialog(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
	HWND size_slider = GetDlgItem(hDlg, IDC_SLIDER_SIZE),
		speed_slider = GetDlgItem(hDlg, IDC_SLIDER_SPEED),
		width_slider = GetDlgItem(hDlg, IDC_SLIDER_WIDTH);

	groupset* mmap = FindMapping(eItem);

	switch (message)
	{
	case WM_INITDIALOG:
	{
		UpdateCombo(GetDlgItem(hDlg, IDC_COMBO_TRIGGER), { "Off", "Continues", "Random", "Keyboard", "Event"/*, "Haptics"*/});
		UpdateCombo(GetDlgItem(hDlg, IDC_COMBO_GEFFTYPE), { "Running light", "Wave", "Gradient" });
		UpdateCombo(GetDlgItem(hDlg, IDC_COMBO_GAUGE), { "Off", "Horizontal", "Vertical", "Diagonal (left)", "Diagonal (right)", "Radial" });
		SendMessage(size_slider, TBM_SETRANGE, true, MAKELPARAM(1, 80));
		SendMessage(speed_slider, TBM_SETRANGE, true, MAKELPARAM(-80, 80));
		SendMessage(width_slider, TBM_SETRANGE, true, MAKELPARAM(1, 80));
		sTip1 = CreateToolTip(GetDlgItem(hDlg, IDC_SLIDER_SIZE), sTip1);
		sTip2 = CreateToolTip(GetDlgItem(hDlg, IDC_SLIDER_SPEED), sTip2);
		sTip3 = CreateToolTip(GetDlgItem(hDlg, IDC_SLIDER_WIDTH), sTip3);
		UpdateEffectInfo(hDlg, mmap);

	} break;
	case WM_APP + 2: {
		UpdateEffectInfo(hDlg, mmap);
	} break;
	case WM_COMMAND: {
		if (!mmap)
			return false;
		bool state = IsDlgButtonChecked(hDlg, LOWORD(wParam)) == BST_CHECKED;
		switch (LOWORD(wParam))
		{
		case IDC_COMBO_GAUGE:
			if (HIWORD(wParam) == CBN_SELCHANGE) {
				mmap->gauge = ComboBox_GetCurSel(GetDlgItem(hDlg, LOWORD(wParam)));
				EnableWindow(GetDlgItem(hDlg, IDC_CHECK_SPECTRUM), mmap && mmap->gauge);
				EnableWindow(GetDlgItem(hDlg, IDC_CHECK_REVERSE), mmap && mmap->gauge);
				//fxhl->Refresh();
			}
			break;
		case IDC_COMBO_TRIGGER:
			if (HIWORD(wParam) == CBN_SELCHANGE) {
				mmap->effect.trigger = ComboBox_GetCurSel(GetDlgItem(hDlg, LOWORD(wParam)));
				fxhl->Refresh();
			}
			break;
		case IDC_COMBO_GEFFTYPE:
			if (HIWORD(wParam) == CBN_SELCHANGE) {
				mmap->effect.type = ComboBox_GetCurSel(GetDlgItem(hDlg, LOWORD(wParam)));
			}
			break;
		case IDC_CHECK_SPECTRUM:
			SetBitMask(mmap->flags, GAUGE_GRADIENT, state);
			break;
		case IDC_CHECK_REVERSE:
			SetBitMask(mmap->flags, GAUGE_REVERSE, state);
			break;
		case IDC_CHECK_CIRCLE:
			SetBitMask(mmap->effect.flags, GE_FLAG_CIRCLE, state);
			break;
		case IDC_CHECK_ZONELIGHTS:
			SetBitMask(mmap->effect.flags, GE_FLAG_ZONE, state);
			break;
		case IDC_BUTTON_GEFROM:
			SetColor(hDlg, LOWORD(wParam), &mmap->effect.from);
			break;
		case IDC_BUTTON_GETO:
			SetColor(hDlg, LOWORD(wParam), &mmap->effect.to);
			break;
		}
	} break;
	case WM_HSCROLL:
		switch (LOWORD(wParam)) {
		case TB_THUMBTRACK: case TB_ENDTRACK:
			if (mmap) {
				if ((HWND)lParam == speed_slider) {
					mmap->effect.speed = (BYTE)SendMessage((HWND)lParam, TBM_GETPOS, 0, 0) + 80;
					SetSlider(sTip2, mmap->effect.speed - 80);
				}
				if ((HWND)lParam == size_slider) {
					mmap->effect.size = (BYTE)SendMessage((HWND)lParam, TBM_GETPOS, 0, 0);
					SetSlider(sTip1, mmap->effect.size);
				}
				if ((HWND)lParam == width_slider) {
					mmap->effect.width = (BYTE)SendMessage((HWND)lParam, TBM_GETPOS, 0, 0);
					SetSlider(sTip3, mmap->effect.width);
				}
			}
		} break;
	case WM_DRAWITEM:
		if (mmap) {
			AlienFX_SDK::Colorcode* c{ NULL };
			switch (((DRAWITEMSTRUCT*)lParam)->CtlID) {
			case IDC_BUTTON_GEFROM:
				c = &mmap->effect.from;
				break;
			case IDC_BUTTON_GETO:
				c = &mmap->effect.to;
				break;
			}
			RedrawButton(hDlg, ((DRAWITEMSTRUCT*)lParam)->CtlID, c);
		}
	    break;
	default: return false;
	}
	return true;
}