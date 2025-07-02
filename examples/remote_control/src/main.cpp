// Dear ImGui: standalone example application for DirectX 11

// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <tchar.h>

#include <AssetCookerAPI.h>

#define STB_IMAGE_IMPLEMENTATION
#include <imgui_internal.h>

#include "stb_image.h"

#define _USE_MATH_DEFINES
#include <math.h>

// Data
static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static bool                     g_SwapChainOccluded = false;
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;

// Forward declarations of helper functions
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);


struct AssetCookerStatus
{
	~AssetCookerStatus()
	{
		AssetCooker_Detach(&mHandle);
	}

	void Launch()
	{
		// If there already was a handle, destroy it.
		AssetCooker_Detach(&mHandle);

		int options = AssetCookerOption_StartUnpaused;
		if (mStartMinimized)
			options |= AssetCookerOption_StartMinimized;

		// Launch Asset Cooker
		AssetCooker_Launch(mExecutablePath, mConfigFilePath, options, &mHandle);

		// Reset internal values.
		mIconAngle = 0.f;
		mIconIdleTimer = 0.f;
	}

	void Draw(ImVec2 inSize = { 64.f, 64.f });

	char				mExecutablePath[260] = {};
	char				mConfigFilePath[260] = {};
	bool				mStartMinimized = false;
	ImTextureRef		mIcon = {};

private:

	AssetCookerHandle	mHandle = nullptr;
	float				mIconAngle = 0.f;
	float				mIconIdleTimer = 0.f;
};


void AssetCookerStatus::Draw(ImVec2 inSize)
{
	mIconIdleTimer += ImGui::GetIO().DeltaTime;

	ImGui::Begin("Asset Cooker Status Bar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDocking);

	bool is_alive = AssetCooker_IsAlive(mHandle);
	bool is_idle = AssetCooker_IsIdle(mHandle);
	bool has_errors = AssetCooker_HasErrors(mHandle);
	bool is_paused = AssetCooker_IsPaused(mHandle);

	bool want_spin = is_alive && !is_idle;
	if (want_spin)
		mIconIdleTimer = 0;

	// Update the rotation of the icon.
	if (want_spin 
		|| mIconAngle != 0.f) // If we're already spinning, always continue until we reach angle zero.
	{
		constexpr float pi = 3.14159265358979323846f;

		float base_rot_speed = 2.5f * pi;
		float extra_rot_speed = (cosf(mIconAngle * 0.5f + pi) + 1.0f) * 1.2f * pi;

		float angle_before = mIconAngle;

		mIconAngle += (base_rot_speed + extra_rot_speed) * ImGui::GetIO().DeltaTime;
		mIconAngle = fmodf(mIconAngle, 4.0f * pi);

		// Keep spinning until we've made a full cycle, then stop.
		if (!want_spin && mIconAngle < angle_before)
			mIconAngle = 0.f;
	}

	ImU32 color_alive	= IM_COL32(255, 255, 255, 255);
	ImU32 color_dead	= IM_COL32(100, 100, 100, 255);
	ImU32 color_errors	= IM_COL32(255, 100, 100, 255);

	// Choose a color depending on status.
	ImU32 color = 0;
	if (!is_alive)
		color = color_dead;
	else if (has_errors)
		color = color_errors;
	else
		color = color_alive;

	// If the icon was idle for long enough, fade it out.
	constexpr float fadeout_start = 1.0f;
	constexpr float fadeout_duration = 0.5f;
	constexpr float fadeout_min_alpha = 0.5f;
	if (mIconIdleTimer > fadeout_start)
	{
		float fadeout_strength = (mIconIdleTimer - fadeout_start) / fadeout_duration;
		if (fadeout_strength > 1.0f) fadeout_strength = 1.0f;

		ImColor color4f(color);
		color4f.Value.w = 1.0f - (1.0f - fadeout_min_alpha) * fadeout_strength;

		color = color4f;
	}

	// Draw the icon.
	{
		ImVec2 p1 = ImGui::GetCursorScreenPos();
		ImVec2 p2 = { p1.x + inSize.x, p1.y            };
		ImVec2 p3 = { p1.x + inSize.x, p1.y + inSize.y };
		ImVec2 p4 = { p1.x           , p1.y + inSize.y };

		ImVec2 center = (p1 + p3) / 2.0f;

		float cos_a = cosf(mIconAngle);
		float sin_a = sinf(mIconAngle);

		p1 = ImRotate(p1 - center, cos_a, sin_a) + center;
		p2 = ImRotate(p2 - center, cos_a, sin_a) + center;
		p3 = ImRotate(p3 - center, cos_a, sin_a) + center;
		p4 = ImRotate(p4 - center, cos_a, sin_a) + center;

		ImGui::GetWindowDrawList()->AddImageQuad(mIcon, 
			p1, p2, p3, p4, 
			{ 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 }, 
			color);
	}

	// Add an invisible button where the icon is.
	bool clicked = ImGui::InvisibleButton("AssetCookerIcon", inSize);

	// Add a popup menu if right-clicking the icon.
	if (ImGui::BeginPopupContextItem())
	{
		mIconIdleTimer = 0;

		if (is_alive)
		{
			if (ImGui::MenuItem("Show Window"))
				AssetCooker_ShowWindow(mHandle);

			if (is_paused)
			{
				if (ImGui::MenuItem("Unpause"))
					AssetCooker_Pause(mHandle, false);
			}
			else
			{
				if (ImGui::MenuItem("Pause"))
					AssetCooker_Pause(mHandle, true);
			}
		}
		else
		{
			if (ImGui::MenuItem("Launch"))
				Launch();

			ImGui::MenuItem("Start Minimized", {}, &mStartMinimized);
		}

		ImGui::EndPopup();
	}

	// Add a tooltip when hovering the icon.
	if (ImGui::IsItemHovered())
	{
		mIconIdleTimer = 0;

		if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0))
		{
			ImGui::StartMouseMovingWindow(ImGui::GetCurrentWindow());
		}

		// Choose a tootip depending on status.
		const char* tooltip = "";
		if (!is_alive)
			tooltip = "Not running";
		else if (has_errors)
		{
			if (is_idle)
				tooltip = "Cooking errors!";
			else
				tooltip = "Cooking errors! But still cooking...";
		}
		else if (is_paused)
			tooltip = "Cooking paused";
		else if (!is_idle)
			tooltip = "Still cooking...";
		else
			tooltip = "All caught up!";

		ImGui::SetTooltip(tooltip);
	}

	if (clicked)
	{
		mIconIdleTimer = 0;

		if (is_alive)
			AssetCooker_ShowWindow(mHandle);
	}

	ImGui::End();
}


// Main code
int WinMain(
  HINSTANCE hInstance,
  HINSTANCE hPrevInstance,
  LPSTR     lpCmdLine,
  int       nShowCmd
)
{
	// Make process DPI aware and obtain main monitor scale
	ImGui_ImplWin32_EnableDpiAwareness();
	float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

	// Create application window
	WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"ImGui Example", nullptr };
	::RegisterClassExW(&wc);
	HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"Dear ImGui DirectX11 Example", WS_OVERLAPPEDWINDOW, 100, 100, (int)(1280 * main_scale), (int)(800 * main_scale), nullptr, nullptr, wc.hInstance, nullptr);

	// Initialize Direct3D
	if (!CreateDeviceD3D(hwnd))
	{
		CleanupDeviceD3D();
		::UnregisterClassW(wc.lpszClassName, wc.hInstance);
		return 1;
	}

	// Show the window
	::ShowWindow(hwnd, SW_SHOWDEFAULT);
	::UpdateWindow(hwnd);

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;       // Enable Multi-Viewport / Platform Windows
	//io.ConfigViewportsNoAutoMerge = true;
	//io.ConfigViewportsNoTaskBarIcon = true;
	//io.ConfigViewportsNoDefaultParent = true;
	//io.ConfigDockingAlwaysTabBar = true;
	//io.ConfigDockingTransparentPayload = true;

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	//ImGui::StyleColorsLight();

	// Setup scaling
	ImGuiStyle& style = ImGui::GetStyle();
	style.ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
	style.FontScaleDpi = main_scale;        // Set initial font scale. (using io.ConfigDpiScaleFonts=true makes this unnecessary. We leave both here for documentation purpose)
	io.ConfigDpiScaleFonts = true;          // [Experimental] Automatically overwrite style.FontScaleDpi in Begin() when Monitor DPI changes. This will scale fonts but _NOT_ scale sizes/padding for now.
	io.ConfigDpiScaleViewports = true;      // [Experimental] Scale Dear ImGui and Platform Windows when Monitor DPI changes.

	// When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		style.WindowRounding = 0.0f;
		style.Colors[ImGuiCol_WindowBg].w = 1.0f;
	}

	// Setup Platform/Renderer backends
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

	// Load Fonts
	// - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
	// - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
	// - If the file cannot be loaded, the function will return a nullptr. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
	// - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use Freetype for higher quality font rendering.
	// - Read 'docs/FONTS.md' for more instructions and details.
	// - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
	//style.FontSizeBase = 20.0f;
	//io.Fonts->AddFontDefault();
	//io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf");
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf");
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf");
	io.Fonts->AddFontFromFileTTF("../../data/fonts/cousine/Cousine-Regular.ttf", 14.0f);
	//ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf");
	//IM_ASSERT(font != nullptr);

	// Our state
	bool show_demo_window = true;
	bool show_another_window = false;
	ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

	
	FILE* f = nullptr;
	fopen_s(&f, "chef-hat-heart.png", "rb");
	int x, y, channels;
	auto pixels = stbi_load_from_file(f, &x, &y, &channels, 0);

	ImTextureData texture_data;
	texture_data.RefCount++;
	texture_data.Create(ImTextureFormat_RGBA32, x, y);
	memcpy(texture_data.GetPixels(), pixels, texture_data.GetSizeInBytes());

	stbi_image_free(pixels);
	fclose(f);

	texture_data.SetStatus(ImTextureStatus_WantCreate);

	ImGui::RegisterUserTexture(&texture_data);

	AssetCookerStatus ac_status;
	strcpy_s(ac_status.mExecutablePath, "../../bin/x64/Debug/AssetCookerDebug.exe");
	strcpy_s(ac_status.mConfigFilePath, "config.toml");
	ac_status.mIcon = texture_data.GetTexRef();


	// Main loop
	bool done = false;
	while (!done)
	{
		// Poll and handle messages (inputs, window resize, etc.)
		// See the WndProc() function below for our to dispatch events to the Win32 backend.
		MSG msg;
		while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
		{
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
			if (msg.message == WM_QUIT)
				done = true;
		}
		if (done)
			break;

		// Handle window being minimized or screen locked
		if (g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
		{
			::Sleep(10);
			continue;
		}
		g_SwapChainOccluded = false;

		// Handle window resize (we don't resize directly in the WM_SIZE handler)
		if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
		{
			CleanupRenderTarget();
			g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
			g_ResizeWidth = g_ResizeHeight = 0;
			CreateRenderTarget();
		}

		// Start the Dear ImGui frame
		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		
		ac_status.Draw();
		
		static AssetCookerHandle ac_handle = {};
		static bool start_minimized = false;

		{
			ImGui::Begin("Asset Cooker Remote Control Example");

			if (ImGui::Button("Launch Asset Cooker"))
			{
				if (ac_handle)
					AssetCooker_Detach(&ac_handle);

				AssetCooker_Launch("../../bin/x64/Debug/AssetCookerDebug.exe", "config.toml", 
					start_minimized ? AssetCookerOption_StartMinimized : 0, 
					&ac_handle);
			}

			ImGui::SameLine();

			ImGui::Checkbox("Start Minimized", &start_minimized);

			bool alive = AssetCooker_IsAlive(ac_handle) != 0;
			bool idle = AssetCooker_IsIdle(ac_handle) != 0;
			bool paused = AssetCooker_IsPaused(ac_handle) != 0;
			bool errors = AssetCooker_HasErrors(ac_handle) != 0;
			ImGui::Text("Alive %s", alive ? "true" : "false");
			ImGui::Text("Idle %s", idle ? "true" : "false");
			ImGui::Text("Paused %s", paused ? "true" : "false");
			ImGui::Text("Errors %s", errors ? "true" : "false");

			if (ImGui::Button("Kill"))
				AssetCooker_Kill(&ac_handle);

			if (ImGui::Button("Pause"))
				AssetCooker_Pause(ac_handle, 1);

			if (ImGui::Button("Unpause"))
				AssetCooker_Pause(ac_handle, 0);

			if (ImGui::Button("Show Window"))
				AssetCooker_ShowWindow(ac_handle);

			ImGui::End();
		}


		// 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
		if (show_demo_window)
		    ImGui::ShowDemoWindow(&show_demo_window);

		// 2. Show a simple window that we create ourselves. We use a Begin/End pair to create a named window.
		{
		    static float f = 0.0f;
		    static int counter = 0;

		    ImGui::Begin("Hello, world!");                          // Create a window called "Hello, world!" and append into it.

		    ImGui::Text("This is some useful text.");               // Display some text (you can use a format strings too)
		    ImGui::Checkbox("Demo Window", &show_demo_window);      // Edit bools storing our window open/close state
		    ImGui::Checkbox("Another Window", &show_another_window);

		    ImGui::SliderFloat("float", &f, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
		    ImGui::ColorEdit3("clear color", (float*)&clear_color); // Edit 3 floats representing a color

		    if (ImGui::Button("Button"))                            // Buttons return true when clicked (most widgets return true when edited/activated)
		        counter++;
		    ImGui::SameLine();
		    ImGui::Text("counter = %d", counter);

		    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
		    ImGui::End();
		}

		// 3. Show another simple window.
		if (show_another_window)
		{
		    ImGui::Begin("Another Window", &show_another_window);   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
		    ImGui::Text("Hello from another window!");
		    if (ImGui::Button("Close Me"))
		        show_another_window = false;
		    ImGui::End();
		}

		// Rendering
		ImGui::Render();
		const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
		g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
		g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

		// Update and Render additional Platform Windows
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
		}

		// Present
		HRESULT hr = g_pSwapChain->Present(1, 0);   // Present with vsync
		//HRESULT hr = g_pSwapChain->Present(0, 0); // Present without vsync
		g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
	}

	// Cleanup
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	CleanupDeviceD3D();
	::DestroyWindow(hwnd);
	::UnregisterClassW(wc.lpszClassName, wc.hInstance);

	return 0;
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
		if (wParam == SIZE_MINIMIZED)
			return 0;
		g_ResizeWidth = (UINT)LOWORD(lParam); // Queue resize
		g_ResizeHeight = (UINT)HIWORD(lParam);
		return 0;
	case WM_SYSCOMMAND:
		if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
			return 0;
		break;
	case WM_DESTROY:
		::PostQuitMessage(0);
		return 0;
	}
	return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
