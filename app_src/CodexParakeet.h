
// CodexParakeet.h : PROJECT_NAME アプリケーションのメイン ヘッダー ファイルです
//

#pragma once

#ifndef __AFXWIN_H__
	#error "PCH に対してこのファイルをインクルードする前に 'pch.h' をインクルードしてください"
#endif

#include "resource.h"		// メイン シンボル
#include "VoicevoxEngine.h"


// CCodexParakeetApp:
// このクラスの実装については、CodexParakeet.cpp を参照してください
//

class CCodexParakeetApp : public CWinApp
{
public:
	CCodexParakeetApp();

// オーバーライド
public:
	virtual BOOL InitInstance();
	VoicevoxEngine& Engine() { return engine_; }

// 実装

	DECLARE_MESSAGE_MAP()
	VoicevoxEngine engine_;
};

extern CCodexParakeetApp theApp;
