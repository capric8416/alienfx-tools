#include "alienfx-gui.h"

extern int eItem;
extern groupset* FindMapping(int mid, vector<groupset>* set = conf->active_set);

HWND zsDlg;

void UpdateZoneList() {
	int rpos = -1, pos = 0;
	HWND zone_list = GetDlgItem(zsDlg, IDC_LIST_ZONES);
	LVITEMA lItem{ LVIF_TEXT | LVIF_PARAM | LVIF_STATE };
	ListView_DeleteAllItems(zone_list);
	ListView_SetExtendedListViewStyle(zone_list, LVS_EX_FULLROWSELECT | LVS_EX_LABELTIP);
	if (!ListView_GetColumnWidth(zone_list, 0)) {
		LVCOLUMNA lCol{ /*LVCF_TEXT |*/ LVCF_FMT, LVCFMT_LEFT };
		ListView_InsertColumn(zone_list, 0, &lCol);
		lCol.fmt = LVCFMT_RIGHT;
		ListView_InsertColumn(zone_list, 1, &lCol);
	}
	for (auto i = conf->activeProfile->lightsets.begin(); i < conf->activeProfile->lightsets.end(); i++) {
		AlienFX_SDK::group* grp = conf->afx_dev.GetGroupById(i->group);
		string name = "(" + to_string(grp->lights.size()) + ")";
		lItem.iItem = pos;
		lItem.lParam = i->group;
		lItem.pszText = (LPSTR)grp->name.c_str();
		if (grp->gid == eItem) {
			lItem.state = LVIS_SELECTED;
			rpos = pos;
		}
		else
			lItem.state = 0;
		ListView_InsertItem(zone_list, &lItem);
		ListView_SetItemText(zone_list, pos, 1, (LPSTR)name.c_str());
		pos++;
	}
	RECT cArea;
	GetClientRect(zone_list, &cArea);
	ListView_SetColumnWidth(zone_list, 1, LVSCW_AUTOSIZE);
	ListView_SetColumnWidth(zone_list, 0, cArea.right - ListView_GetColumnWidth(zone_list, 1));
	ListView_EnsureVisible(zone_list, rpos, false);
}

BOOL CALLBACK AddZoneDialog(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
	HWND grouplist = GetDlgItem(hDlg, IDC_LIST_GROUPS);
	switch (message)
	{
	case WM_INITDIALOG:
	{
		unsigned maxGrpID = 0x10000;
		while (conf->afx_dev.GetGroupById(maxGrpID))
			maxGrpID++;
		ListBox_SetItemData(grouplist, ListBox_AddString(grouplist, "<New Zone>"), maxGrpID);
		for (auto t = conf->afx_dev.GetGroups()->begin(); t < conf->afx_dev.GetGroups()->end(); t++) {
			if (!FindMapping(t->gid))
				ListBox_SetItemData(grouplist, ListBox_AddString(grouplist, t->name.c_str()), t->gid);
		}
		if (ListBox_GetCount(grouplist) == 1) {
			// No groups in list, exit
			AlienFX_SDK::group* grp = new AlienFX_SDK::group({ (DWORD)maxGrpID, "New zone #" + to_string((maxGrpID & 0xffff) + 1) });
			conf->afx_dev.GetGroups()->push_back(*grp);
			eItem = grp->gid;
			conf->activeProfile->lightsets.push_back({ eItem });
			EndDialog(hDlg, IDOK);
		} else
		{
			RECT pRect;
			GetWindowRect(zsDlg, &pRect);
			SetWindowPos(hDlg, NULL, pRect.left, pRect.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
		}
	}
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDCLOSE: case IDCANCEL: EndDialog(hDlg, IDCLOSE); break;
		case IDC_LIST_GROUPS:
			switch (HIWORD(wParam)) {
			case LBN_SELCHANGE:
			{
				eItem = (int)ListBox_GetItemData(grouplist, ListBox_GetCurSel(grouplist));
				if (!conf->afx_dev.GetGroupById(eItem))
					conf->afx_dev.GetGroups()->push_back({ (DWORD)eItem, "New zone #" + to_string((eItem & 0xffff) + 1) });
				conf->activeProfile->lightsets.push_back({ eItem });
				EndDialog(hDlg, IDOK);
			} break;
			}
		}
		break;
	default: return false;
	}
	return true;
}

BOOL CALLBACK ZoneSelectionDialog(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message)
	{
	case WM_INITDIALOG:
	{
		zsDlg = hDlg;
		if (eItem < 0 && conf->activeProfile->lightsets.size())
			eItem = conf->activeProfile->lightsets.front().group;
		UpdateZoneList();
		//return false;
	} break;
	case WM_COMMAND: {
		switch (LOWORD(wParam))
		{
		case IDC_BUT_ADD_ZONE: {
			DialogBox(hInst, MAKEINTRESOURCE(IDD_DIALOG_ADDGROUP), hDlg, (DLGPROC)AddZoneDialog);
			UpdateZoneList();
			//SendMessage(GetParent(hDlg), WM_APP + 2, 0, 1);
		} break;
		case IDC_BUT_DEL_ZONE:
			if (eItem > 0) {
				bool doDelete = true;
				int neItem = eItem;
				// delete from all profiles...
				for (auto Iter = conf->profiles.begin(); Iter != conf->profiles.end(); Iter++) {
					auto pos = find_if((*Iter)->lightsets.begin(), (*Iter)->lightsets.end(),
						[](auto t) {
							return t.group == eItem;
						});
					if (pos != (*Iter)->lightsets.end())
						if ((*Iter)->id != conf->activeProfile->id)
							doDelete = false;
						else {
							if (pos != (*Iter)->lightsets.begin())
								neItem = (pos - 1)->group;
							else
								if ((*Iter)->lightsets.size() > 1)
									neItem = (pos + 1)->group;
								else
									neItem = -1;
							(*Iter)->lightsets.erase(pos);
						}
				}
				if (doDelete) {
					auto Iter = find_if(conf->afx_dev.GetGroups()->begin(), conf->afx_dev.GetGroups()->end(),
						[](auto t) {
							return t.gid == eItem;
						});
					if (Iter != conf->afx_dev.GetGroups()->end())
						conf->afx_dev.GetGroups()->erase(Iter);
				}
				eItem = neItem;
				UpdateZoneList();
				if (eItem < 0)
					SendMessage(GetParent(hDlg), WM_APP + 2, 0, 1);
			}
			break;
		}
	} break;
	case WM_NOTIFY:
		switch (((NMHDR*)lParam)->idFrom) {
		case IDC_LIST_ZONES:
			switch (((NMHDR*)lParam)->code) {
			case LVN_ITEMACTIVATE: {
				NMITEMACTIVATE* item = (NMITEMACTIVATE*)lParam;
				ListView_EditLabel(((NMHDR*)lParam)->hwndFrom, ((NMITEMACTIVATE*)lParam)->iItem);
			} break;

			case LVN_ITEMCHANGED:
			{
				NMLISTVIEW* lPoint = (LPNMLISTVIEW)lParam;
				if (lPoint->uNewState & LVIS_SELECTED) {
					// Select other item...
					eItem = (int)lPoint->lParam;
					SendMessage(GetParent(hDlg), WM_APP + 2, 0, 1);
				}
				else {
					if (!lPoint->uNewState && ListView_GetItemState(((NMHDR*)lParam)->hwndFrom, lPoint->iItem, LVIS_FOCUSED)) {
						ListView_SetItemState(((NMHDR*)lParam)->hwndFrom, lPoint->iItem, LVIS_SELECTED, LVIS_SELECTED);
					}
					else
						ListView_SetItemState(((NMHDR*)lParam)->hwndFrom, lPoint->iItem, 0, LVIS_SELECTED);
				}
			} break;
			case LVN_ENDLABELEDIT:
			{
				NMLVDISPINFO* sItem = (NMLVDISPINFO*)lParam;
				AlienFX_SDK::group* grp = conf->afx_dev.GetGroupById((int)sItem->item.lParam);
				if (grp && sItem->item.pszText) {
					grp->name = sItem->item.pszText;
					ListView_SetItem(((NMHDR*)lParam)->hwndFrom, &sItem->item);
					RECT cArea;
					GetClientRect(((NMHDR*)lParam)->hwndFrom, &cArea);
					ListView_SetColumnWidth(((NMHDR*)lParam)->hwndFrom, 1, LVSCW_AUTOSIZE);
					ListView_SetColumnWidth(((NMHDR*)lParam)->hwndFrom, 0, cArea.right - ListView_GetColumnWidth(((NMHDR*)lParam)->hwndFrom, 1));
					return true;
				}
				else
					return false;
			} break;
			}
			break;
		}
		break;
	default: return false;
	}
	return true;
}