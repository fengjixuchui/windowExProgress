#include <windows.h>
#include <tchar.h>
#include <process.h>
#include <commctrl.h>
#include "ncbind/ncbind.hpp"

#define CLASSNAME _T("WindowExProgress")
#define KRKRDISPWINDOWCLASS _T("TScrollBox")

#ifndef ID_CANCEL
#define ID_CANCEL 3
#endif

/**
 * �Z�[�u�����X���b�h�p���
 * �v���O���X���������s����E�C���h�E
 */
class ProgressWindow {

public:
	/**
	 * �E�C���h�E�N���X�̓o�^
	 */
	static void registerWindowClass() {
		WNDCLASSEX wcex;
		ZeroMemory(&wcex, sizeof wcex);
		wcex.cbSize		= sizeof(WNDCLASSEX);
		wcex.style		= CS_HREDRAW | CS_VREDRAW;
		wcex.lpfnWndProc	= (WNDPROC)WndProc;
		wcex.cbClsExtra		= 0;
		wcex.cbWndExtra		= 0;
		wcex.hInstance		= GetModuleHandle(NULL);
		wcex.hIcon		    = NULL;
		wcex.hCursor		= LoadCursor(NULL, IDC_WAIT);
		wcex.hbrBackground	= (HBRUSH)(COLOR_WINDOW+1);
		wcex.lpszMenuName	= 0;
		wcex.lpszClassName	= CLASSNAME;
		wcex.hIconSm		= 0;
		RegisterClassEx(&wcex);
	}

	/**
	 * �E�C���h�E�N���X�̍폜
	 */
	static void unregisterWindowClass() {
		UnregisterClass(CLASSNAME, GetModuleHandle(NULL));
	}

	/**
	 * �R���X�g���N�^
	 */
	ProgressWindow(iTJSDispatch2 *window) : window(window), hParent(0), hWnd(0), thread(0), doneflag(false), cancelflag(false), percent(0), hProgress(0){
		prepare = CreateEvent(NULL, FALSE, FALSE, NULL);
		setReceiver(true);
		start();
	}

	/**
	 * �f�X�g���N�^
	 */
	~ProgressWindow() {
		CloseHandle(prepare);
		setReceiver(false);
		end();
	}
	
	/**
	 * �v���O���X�ʒm
	 * @return �L�����Z������Ă��� true
	 */
	bool doProgress(int percent) {
		if (percent != this->percent) {
			this->percent = percent;
			if (hProgress) {
				SendMessage(hProgress, PBM_SETPOS, (WPARAM)percent, 0 );
			}
		}
		return !hWnd && cancelflag;
	}

protected:
	iTJSDispatch2 *window; //< �e�E�C���h�E
	HWND hParent; //< �e�n���h��
	HWND hWnd; //< �����̃n���h��
	HANDLE thread; //< �v���O���X�����̃X���b�h
	HANDLE prepare; //< �����҂��C�x���g
	bool doneflag;   // �I���t���O
	bool cancelflag; // �L�����Z���t���O
	int percent; // �p�[�Z���g�w��

	HWND hProgress; //< �v���O���X�o�[�̃n���h��
	
	/**
	 * �E�C���h�E�v���V�[�W��
	 */
	static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
		ProgressWindow *self = (ProgressWindow*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if (self) {
			switch (message) {
			case WM_PAINT: // ��ʍX�V
				{
					PAINTSTRUCT ps;
					BeginPaint(hWnd, &ps);
					self->show();
					EndPaint(hWnd, &ps);
				}
				return 0;
			case WM_COMMAND: // �L�����Z���ʒm
				switch (wParam) {
				case ID_CANCEL:
					self->cancel();
					break;
				}
				break;
			}
		}
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	
	/*
	 * �E�C���h�E�C�x���g�������V�[�o
	 */
	static bool __stdcall receiver(void *userdata, tTVPWindowMessage *Message) {
		ProgressWindow *self = (ProgressWindow*)userdata;
		switch (Message->Msg) {
		case TVP_WM_ATTACH:
			self->start();
			break;
		case TVP_WM_DETACH:
			self->end();
			break;
		default:
			break;
		}
		return false;
	}

	// ���[�U���b�Z�[�W���V�[�o�̓o�^/����
	void setReceiver(bool enable) {
		tTJSVariant mode     = enable ? (tTVInteger)(tjs_int)wrmRegister : (tTVInteger)(tjs_int)wrmUnregister;
		tTJSVariant proc     = (tTVInteger)(tjs_int)receiver;
		tTJSVariant userdata = (tTVInteger)(tjs_int)this;
		tTJSVariant *p[] = {&mode, &proc, &userdata};
		if (window->FuncCall(0, L"registerMessageReceiver", NULL, NULL, 4, p, window) != TJS_S_OK) {
			TVPThrowExceptionMessage(L"can't regist user message receiver");
		}
	}
	
	// ���s�X���b�h
	static unsigned __stdcall threadFunc(void *data) {
		((ProgressWindow*)data)->main();
		_endthreadex(0);
		return 0;
	}

	/**
	 * �����J�n
	 */
	void start() {
		end();
		doneflag = false;
		tTJSVariant krkrHwnd;
		if (TJS_SUCCEEDED(window->PropGet(0, TJS_W("HWND"), NULL, &krkrHwnd, window))) {
			hParent = ::FindWindowEx((HWND)(tjs_int)krkrHwnd, NULL, KRKRDISPWINDOWCLASS, NULL);
			if (hParent) {
				thread = (HANDLE)_beginthreadex(NULL, 0, threadFunc, this, 0, NULL);
				if (thread) {
					WaitForSingleObject(prepare, 1000 * 3);
				}
			}
		}
	}
	
	/**
	 * �����I��
	 */
	void end() {
		doneflag = true;
		if (thread) {
			WaitForSingleObject(thread, INFINITE);
			CloseHandle(thread);
			thread = 0;
		}
		hParent = 0;
	}

	/**
	 * ���s���C������
	 * �E�C���h�E�̐�������j���܂ł�Ɨ������X���b�h�ōs��
	 */
	void main() {
		// �E�C���h�E����
		if (hParent && !hWnd) {
			RECT rect;
			POINT point;
			point.x = 0;
			point.y = 0;
			::GetClientRect(hParent, &rect);
			::ClientToScreen(hParent, &point);
			int left   = point.x;
			int top    = point.y;
			int width  = rect.right  - rect.left;
			int height = rect.bottom - rect.top;
			hWnd = ::CreateWindowEx(0, CLASSNAME, _T(""), WS_POPUP, left, top, width, height, 0, 0, GetModuleHandle(NULL), NULL);
			if (hWnd && !doneflag) {
				::SetWindowLong(hWnd, GWL_USERDATA, (LONG)this);
				::ShowWindow(hWnd,TRUE);
				create();
				// �҂����킹����
				SetEvent(prepare);
				// ���b�Z�[�W���[�v�̎��s
				MSG msg;
				while (!doneflag) {
					if (::PeekMessage(&msg, 0, 0, 0, PM_NOREMOVE)) {
						if (GetMessage(&msg, NULL, 0, 0)) {
							::TranslateMessage (&msg);
							::DispatchMessage (&msg);
						} else {
							break;
						}
					} else {
					    Sleep(0);
					}
				}
				// �E�C���h�E�̔j��
				::DestroyWindow(hWnd);
				hWnd = 0;
			}
		}
	}

	// -------------------------------------------------------------
	
	/**
	 * �`����e����
	 */
	void create() {

		// �v���O���X�o�[�̔z�u����
		RECT rect;
		GetClientRect(hWnd, &rect);
		int swidth  = rect.right  - rect.left;
		int sheight = rect.bottom - rect.top;
		int width = swidth/3;
		int height = sheight/10;
		// �v���O���X�o�[���쐬
		hProgress = CreateWindowEx(0, PROGRESS_CLASS, _T(""),
								   WS_VISIBLE | WS_CHILD | PBS_SMOOTH,
								   (swidth-width)/2, (sheight-height)/2, width, height, hWnd, (HMENU)1, GetModuleHandle(NULL), NULL);
		SendMessage(hProgress, PBM_SETRANGE , 0, MAKELPARAM(0, 100));
		SendMessage(hProgress, PBM_SETSTEP, 1, 0 );
		SendMessage(hProgress, PBM_SETPOS, percent, 0);
	}
	
	/**
	 * ��ʍX�V����
	 */
	void show() {
		// �r�b�g�}�b�v�w�肪����Δw�i������œh��Ԃ�
	}

	/**
	 * �L�����Z���ʒm
	 */
	void cancel() {
		cancelflag = true;
	}
};

/**
 * �E�C���h�E�Ƀ��C���Z�[�u�@�\���g��
 */
class WindowExProgress {

protected:
	iTJSDispatch2 *objthis; //< �I�u�W�F�N�g���̎Q��
	ProgressWindow *progressWindow; //< �v���O���X�\���p

public:
	/**
	 * �R���X�g���N�^
	 */
	WindowExProgress(iTJSDispatch2 *objthis) : objthis(objthis), progressWindow(NULL) {}

	/**
	 * �f�X�g���N�^
	 */
	~WindowExProgress() {
		delete progressWindow;
	}

	/**
	 * �v���O���X�������J�n����B
	 * �g���g�������s�u���b�N���ł�����ɕ\���p�����܂��B
	 * @param init �������f�[�^(����)
	 */
	void startProgress(tTJSVariant init) {
		if (progressWindow) {
			TVPThrowExceptionMessage(L"already running progress");
		}
		progressWindow = new ProgressWindow(objthis);
	}
	
	/**
	 * �v���O���X�����̌o�ߏ�Ԃ�ʒm����B
	 * @param percent �o�ߏ�Ԃ��p�[�Z���g�w��
	 * @return �L�����Z���v��������� true
	 */
	bool doProgress(int percent) {
		if (!progressWindow) {
			TVPThrowExceptionMessage(L"not running progress");
		}
		return progressWindow->doProgress(percent);
	}

	/**
	 * �v���O���X�������I������B
	 */
	void endProgress() {
		if (!progressWindow) {
			TVPThrowExceptionMessage(L"not running progress");
		}
		delete progressWindow;
		progressWindow = NULL;
	}
};

//---------------------------------------------------------------------------

// �C���X�^���X�Q�b�^
NCB_GET_INSTANCE_HOOK(WindowExProgress)
{
	NCB_INSTANCE_GETTER(objthis) { // objthis �� iTJSDispatch2* �^�̈����Ƃ���
		ClassT* obj = GetNativeInstance(objthis);	// �l�C�e�B�u�C���X�^���X�|�C���^�擾
		if (!obj) {
			obj = new ClassT(objthis);				// �Ȃ��ꍇ�͐�������
			SetNativeInstance(objthis, obj);		// objthis �� obj ���l�C�e�B�u�C���X�^���X�Ƃ��ēo�^����
		}
		return obj;
	}
};

NCB_ATTACH_CLASS_WITH_HOOK(WindowExProgress, Window) {
	NCB_METHOD(startProgress);
	NCB_METHOD(doProgress);
	NCB_METHOD(endProgress);
};

/**
 * �o�^������
 */
static void PreRegistCallback()
{
	ProgressWindow::registerWindowClass();
}

/**
 * �J�������O
 */
static void PostUnregistCallback()
{
	ProgressWindow::unregisterWindowClass();
}

NCB_PRE_REGIST_CALLBACK(PreRegistCallback);
NCB_POST_UNREGIST_CALLBACK(PostUnregistCallback);
