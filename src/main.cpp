/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <tchar.h>

#include "UI.h"
#include "App.h"
#include "Debug.h"
#include "FileSystem.h"
#include "CookingSystem.h"
#include "Notifications.h"
#include "Version.h"
#include <Bedrock/Test.h>
#include <Bedrock/Ticks.h>
#include <Bedrock/Trace.h>


// Data
static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;

// Forward declarations of helper functions
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static HashMap<String, String> sParseArguments(StringView inCommandLine);


static BOOL WINAPI sCtrlHandler(DWORD inEvent)
{
	switch (inEvent)
	{
	case CTRL_C_EVENT:
	case CTRL_CLOSE_EVENT:
	case CTRL_BREAK_EVENT:
		gApp.RequestExit();
		return TRUE;

	default:
		return FALSE;
	}
}


// Helper struct to measure CPU/GPU times of the UI updates.
struct FrameTimer
{
	static constexpr int cGPUHistorySize                    = 8;
	static constexpr int cCPUHistorySize                    = 8;

	ID3D11Query*         mDisjointQuery[cGPUHistorySize]    = {};
	ID3D11Query*         mStartQuery   [cGPUHistorySize]    = {};
	ID3D11Query*         mEndQuery     [cGPUHistorySize]    = {};
	double               mGPUTimesMS   [cGPUHistorySize]    = {}; // In Milliseconds.
	double               mCPUTimesMS   [cCPUHistorySize]    = {}; // In Milliseconds.
	uint64               mFrameIndex                        = 0;
	Timer                mCPUTimer;

	void Init()
	{
		for (auto& query : mDisjointQuery)
		{
			D3D11_QUERY_DESC desc = { D3D11_QUERY_TIMESTAMP_DISJOINT };
			g_pd3dDevice->CreateQuery(&desc, &query);
		}

		for (auto& query : mStartQuery)
		{
			D3D11_QUERY_DESC desc = { D3D11_QUERY_TIMESTAMP };
			g_pd3dDevice->CreateQuery(&desc, &query);
		}

		for (auto& query : mEndQuery)
		{
			D3D11_QUERY_DESC desc = { D3D11_QUERY_TIMESTAMP };
			g_pd3dDevice->CreateQuery(&desc, &query);
		}

		mFrameIndex = 0;
	}

	void Shutdown()
	{
		for (auto& query : mDisjointQuery)
			query->Release();

		for (auto& query : mStartQuery)
			query->Release();

		for (auto& query : mEndQuery)
			query->Release();

		*this = {};
	}

	void Reset()
	{
		for (auto& time : mGPUTimesMS)
			time = 0;
		for (auto& time : mCPUTimesMS)
			time = 0;
		mFrameIndex  = 0;
	}

	void StartFrame(double inWaitTime = 0)
	{
		// Reset the timer for this frame.
		mCPUTimer.Reset();

		// Start the queries for GPU time.
		auto gpu_frame_index = mFrameIndex % cGPUHistorySize;
		g_pd3dDeviceContext->Begin(mDisjointQuery[gpu_frame_index]);
		g_pd3dDeviceContext->End(mStartQuery[gpu_frame_index]);
	}

	void EndFrame()
	{
		// CPU time is between Strt and EndFrame.
		mCPUTimesMS[mFrameIndex % cCPUHistorySize] = gTicksToMilliseconds(mCPUTimer.GetTicks());

		// End the GPU queries.
		auto gpu_frame_index = mFrameIndex % cGPUHistorySize;
		g_pd3dDeviceContext->End(mEndQuery[gpu_frame_index]);
		g_pd3dDeviceContext->End(mDisjointQuery[gpu_frame_index]);

		// Update the frame index.
		mFrameIndex++;

		// Get the GPU query data for the oldest frame.
		// This is a bit sloppy, we don't check the return value, but it's unlikely to fail with >5 frames of history, and if it fails the GPU time will just be zero.
		gpu_frame_index = mFrameIndex % cGPUHistorySize;
		UINT64 start_time = 0;
		g_pd3dDeviceContext->GetData(mStartQuery[gpu_frame_index], &start_time, sizeof(start_time), 0);

		UINT64 end_time = 0;
		g_pd3dDeviceContext->GetData(mEndQuery[gpu_frame_index], &end_time, sizeof(end_time), 0);

		D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint_data = { .Frequency = 1, .Disjoint = TRUE };
		g_pd3dDeviceContext->GetData(mDisjointQuery[gpu_frame_index], &disjoint_data, sizeof(disjoint_data), 0);

		if (disjoint_data.Disjoint == FALSE)
		{
			UINT64 delta = end_time - start_time;
			mGPUTimesMS[gpu_frame_index] = (double)delta * 1000.0 / (double)disjoint_data.Frequency;
		}
		else
		{
			mGPUTimesMS[gpu_frame_index] = 0.0; // Better show 0 than an unreliable number.
		}
	}

	double GetGPUAverageMilliseconds() const
	{
		int    count = 0;
		double sum   = 0;
		for (double time : mGPUTimesMS)
		{
			if (time == 0.0) continue; // Skip invalid times.

			count++;
			sum += time;
		}
		if (count == 0)
			return 0; // Don't divide by zero.

		return sum / count;
	}

	double GetCPUAverageMilliseconds() const
	{
		int    count = 0;
		double sum   = 0;
		for (double time : mCPUTimesMS)
		{
			if (time == 0.0) continue; // Skip invalid times.

			count++;
			sum += time;
		}
		if (count == 0)
			return 0; // Don't divide by zero.

		return sum / count;
	}
};


// Main code
int WinMain(
  HINSTANCE hInstance,
  HINSTANCE hPrevInstance,
  LPSTR     lpCmdLine,
  int       nShowCmd
)
{
	// Init some temp memory.
	gThreadInitTempMemory(gMemAlloc(1_MiB));
	defer { gMemFree(gThreadExitTempMemory()); };

	// Extract flags from command line
	HashMap args = sParseArguments(lpCmdLine);

	// Check if we only want to run the tests.
	if (args.Contains("-test"))
	{
		return (gRunTests() == TestResult::Success) ? 0 : 1;
	}

	// Check if we only want to run without UI.
	gApp.mNoUI = args.Contains("-no_ui");
	if (gApp.mNoUI)
	{
		// Make sure there's a console so we can printf to it.
		if (!AttachConsole(ATTACH_PARENT_PROCESS))
			AllocConsole();

		// Redirect the std io to the console.
		FILE* dummy;
		freopen_s(&dummy, "CONIN$", "r", stdin);
		freopen_s(&dummy, "CONOUT$", "w", stderr);
		freopen_s(&dummy, "CONOUT$", "w", stdout);

		// Set a ctrl handler to gracefully exit on Ctrl+C, etc.
		SetConsoleCtrlHandler(sCtrlHandler, TRUE);
	}

	// Check if we want to change the working directory.
	// Note: This has to be done before gApp.Init() since that changes where the config.toml file is read from.
	if (auto working_dir = args.Find("-working_dir"); working_dir != args.End())
	{
		TempString abs_working_dir = gGetAbsolutePath(working_dir->mValue);
		if (!SetCurrentDirectoryA(abs_working_dir.AsCStr()))
			gAppFatalError(R"(Failed to set working directory to "%s")", abs_working_dir.AsCStr());
	}

	// Forward gTrace to gAppLog so we can see the test logs in Asset Cooker's logs.
	gSetTraceCallback([](StringView inStr) { gApp._Log(LogType::Normal, inStr); });
	defer { gSetTraceCallback(nullptr); }; // Also remove it eventually, otherwise an assert/trace in a global var destructor would crash.

	gSetCurrentThreadName("Main Thread");

	// Make process DPI aware and obtain main monitor scale
	ImGui_ImplWin32_EnableDpiAwareness();

	gApp.Init();

	TempString window_title = gApp.mMainWindowTitle;

	// If we don't have a proper version, add the build time to the window title to help identify.
	if constexpr (StringView(ASSET_COOKER_VER_FULL).Empty())
		gAppendFormat(window_title, " - Build: %s %s", __DATE__, __TIME__);

	wchar_t     window_title_wchar_buffer[256];
	WStringView window_title_wchar = gUtf8ToWideChar(window_title, window_title_wchar_buffer);

	if (window_title_wchar.empty())
		window_title_wchar = L"Asset Cooker";

	// TODO move that inside App?
	// TODO load window size/pos from config file?
	// Create application window
	HINSTANCE   hinstance = GetModuleHandle(nullptr);
	WNDCLASSEXW wc = { .cbSize        = sizeof(wc),
					   .style         = CS_CLASSDC,
					   .lpfnWndProc   = WndProc,
					   .cbClsExtra    = 0L,
					   .cbWndExtra    = 0L,
					   .hInstance     = hinstance,
					   .hIcon         = LoadIconA(hinstance, "chef_hat_heart"),
					   .hCursor       = nullptr,
					   .hbrBackground = nullptr,
					   .lpszMenuName  = nullptr,
					   .lpszClassName = window_title_wchar.data(),
					   .hIconSm       = nullptr };

	::RegisterClassExW(&wc);
	HWND hwnd = ::CreateWindowW(
		wc.lpszClassName, 
		window_title_wchar.data(), 
		WS_OVERLAPPEDWINDOW, 
		CW_USEDEFAULT, CW_USEDEFAULT, // pos
		CW_USEDEFAULT, CW_USEDEFAULT, // size
		nullptr, nullptr, wc.hInstance, nullptr);

	gApp.mMainWindowHwnd = hwnd;

	// Initialize the notifications and add the system tray icon.
	gNotifInit(hwnd);

	// Initialize Direct3D
	if (!CreateDeviceD3D(hwnd))
		gAppFatalError("Failed to create D3D device - %s", GetLastErrorString().AsCStr());

	// Show the window
	::ShowWindow(hwnd, gApp.mStartMinimized ? SW_SHOWMINIMIZED : SW_SHOWDEFAULT);
	::UpdateWindow(hwnd);

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
	//io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;       // Enable Multi-Viewport / Platform Windows
	//io.ConfigViewportsNoAutoMerge = true;
	//io.ConfigViewportsNoTaskBarIcon = true;
	//io.ConfigViewportsNoDefaultParent = true;
	//io.ConfigDockingAlwaysTabBar = true;
	//io.ConfigDockingTransparentPayload = true;

	// Setup Dear ImGui style
	//ImGui::StyleColorsDark();
	//ImGui::StyleColorsLight();

	// Set the DPI scale once, then it'll be updated by the WM_DPICHANGED message.
	gUISetDPIScale(ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd));

	// Setup scaling
	ImGuiStyle& style = ImGui::GetStyle();
	//style.ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
	//style.FontScaleDpi = main_scale;        // Set initial font scale. (using io.ConfigDpiScaleFonts=true makes this unnecessary. We leave both here for documentation purpose)
	io.ConfigDpiScaleFonts = true;          // [Experimental] Automatically overwrite style.FontScaleDpi in Begin() when Monitor DPI changes. This will scale fonts but _NOT_ scale sizes/padding for now.
	io.ConfigDpiScaleViewports = true;      // [Experimental] Scale Dear ImGui and Platform Windows when Monitor DPI changes.

	// When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		style.WindowRounding = 0.0f;
		style.Colors[ImGuiCol_WindowBg].w = 1.0f;
	}
		
	ImFontConfig font_config;
	font_config.SizePixels			 = 14.0f;
	font_config.FontDataOwnedByAtlas = false; // Fonts are embedded in the exe, they don't need to be released.

	// The main font.
	{
		auto cousine_ttf = gGetEmbeddedFont("cousine_regular");
		io.Fonts->AddFontFromMemoryTTF((void*)cousine_ttf.Data(), cousine_ttf.Size(), 14.0f, &font_config);
	}

	// The icons font.
	{
		ImFontConfig icons_config          = font_config;
		icons_config.MergeMode             = true; // Merge into the default font.
		icons_config.GlyphOffset.y         = 0;

		auto ttf_data = gGetEmbeddedFont("forkawesome");
		io.Fonts->AddFontFromMemoryTTF((void*)ttf_data.Data(), ttf_data.Size(), 14.0f, &icons_config);
	}

	// Setup Platform/Renderer backends
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

	FrameTimer frame_timer;
	frame_timer.Init();

	// TODO imgui tooltips are hard to deal with, maybe keep drawing for a fixed time longer than tooltip delay? (instead of frames)
	constexpr int cIdleFramesDelay = 30; // TODO should ideally be zero, but needs some fixes, check the other todos
	int           idle_frames      = 0;

	// Main loop
	while (!gApp.IsExitRequested())
	{
		if (!gCookingSystem.IsIdle())
			idle_frames = 0;
		else
			idle_frames++;

		// We've been idle enough time, don't draw the UI and wait for something to happen instead.
		if (idle_frames > cIdleFramesDelay)
		{
			while (true)
			{
				// Wait for either a window event (QS_ALLINPUT), exit being requested, or one second.
				constexpr int timeout_ms = 1000;
				HANDLE		  handles[]	 = { gApp.mExitRequestedEvent.GetOSHandle() };
				if (MsgWaitForMultipleObjects(1, handles, FALSE, timeout_ms, QS_ALLINPUT) != WAIT_TIMEOUT)
					break; // Something happened, stop idling!

				// After one second, check if the cooking system is still idle (files may have been modified).
				if (!gCookingSystem.IsIdle())
					break;
			}

			idle_frames = 0;
		}
		if (gApp.IsExitRequested())
			break;

		frame_timer.StartFrame();

		// Poll and handle messages (inputs, window resize, etc.)
		// See the WndProc() function below for our to dispatch events to the Win32 backend.
		MSG msg;
		while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
		{
			// If there's any message to process, we're not idle. The mouse might be moving over the UI, etc.
			idle_frames = 0;

			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
		}
		if (gApp.IsExitRequested())
			break;

		// If the window is minimized, never draw.
		gAssert(gApp.mMainWindowIsMinimized == (bool)IsIconic(hwnd));
		if (gApp.mMainWindowIsMinimized)
		{
			// We're not going to call EndFrame, so reset the timer.
			frame_timer.Reset();
			continue;
		}

		// Handle window resize (we don't resize directly in the WM_SIZE handler)
		if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
		{
			CleanupRenderTarget();
			g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
			g_ResizeWidth = g_ResizeHeight = 0;
			CreateRenderTarget();
		}

		gUIUpdate();

		// Start the Dear ImGui frame
		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		gDrawMainMenuBar();
		gDrawMain();

		// Rendering
		ImGui::Render();
		g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

		// Update and Render additional Platform Windows
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
		}

		frame_timer.EndFrame();

		gUILastFrameStats.mCPUMilliseconds = frame_timer.GetCPUAverageMilliseconds();
		gUILastFrameStats.mGPUMilliseconds = frame_timer.GetGPUAverageMilliseconds();

		g_pSwapChain->Present(1, 0); // Present with vsync
	}

	gFileSystem.StopMonitoring();
	gNotifExit();

	// Cleanup
	frame_timer.Shutdown();
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	CleanupDeviceD3D();
	::DestroyWindow(hwnd);
	::UnregisterClassW(wc.lpszClassName, wc.hInstance);

	int exit_code = 0;

	// If running without UI, print some stats and return failure if there were cooking errors.
	if (gApp.mNoUI)
	{
		int cooked_count = gCookingSystem.GetCookedCommandCount();
		int error_count = gCookingSystem.GetCookingErrorCount();
		int dirty_count = gCookingSystem.GetDirtyCommandCount();

		gAppLog("Cooked %d commands.", gCookingSystem.GetCookedCommandCount());

		if (error_count > 0)
		{
			gAppLogError("[error] There were %d cooking errors!", error_count);
			exit_code = 1;
		}

		if (dirty_count > 0)
		{
			gAppLogError("[error] Not all commands were cooked, %d dirty commands remaining!", dirty_count);
			exit_code = 1;
		}
	}

	gApp.Exit();

	return exit_code;
}

// Helper functions
bool CreateDeviceD3D(HWND hWnd)
{
	// Setup swap chain
	DXGI_SWAP_CHAIN_DESC sd;
	ZeroMemory(&sd, sizeof(sd));
	sd.BufferCount = 2;
	sd.BufferDesc.Width = 0;
	sd.BufferDesc.Height = 0;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = hWnd;
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.Windowed = TRUE;
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	UINT createDeviceFlags = 0;
	//createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
	D3D_FEATURE_LEVEL featureLevel;
	const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
	HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
	if (res == DXGI_ERROR_UNSUPPORTED) // Try high-performance WARP software driver if hardware is not available.
		res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
	if (res != S_OK)
		return false;

	CreateRenderTarget();
	return true;
}

void CleanupDeviceD3D()
{
	CleanupRenderTarget();
	if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
	if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
	if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
	ID3D11Texture2D* pBackBuffer;
	g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
	g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
	pBackBuffer->Release();
}

void CleanupRenderTarget()
{
	if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0 // From Windows SDK 8.1+ headers
#endif

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
		return true;

	switch (msg)
	{
	case WM_SIZE:
		// Any size message where the size is not minized means it's not minimized anymore.
		gApp.mMainWindowIsMinimized = (wParam == SIZE_MINIMIZED);

		if (wParam == SIZE_MINIMIZED)
		{
			if (gApp.mHideWindowOnMinimize)
			{
				// Hide the window.
				::ShowWindow(hWnd, SW_HIDE);

				// First time that happens, show a notification.
				if (gApp.mEnableNotifOnHideWindow != NotifEnabled::Never)
				{
					gApp.mEnableNotifOnHideWindow = NotifEnabled::Never;
					gNotifAdd(NotifType::Info, "Asset Cooker is still running!", "Click on the tray icon to make it appear again.");
				}
			}
			return 0;
		}
		g_ResizeWidth = (UINT)LOWORD(lParam); // Queue resize
		g_ResizeHeight = (UINT)HIWORD(lParam);
		return 0;
	case WM_SYSCOMMAND:
		if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
			return 0;
		break;
		
	case WM_DESTROY:
		::PostQuitMessage(0);
		[[fallthrough]];
	case WM_QUIT:
		gApp.RequestExit();
		return 0;
	case WM_DPICHANGED:
		{
			// Update the UI DPI scale.
			const int   dpi       = HIWORD(wParam);
			const float dpi_scale = (float)dpi / 96.0f;
			gUISetDPIScale(dpi_scale);

		}
		break;
	case WM_COMMAND:
		switch (wParam)
		{
		case cNotifMenuPauseCooking:
			gCookingSystem.SetCookingPaused(!gCookingSystem.IsCookingPaused());
			break;
			// Exit
		case cNotifMenuExit:
			gApp.RequestExit();
			break;
		default:
			break;
		}
		break;
	case WM_UNINITMENUPOPUP:
		// Reset the stored handle to prevent menu recreation.
		if (wParam == reinterpret_cast<UINT_PTR>(gApp.mNotifMenuHmenu))
		{
			gApp.mNotifMenuHmenu = nullptr;
		}
		break;
		
	case cNotifCallbackID:
		lParam = LOWORD(lParam);			// High word contains uID of the icon
		if (lParam == WM_LBUTTONDOWN ||     // Click on the notif icon
			lParam == NIN_BALLOONUSERCLICK) // Click on the notif popup
		{
			ShowWindow(hWnd, SW_RESTORE);   // Restore the window (if minimized or hidden).
			SetFocus(hWnd);                 // Set the focus on the window.
			SetForegroundWindow(hWnd);      // And bring it to the foreground.
		}
		
		if (lParam ==  WM_RBUTTONDOWN &&     // Right click on notification icon.
			gApp.mNotifMenuHmenu == nullptr) // Create the menu only if it's not already open.
		{
			// Create the menu where the mouse is placed.
			POINT cursorPosition = {};
			BOOL ret = GetCursorPos(&cursorPosition);
			gAssert(ret);

			HMENU hMenu = CreatePopupMenu();
			gApp.mNotifMenuHmenu = hMenu;
			bool isCookingPaused = gCookingSystem.IsCookingPaused(); 
			ret = InsertMenuA(hMenu, 0, MF_BYPOSITION | MF_STRING, cNotifMenuPauseCooking,  isCookingPaused ? "Resume cooking" : "Pause cooking");
			gAssert(ret);
			ret = InsertMenuA(hMenu, -1, MF_BYPOSITION | MF_STRING, cNotifMenuExit, "Exit");
			gAssert(ret);
			ret = TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_BOTTOMALIGN, cursorPosition.x, cursorPosition.y, 0, hWnd, nullptr);
			gAssert(ret);
		}
		break;
	}
			
	return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

HashMap<String, String> sParseArguments(StringView inCommandLine)
{
	HashMap<String, String> args;
	int pos = 0;

	while (pos < inCommandLine.Size())
	{
		// Skip spaces
		while (pos < inCommandLine.Size() && inCommandLine[pos] == ' ')
			pos++;

		// Find next space or end of string
		int start = pos;
		while (pos < inCommandLine.Size() && inCommandLine[pos] != ' ')
			pos++;

		// Extract the token
		StringView token = inCommandLine.SubStr(start, pos - start);

		if (!token.Empty() && token.Front() == '-') // It's a flag
		{
			int peek_pos = pos;

			// Peek at next token to check if it's a value (not another flag)
			while (peek_pos < inCommandLine.Size() && inCommandLine[peek_pos] == ' ') 
				peek_pos++;

			int value_start = peek_pos;
			while (peek_pos < inCommandLine.Size() && inCommandLine[peek_pos] != ' ') 
				peek_pos++;

			StringView value = inCommandLine.SubStr(value_start, peek_pos - value_start);

			if (value.Empty() || value.Front() == '-') 
			{
				args[token] = ""; // Flag without value
			}
			else
			{
				args[token] = value; // Flag with value

				// Skip directly to after the value
				pos = peek_pos;
			}
		}
	}

	return args;
}
