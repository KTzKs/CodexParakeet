
// CodexParakeetDlg.h : ヘッダー ファイル
//

#pragma once
#include <map>
#include <string>
#include <mutex>


// CCodexParakeetDlg ダイアログ
class CCodexParakeetDlg : public CDialogEx
{
// コンストラクション
public:
	CCodexParakeetDlg(CWnd* pParent = nullptr);	// 標準コンストラクター

// ダイアログ データ
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_CODEXPARAKEET_DIALOG };
#endif

// 実装
protected:
	virtual BOOL OnInitDialog();
	virtual void DoDataExchange(CDataExchange* pDX);
	afx_msg BOOL OnCopyData(CWnd* pWnd, COPYDATASTRUCT* pCopyDataStruct);
	afx_msg LRESULT OnTrayMessage(WPARAM wParam, LPARAM lParam);
	afx_msg void OnDestroy();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg LRESULT OnSpeakInitialMessage(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnLipSync(WPARAM wParam, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg void OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct);
	afx_msg HCURSOR OnQueryDragIcon();
	afx_msg void OnBnClickedOk();
	afx_msg void OnBnClickedCancel();
	afx_msg void OnBnClickedCheckEnableTts();
	afx_msg void OnBnClickedMfcbutton1();
	DECLARE_MESSAGE_MAP()

private:
	void AddTrayIcon();
	void RemoveTrayIcon();
	void LoadMouthBitmaps();
	void ClearMouthBitmaps();
	void ResetLipBackBuffer(const CSize& size, CDC& referenceDc);
	void LoadVoiceSettings();
	void SaveVoiceSettings();
	void ApplyVoiceSettings();
	void SpeakMessage(const CString& message, const CString& threadId, const CString& spatial = CString());

	HICON m_hIcon;
	CButton m_CheckEnableTTS;
	CEdit m_EditSokudo;
	CEdit m_EditMuon;
	std::map<std::wstring, int> threadVoices_;
	int nextVoice_ = 0;
	CString initialMessage_;
	CString initialThreadId_;
	CString initialSpatial_;
	CStatic m_ParakeetArea;
	std::map<wchar_t, HBITMAP> mouthBitmaps_;
	CDC lipBackBufferDc_;
	CBitmap lipBackBufferBitmap_;
	CSize lipBackBufferSize_{};
	CButton m_CheckOnzo;
	bool m_SettingButtonState;
	CButton m_OnlyFirstSentence;
	CButton m_CheckAllSentence;
	std::mutex lipSyncStateMutex_;
	wchar_t latestMouth_ = L'c';
	wchar_t displayedMouth_ = L'c';
	bool lipSyncMessagePending_ = false;
};
