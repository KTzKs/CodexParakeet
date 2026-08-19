
// CodexParakeet.cpp : アプリケーションのクラス動作を定義します。
//

#include "pch.h"
#include "framework.h"
#include "CodexParakeet.h"
#include "CodexParakeetDlg.h"
#include "Common.h"
#include <filesystem>
#include <vector>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


// CCodexParakeetApp

BEGIN_MESSAGE_MAP(CCodexParakeetApp, CWinApp)
	ON_COMMAND(ID_HELP, &CWinApp::OnHelp)
END_MESSAGE_MAP()


// CCodexParakeetApp の構築

CCodexParakeetApp::CCodexParakeetApp()
{
	// 再起動マネージャーをサポートします
	m_dwRestartManagerSupportFlags = AFX_RESTART_MANAGER_SUPPORT_RESTART;

	// TODO: この位置に構築用コードを追加してください。
	// ここに InitInstance 中の重要な初期化処理をすべて記述してください。
}


// 唯一の CCodexParakeetApp オブジェクト

CCodexParakeetApp theApp;


// CCodexParakeetApp の初期化

BOOL CCodexParakeetApp::InitInstance()
{
	// 2重起動禁止措置。2回目以降は文章だけ既存プロセスへ渡す。
	TCHAR szWinName[] = APP_NAME;
	HANDLE hMutex = CreateMutex(NULL, FALSE, szWinName);
	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
		HWND hInst = nullptr;
		if ((hInst = ::FindWindow(NULL, APP_NAME)) != NULL)
		{
			if (__argc >= 2)
			{
				COPYDATASTRUCT data{};
				data.dwData = WM_CODEXPARAKEET_TEXT;
				const TCHAR* threadId = __argc >= 3 ? __targv[2] : _T("");
				const TCHAR* spatial = __argc >= 4 ? __targv[3] : _T("");
				const CString decodedMessage = DecodeMessageArgument(__targv[1]);
				std::vector<TCHAR> payload(decodedMessage.GetLength() + _tcslen(threadId) + _tcslen(spatial) + 3);
				_tcscpy_s(payload.data(), payload.size(), decodedMessage.GetString());
				_tcscpy_s(payload.data() + decodedMessage.GetLength() + 1,
					payload.size() - decodedMessage.GetLength() - 1, threadId);
				_tcscpy_s(payload.data() + decodedMessage.GetLength() + _tcslen(threadId) + 2,
					payload.size() - decodedMessage.GetLength() - _tcslen(threadId) - 2, spatial);
				data.cbData = static_cast<DWORD>(payload.size() * sizeof(TCHAR));
				data.lpData = payload.data();
				::SendMessage(hInst, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data));
			}
		}
		CloseHandle(hMutex);
		return FALSE;
	}

	// Windows XP では InitCommonControlsEx() が必要です (以下の場合: アプリケーション
	// ComCtl32.dll Version 6 以降の使用を指定する場合は、
	// Windows XP に InitCommonControlsEx() が必要です。さもなければ、ウィンドウ作成はすべて失敗します。
	INITCOMMONCONTROLSEX InitCtrls;
	InitCtrls.dwSize = sizeof(InitCtrls);
	// アプリケーションで使用するすべてのコモン コントロール クラスを含めるには、
	// これを設定します。
	InitCtrls.dwICC = ICC_WIN95_CLASSES;
	InitCommonControlsEx(&InitCtrls);

	CWinApp::InitInstance();
	wchar_t executablePath[MAX_PATH]{};
	GetModuleFileNameW(nullptr, executablePath, MAX_PATH);
	if (!engine_.Initialize(std::filesystem::path(executablePath).parent_path()))
	{
		AfxMessageBox(CA2T(engine_.Error().c_str()), MB_ICONERROR);
		CloseHandle(hMutex);
		return FALSE;
	}


	AfxEnableControlContainer();

	// ダイアログにシェル ツリー ビューまたはシェル リスト ビュー コントロールが
	// 含まれている場合にシェル マネージャーを作成します。
	CShellManager *pShellManager = new CShellManager;

	// MFC コントロールでテーマを有効にするために、"Windows ネイティブ" のビジュアル マネージャーをアクティブ化
	CMFCVisualManager::SetDefaultManager(RUNTIME_CLASS(CMFCVisualManagerWindows));

	// 標準初期化
	// これらの機能を使わずに最終的な実行可能ファイルの
	// サイズを縮小したい場合は、以下から不要な初期化
	// ルーチンを削除してください。
	// 設定が格納されているレジストリ キーを変更します。
	// TODO: 会社名または組織名などの適切な文字列に
	// この文字列を変更してください。
	SetRegistryKey(_T("アプリケーション ウィザードで生成されたローカル アプリケーション"));

	CCodexParakeetDlg dlg;
	m_pMainWnd = &dlg;
	INT_PTR nResponse = dlg.DoModal();
	if (nResponse == IDOK)
	{
		// TODO: ダイアログが <OK> で消された時のコードを
		//  記述してください。
	}
	else if (nResponse == IDCANCEL)
	{
		// TODO: ダイアログが <OK> で消された時のコードを
		//  記述してください。
	}
	else if (nResponse == -1)
	{
		TRACE(traceAppMsg, 0, "警告: ダイアログの作成に失敗しました。アプリケーションは予期せずに終了します。\n");
		TRACE(traceAppMsg, 0, "警告: ダイアログで MFC コントロールを使用している場合、#define _AFX_NO_MFC_CONTROLS_IN_DIALOGS を指定できません。\n");
	}

	// 上で作成されたシェル マネージャーを削除します。
	if (pShellManager != nullptr)
	{
		delete pShellManager;
	}

#if !defined(_AFXDLL) && !defined(_AFX_NO_MFC_CONTROLS_IN_DIALOGS)
	ControlBarCleanUp();
#endif

	CloseHandle(hMutex);

	// ダイアログは閉じられました。アプリケーションのメッセージ ポンプを開始しないで
	//  アプリケーションを終了するために FALSE を返してください。
	return FALSE;
}

