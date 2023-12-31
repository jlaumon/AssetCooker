solution "AssetCooker"
	
	platforms { "x64" }
	configurations { "Debug", "Release" }
	startproject "AssetCooker"

	project "AssetCooker"

        kind "WindowedApp"
        symbols "On"
        editandcontinue "On"
        dpiawareness "HighPerMonitor"
		cppdialect "C++20"
        
        defines
        {
            "_CRT_SECURE_NO_WARNINGS",
            "STR_DEFINE_STR32",
            "STR_SUPPORT_STD_STRING=0",
			"IMGUI_USER_CONFIG=<ImGuiConfig.h>"
        }

        filter { "toolset:msc*" }
            disablewarnings
            {
                "4244", -- 'return': conversion from '__int64' to 'int', possible loss of data
                "4267", -- 'initializing': conversion from 'size_t' to 'int', possible loss of data
            }

        filter { "configurations:Debug" }
            defines "ASSERTS_ENABLED"

        filter {}
        
		files 
		{
            "src/.clang-format",
			"src/**.h",
            "src/**.cpp",
            "src/**.natvis",
            "thirdparty/imgui/*.h",
            "thirdparty/imgui/*.cpp",
            "thirdparty/imgui/**.natvis",
            "thirdparty/imgui/backends/imgui_impl_dx11.cpp",
            "thirdparty/imgui/backends/imgui_impl_dx11.h",
            "thirdparty/imgui/backends/imgui_impl_win32.cpp",
            "thirdparty/imgui/backends/imgui_impl_win32.h",
            "thirdparty/Str/**.h",
            "thirdparty/Str/**.cpp",
            "thirdparty/Str/**.natvis",
            "thirdparty/icons/**.h",
        }
		
		includedirs 
		{
            "src",
            "thirdparty",
            "thirdparty/imgui",
            "thirdparty/imgui/backends",
        }
        
        links
        {
            "D3D11.lib",
        }
