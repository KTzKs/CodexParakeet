#define APP_NAME           _T("CodexParakeet")
#pragma once

#define APP_NAME _T("CodexParakeet")
#define WM_CODEXPARAKEET_TEXT (WM_APP + 100)
#define WM_TASKTRAY (WM_APP + 101)

//#define OUTPUT_LOG

CString DecodeMessageArgument(const TCHAR* argument);
