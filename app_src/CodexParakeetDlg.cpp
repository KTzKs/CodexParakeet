
// CodexParakeetDlg.cpp : 実装ファイル
//

#include "pch.h"
#include "framework.h"
#include "CodexParakeet.h"
#include "CodexParakeetDlg.h"
#include "afxdialogex.h"
#include "Common.h"
#include <shellapi.h>
#include <vector>
#include <filesystem>
#include <array>
#include <chrono>
#include <iomanip>
#include <atlimage.h>

#pragma comment(lib, "shell32.lib")

#define SIZE1_WIDTH 280
#define SIZE1_HEIGHT 220

#define SIZE2_WIDTH 740
#define SIZE2_HEIGHT 300

static void WriteSpeakLog(const std::wstring& message)
{
	UNREFERENCED_PARAMETER(message);
}

static std::wstring ToWide(const std::string& text)
{
	if (text.empty()) return {};
	const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(),
		static_cast<int>(text.size()), nullptr, 0);
	if (length <= 0) return {};
	std::wstring result(static_cast<size_t>(length), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
		result.data(), length);
	return result;
}

static std::vector<std::wstring> SplitSpeechLines(const CString& message)
{
	std::vector<std::wstring> lines;
	bool inCodeBlock = false;
	int start = 0;
	while (start < message.GetLength())
	{
		int cr = message.Find(_T('\r'), start);
		int lf = message.Find(_T('\n'), start);
		int end = message.GetLength();
		if (cr >= 0 && cr < end) end = cr;
		if (lf >= 0 && lf < end) end = lf;
		CString line = message.Mid(start, end - start);
		line.Trim();
		// Markdownのコードブロックは表示側では整形されるが、音声合成に
		// コード本体を渡すと記号だらけの入力になり、途中で合成が崩れる。
		// 行頭が ``` の行を開始・終了フェンスとして扱い、その間を丸ごと除外する。
		if (line.Left(3) == _T("```"))
		{
			inCodeBlock = !inCodeBlock;
			if (end == message.GetLength()) break;
			start = end + 1;
			while (start < message.GetLength() && (message[start] == _T('\r') || message[start] == _T('\n'))) ++start;
			continue;
		}
		if (inCodeBlock)
		{
			if (end == message.GetLength()) break;
			start = end + 1;
			while (start < message.GetLength() && (message[start] == _T('\r') || message[start] == _T('\n'))) ++start;
			continue;
		}
		if (!line.IsEmpty())
		{
			lines.emplace_back(line.GetString());
		}
		if (end == message.GetLength()) break;
		start = end + 1;
		while (start < message.GetLength() && (message[start] == _T('\r') || message[start] == _T('\n'))) ++start;
	}
	return lines;
}

static std::vector<std::wstring> SelectFirstSentence(const std::vector<std::wstring>& lines)
{
	if (lines.empty()) return {};

	std::wstring sentence;
	for (const auto& line : lines)
	{
		if (!sentence.empty()) sentence += L' ';
		sentence += line;

		const auto end = sentence.find_first_of(L"。！？.!?");
		if (end != std::wstring::npos)
		{
			sentence.resize(end + 1);
			break;
		}
	}

	return sentence.empty() ? std::vector<std::wstring>{} : std::vector<std::wstring>{sentence};
}

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


// CCodexParakeetDlg ダイアログ
CCodexParakeetDlg::CCodexParakeetDlg(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_CODEXPARAKEET_DIALOG, pParent)
	, m_hIcon()
	, m_CheckEnableTTS()
	, m_EditSokudo()
	, m_EditMuon()
	, threadVoices_()
	, nextVoice_()
	, initialMessage_()
	, initialThreadId_()
	, initialSpatial_()
	, m_ParakeetArea()
	, mouthBitmaps_()
	, lipBackBufferDc_()
	, lipBackBufferBitmap_()
	, lipBackBufferSize_()
	, m_CheckOnzo()
	, m_SettingButtonState(false)
	, m_OnlyFirstSentence()
	, m_CheckAllSentence()
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

void CCodexParakeetDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_CHECK1, m_CheckEnableTTS);
	DDX_Control(pDX, IDC_EDIT1, m_EditSokudo);
	DDX_Control(pDX, IDC_EDIT2, m_EditMuon);
	DDX_Control(pDX, IDC_PARAKEETAREA, m_ParakeetArea);
	DDX_Control(pDX, IDC_CHECK2, m_CheckOnzo);
	DDX_Control(pDX, IDC_RADIO1, m_OnlyFirstSentence);
	DDX_Control(pDX, IDC_RADIO2, m_CheckAllSentence);
}

BEGIN_MESSAGE_MAP(CCodexParakeetDlg, CDialogEx)
	ON_WM_COPYDATA()
	ON_MESSAGE(WM_TASKTRAY, &CCodexParakeetDlg::OnTrayMessage)
	ON_MESSAGE(WM_APP + 102, &CCodexParakeetDlg::OnSpeakInitialMessage)
	ON_MESSAGE(WM_APP + 103, &CCodexParakeetDlg::OnLipSync)
	ON_WM_DESTROY()
	ON_WM_SIZE()
	ON_WM_TIMER()
	ON_WM_PAINT()
	ON_WM_DRAWITEM()
	ON_WM_QUERYDRAGICON()
	ON_BN_CLICKED(IDOK, &CCodexParakeetDlg::OnBnClickedOk)
	ON_BN_CLICKED(IDCANCEL, &CCodexParakeetDlg::OnBnClickedCancel)
	ON_BN_CLICKED(IDC_CHECK1, &CCodexParakeetDlg::OnBnClickedCheckEnableTts)
	ON_BN_CLICKED(IDC_MFCBUTTON1, &CCodexParakeetDlg::OnBnClickedMfcbutton1)
END_MESSAGE_MAP()


// CCodexParakeetDlg メッセージ ハンドラー

BOOL CCodexParakeetDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();
	WriteSpeakLog(L"OnInitDialog begin");

	// このダイアログのアイコンを設定します。アプリケーションのメイン ウィンドウがダイアログでない場合、
	//  Framework は、この設定を自動的に行います。
//	SetIcon(m_hIcon, TRUE);			// 大きいアイコンの設定
//	SetIcon(m_hIcon, FALSE);		// 小さいアイコンの設定

	HICON hSmallIcon = (HICON)::LoadImage(
		AfxGetInstanceHandle(),
		MAKEINTRESOURCE(IDR_MAINFRAME),
		IMAGE_ICON,
		16, 16,
		LR_DEFAULTCOLOR
	);
	SetIcon(hSmallIcon, FALSE);

	HICON hBigIcon = (HICON)::LoadImage(
		AfxGetInstanceHandle(),
		MAKEINTRESOURCE(IDR_MAINFRAME),
		IMAGE_ICON,
		32, 32,
		LR_DEFAULTCOLOR
	);
	SetIcon(hBigIcon, TRUE);

	// TODO: 初期化をここに追加します。
	SetWindowText(APP_NAME);  // ウィンドウタイトルを設定
	AddTrayIcon();
	SetTimer(1, 1000, nullptr);
	LoadVoiceSettings();
	ApplyVoiceSettings();
	LoadMouthBitmaps();
	theApp.Engine().SetLipSyncCallback([this](wchar_t mouth) {
		if (m_hWnd) PostMessage(WM_APP + 103, static_cast<WPARAM>(mouth), 0);
	});

	// 初回起動がHookからの呼び出しだった場合、その引数も読み上げる。
	// OnInitDialog中に直接合成すると、ウィンドウの初期化と音声スレッドの
	// 起動が競合して初回メッセージだけ落ちることがあるため、ポストしてから処理する。
	if (__argc >= 2 && __targv[1] != nullptr && __targv[1][0] != _T('\0'))
	{
		initialMessage_ = __targv[1];
		initialThreadId_ = __argc >= 3 ? __targv[2] : _T("");
		initialSpatial_ = __argc >= 4 ? __targv[3] : _T("");
		WriteSpeakLog(L"initial message received; length=" + std::to_wstring(initialMessage_.GetLength()) +
			L", threadId length=" + std::to_wstring(initialThreadId_.GetLength()));
		PostMessage(WM_APP + 102);
		WriteSpeakLog(L"initial speech message posted");
	}
	else
	{
		WriteSpeakLog(L"no initial command-line message; argc=" + std::to_wstring(__argc));
	}
//	ShowWindow(SW_HIDE);

	SetWindowPos(NULL, 0, 0, SIZE1_WIDTH, SIZE1_HEIGHT, SWP_NOMOVE | SWP_NOZORDER);

	return TRUE;  // フォーカスをコントロールに設定した場合を除き、TRUE を返します。
}

void CCodexParakeetDlg::LoadMouthBitmaps()
{
	ClearMouthBitmaps();
	wchar_t exe[MAX_PATH]{};
	GetModuleFileNameW(nullptr, exe, MAX_PATH);
	const auto dir = std::filesystem::path(exe).parent_path() / L"lipsync";
	const auto& imageDir = dir;
	const std::wstring names = L"aieou";
	for (wchar_t mouth : names + std::wstring(L"c")) {
		const auto path = imageDir / (std::wstring(L"mouth_") + mouth + L".png");
		CImage image;
		if (FAILED(image.Load(path.c_str()))) continue;
		mouthBitmaps_[mouth] = image.Detach();
	}
	const auto closed = imageDir / L"mouth_closed.png";
	CImage image;
	if (SUCCEEDED(image.Load(closed.c_str()))) mouthBitmaps_[L'c'] = image.Detach();
	if (mouthBitmaps_.count(L'c')) {
		m_ParakeetArea.ModifyStyle(SS_BITMAP, SS_OWNERDRAW);
		::SetPropW(m_ParakeetArea.GetSafeHwnd(), L"CodexParakeet.Mouth",
			reinterpret_cast<HANDLE>(mouthBitmaps_[L'c']));
		m_ParakeetArea.Invalidate();
	}
}

void CCodexParakeetDlg::ClearMouthBitmaps()
{
	if (m_ParakeetArea.GetSafeHwnd()) {
		::RemovePropW(m_ParakeetArea.GetSafeHwnd(), L"CodexParakeet.Mouth");
	}
	for (auto& [mouth, bitmap] : mouthBitmaps_) if (bitmap) DeleteObject(bitmap);
	mouthBitmaps_.clear();
	lipBackBufferDc_.DeleteDC();
	lipBackBufferBitmap_.DeleteObject();
	lipBackBufferSize_ = CSize(0, 0);
}

void CCodexParakeetDlg::ResetLipBackBuffer(const CSize& size, CDC& referenceDc)
{
	lipBackBufferDc_.DeleteDC();
	lipBackBufferBitmap_.DeleteObject();
	lipBackBufferDc_.CreateCompatibleDC(&referenceDc);
	lipBackBufferBitmap_.CreateCompatibleBitmap(&referenceDc, max(1, size.cx), max(1, size.cy));
	lipBackBufferDc_.SelectObject(&lipBackBufferBitmap_);
	lipBackBufferSize_ = size;
}

LRESULT CCodexParakeetDlg::OnLipSync(WPARAM wParam, LPARAM)
{
	const wchar_t mouth = static_cast<wchar_t>(wParam);
	const auto it = mouthBitmaps_.find(mouth);
	if (it != mouthBitmaps_.end()) {
		::SetPropW(m_ParakeetArea.GetSafeHwnd(), L"CodexParakeet.Mouth",
			reinterpret_cast<HANDLE>(it->second));
		m_ParakeetArea.Invalidate();
	}
	return 0;
}

void CCodexParakeetDlg::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT draw)
{
	if (nIDCtl != IDC_PARAKEETAREA || draw == nullptr) {
		CDialogEx::OnDrawItem(nIDCtl, draw);
		return;
	}
	CDC dc;
	dc.Attach(draw->hDC);
	CRect target(draw->rcItem);
	const CSize targetSize(max(1, target.Width()), max(1, target.Height()));
	if (!lipBackBufferDc_.GetSafeHdc() || lipBackBufferSize_ != targetSize) {
		ResetLipBackBuffer(targetSize, dc);
	}
	// 背景は塗りつぶさず、現在の表示を裏バッファへ引き継ぐ。
	lipBackBufferDc_.BitBlt(0, 0, target.Width(), target.Height(), &dc,
		target.left, target.top, SRCCOPY);
	HBITMAP bitmap = nullptr;
	// 現在の口形は CStatic のウィンドウプロパティに保持する。
	bitmap = reinterpret_cast<HBITMAP>(::GetPropW(m_ParakeetArea.GetSafeHwnd(), L"CodexParakeet.Mouth"));
	if (bitmap) {
		BITMAP info{};
		GetObject(bitmap, sizeof(info), &info);
		CDC source;
		source.CreateCompatibleDC(&lipBackBufferDc_);
		HGDIOBJ old = source.SelectObject(bitmap);
		const double scale = min(
			double(target.Width()) / max(1L, info.bmWidth),
			double(target.Height()) / max(1L, info.bmHeight));
		const int width = max(1, static_cast<int>(info.bmWidth * scale));
		const int height = max(1, static_cast<int>(info.bmHeight * scale));
		lipBackBufferDc_.SetStretchBltMode(HALFTONE);
		lipBackBufferDc_.StretchBlt((target.Width() - width) / 2,
			(target.Height() - height) / 2, width, height,
			&source, 0, 0, info.bmWidth, info.bmHeight, SRCCOPY);
		source.SelectObject(old);
	}
	dc.BitBlt(target.left, target.top, target.Width(), target.Height(),
		&lipBackBufferDc_, 0, 0, SRCCOPY);
	dc.Detach();
}

LRESULT CCodexParakeetDlg::OnSpeakInitialMessage(WPARAM, LPARAM)
{
	WriteSpeakLog(L"OnSpeakInitialMessage entered");
	if (initialMessage_.IsEmpty())
	{
		WriteSpeakLog(L"initial message is empty");
		return 0;
	}

	CString message = initialMessage_;
	CString threadId = initialThreadId_;
	CString spatial = initialSpatial_;
	initialMessage_.Empty();
	initialThreadId_.Empty();
	initialSpatial_.Empty();
	WriteSpeakLog(L"initial message dequeued; length=" + std::to_wstring(message.GetLength()));
	if (m_CheckEnableTTS.GetCheck() == BST_CHECKED)
	{
		WriteSpeakLog(L"TTS enabled; calling SpeakMessage");
		SpeakMessage(message, threadId, spatial);
	}
	else
	{
		WriteSpeakLog(L"TTS disabled; initial message skipped");
	}
	if (!theApp.Engine().Error().empty())
	{
		const std::wstring engineError = ToWide(theApp.Engine().Error());
		WriteSpeakLog(L"engine error: " + engineError);
	}
	else
	{
		WriteSpeakLog(L"initial SpeakMessage completed without engine error");
	}
	return 0;
}

void CCodexParakeetDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == 1)
	{
		KillTimer(1);
		ShowWindow(SW_HIDE);
	}
	CDialogEx::OnTimer(nIDEvent);
}

void CCodexParakeetDlg::OnSize(UINT nType, int cx, int cy)
{
	CDialogEx::OnSize(nType, cx, cy);
	if (nType == SIZE_MINIMIZED)
	{
		ShowWindow(SW_HIDE);
	}
}

void CCodexParakeetDlg::AddTrayIcon()
{
	// ★ 16x16 の小アイコンを明示的に読み込む
	HICON hTrayIcon = (HICON)::LoadImage(
		AfxGetInstanceHandle(),
		MAKEINTRESOURCE(IDR_MAINFRAME),
		IMAGE_ICON,
		16, 16,                // ← トレイ用のサイズを指定
		LR_DEFAULTCOLOR
	);

	NOTIFYICONDATA nid{};
	nid.cbSize = sizeof(nid);
	nid.hWnd = m_hWnd;
	nid.uID = 1;
	nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
	nid.uCallbackMessage = WM_TASKTRAY;
	nid.hIcon = hTrayIcon;     // ★ ここを m_hIcon から差し替え
	_tcscpy_s(nid.szTip, _countof(nid.szTip), APP_NAME);

	Shell_NotifyIcon(NIM_ADD, &nid);
}

void CCodexParakeetDlg::RemoveTrayIcon()
{
	NOTIFYICONDATA nid{};
	nid.cbSize = sizeof(nid);
	nid.hWnd = m_hWnd;
	nid.uID = 1;
	Shell_NotifyIcon(NIM_DELETE, &nid);
}

LRESULT CCodexParakeetDlg::OnTrayMessage(WPARAM, LPARAM lParam)
{
	if (lParam == WM_LBUTTONUP)
	{
		ShowWindow(IsWindowVisible() ? SW_HIDE : SW_SHOWNORMAL);
		if (IsWindowVisible()) SetForegroundWindow();
	}
	return 0;
}

void CCodexParakeetDlg::OnDestroy()
{
	theApp.Engine().SetLipSyncCallback(nullptr);
	ClearMouthBitmaps();
	SaveVoiceSettings();
	RemoveTrayIcon();
	CDialogEx::OnDestroy();
}

static std::filesystem::path CodexParakeetIniPath()
{
	wchar_t path[MAX_PATH]{};
	GetModuleFileNameW(nullptr, path, MAX_PATH);
	return std::filesystem::path(path).parent_path() / L"CodexParakeet.ini";
}

void CCodexParakeetDlg::LoadVoiceSettings()
{
	const auto ini = CodexParakeetIniPath();
	wchar_t speed[64]{}, silence[64]{}, enableTTS[16]{};
	wchar_t enableOnzo[16]{};
	wchar_t enableFirst[16]{};
	GetPrivateProfileStringW(L"Voice", L"Speed", L"1.4", speed, _countof(speed), ini.c_str());
	GetPrivateProfileStringW(L"Voice", L"EndSilenceMs", L"100", silence, _countof(silence), ini.c_str());
	// iniが存在しない初回起動では、Codex読上げを有効にする。
	const wchar_t* defaultEnableTTS = L"1";
	GetPrivateProfileStringW(L"Voice", L"EnableTTS", defaultEnableTTS,
		enableTTS, _countof(enableTTS), ini.c_str());
	GetPrivateProfileStringW(L"Voice", L"EnableOnzo", L"1",
		enableOnzo, _countof(enableOnzo), ini.c_str());
	GetPrivateProfileStringW(L"Voice", L"EnableFirstSentense", L"1",
		enableFirst, _countof(enableFirst), ini.c_str());
	m_EditSokudo.SetWindowTextW(speed);
	m_EditMuon.SetWindowTextW(silence);
	m_CheckEnableTTS.SetCheck(_wtol(enableTTS) != 0 ? BST_CHECKED : BST_UNCHECKED);
	m_CheckOnzo.SetCheck(_wtol(enableOnzo) != 0 ? BST_CHECKED : BST_UNCHECKED);
	if (_wtol(enableFirst) != 0)
	{
		m_OnlyFirstSentence.SetCheck(BST_CHECKED);
		m_CheckAllSentence.SetCheck(BST_UNCHECKED);

	}
	else
	{
		m_OnlyFirstSentence.SetCheck(BST_UNCHECKED);
		m_CheckAllSentence.SetCheck(BST_CHECKED);
	}
}

void CCodexParakeetDlg::SaveVoiceSettings()
{
	CString speed, silence;
	m_EditSokudo.GetWindowText(speed);
	m_EditMuon.GetWindowText(silence);
	const auto ini = CodexParakeetIniPath();
	WritePrivateProfileStringW(L"Voice", L"Speed", speed, ini.c_str());
	WritePrivateProfileStringW(L"Voice", L"EndSilenceMs", silence, ini.c_str());
	WritePrivateProfileStringW(L"Voice", L"EnableTTS",
		m_CheckEnableTTS.GetCheck() == BST_CHECKED ? L"1" : L"0", ini.c_str());
	WritePrivateProfileStringW(L"Voice", L"EnableOnzo",
		m_CheckOnzo.GetCheck() == BST_CHECKED ? L"1" : L"0", ini.c_str());
	WritePrivateProfileStringW(L"Voice", L"EnableFirstSentense",
		m_OnlyFirstSentence.GetCheck() == BST_CHECKED ? L"1" : L"0", ini.c_str());
}

void CCodexParakeetDlg::ApplyVoiceSettings()
{
	CString speed, silence;
	m_EditSokudo.GetWindowText(speed);
	m_EditMuon.GetWindowText(silence);
	theApp.Engine().SetVoiceSettings(_wtof(speed), static_cast<DWORD>(_wtol(silence)));
}

BOOL CCodexParakeetDlg::OnCopyData(CWnd* pWnd, COPYDATASTRUCT* pCopyDataStruct)
{
	if (pCopyDataStruct == nullptr ||
		pCopyDataStruct->dwData != WM_CODEXPARAKEET_TEXT ||
		pCopyDataStruct->lpData == nullptr)
	{
		return CDialogEx::OnCopyData(pWnd, pCopyDataStruct);
	}

	const auto* text = static_cast<const TCHAR*>(pCopyDataStruct->lpData);
	const size_t charCount = pCopyDataStruct->cbData / sizeof(TCHAR);
	CString message(text);
	const size_t messageLength = message.GetLength();
	const size_t threadOffset = messageLength + 1;
	const size_t threadLength = threadOffset < charCount ? _tcslen(text + threadOffset) : 0;
	const size_t spatialOffset = threadOffset + threadLength + 1;
	CString threadId = threadOffset < charCount
		? CString(text + threadOffset, static_cast<int>(threadLength)) : CString();
	CString spatial = spatialOffset < charCount
		? CString(text + spatialOffset, static_cast<int>(_tcslen(text + spatialOffset))) : CString();
	message.TrimRight(L'\0');
	if (m_CheckEnableTTS.GetCheck() != BST_CHECKED)
	{
		return TRUE;
	}

	SpeakMessage(message, threadId, spatial);
	if (!theApp.Engine().Error().empty())
	{
	}

	return TRUE;
}

void CCodexParakeetDlg::SpeakMessage(const CString& message, const CString& threadId, const CString& spatial)
{
	WriteSpeakLog(L"SpeakMessage begin; message length=" + std::to_wstring(message.GetLength()) +
		L", threadId length=" + std::to_wstring(threadId.GetLength()));
	static const wchar_t* models[] = { L"8.vvm", L"0.vvm", L"0.vvm" };
	// 各VVMに含まれるスタイルIDを使用する。別モデルのIDを渡すと
	// VOICEVOXは「指定されたIDに対するスタイルが見つからない」として
	// audio queryを生成できない。
	static const uint32_t speakers[] = { 23, 8, 3 };
	const std::wstring id(threadId.GetString());
	auto it = threadVoices_.find(id);
	if (it == threadVoices_.end()) {
		it = threadVoices_.emplace(id, nextVoice_++ % 3).first;
	}
	wchar_t executablePath[MAX_PATH]{};
	GetModuleFileNameW(nullptr, executablePath, MAX_PATH);
	const auto modelPath = std::filesystem::path(executablePath).parent_path() / L"voicevox_core" / L"models" / L"vvms" / models[it->second];
	theApp.Engine().SetVoiceModel(modelPath);
	theApp.Engine().SetSpeaker(speakers[it->second]);
	if (m_CheckOnzo.GetCheck() == BST_CHECKED)
	{
		int comma = spatial.Find(_T(','));
		if (comma > 0) theApp.Engine().SetSpatialPosition(_ttoi(spatial.Left(comma)), _ttoi(spatial.Mid(comma + 1)));
		else theApp.Engine().ClearSpatialPosition();
	}
	else
	{
		theApp.Engine().ClearSpatialPosition();
	}
	ApplyVoiceSettings();
	auto lines = SplitSpeechLines(message);
	if (m_OnlyFirstSentence.GetCheck() == BST_CHECKED)
	{
		lines = SelectFirstSentence(lines);
	}
	WriteSpeakLog(L"SpeakMessage queueing lines=" + std::to_wstring(lines.size()));
	theApp.Engine().SpeakLines(lines, std::wstring(threadId.GetString()));
	WriteSpeakLog(L"SpeakMessage end");
}

// ダイアログに最小化ボタンを追加する場合、アイコンを描画するための
//  下のコードが必要です。ドキュメント/ビュー モデルを使う MFC アプリケーションの場合、
//  これは、Framework によって自動的に設定されます。

void CCodexParakeetDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this); // 描画のデバイス コンテキスト

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		// クライアントの四角形領域内の中央
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// アイコンの描画
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialogEx::OnPaint();
	}
}

// ユーザーが最小化したウィンドウをドラッグしているときに表示するカーソルを取得するために、
//  システムがこの関数を呼び出します。
HCURSOR CCodexParakeetDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}


void CCodexParakeetDlg::OnBnClickedOk()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	CDialogEx::OnOK();
}

void CCodexParakeetDlg::OnBnClickedCancel()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	CDialogEx::OnCancel();
}

void CCodexParakeetDlg::OnBnClickedCheckEnableTts()
{
	SaveVoiceSettings();
	if (m_CheckEnableTTS.GetCheck() != BST_CHECKED)
	{
		theApp.Engine().Cancel();
	}
}

void CCodexParakeetDlg::OnBnClickedMfcbutton1()
{
	if (m_SettingButtonState == false)
	{
		SetWindowPos(NULL, 0, 0, SIZE2_WIDTH, SIZE2_HEIGHT, SWP_NOMOVE | SWP_NOZORDER);
		m_SettingButtonState = true;
	}
	else
	{
		SetWindowPos(NULL, 0, 0, SIZE1_WIDTH, SIZE1_HEIGHT, SWP_NOMOVE | SWP_NOZORDER);
		m_SettingButtonState = false;	
	}
}
