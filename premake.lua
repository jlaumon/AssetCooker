solution "AssetCooker"
	
	platforms { "x64" }
	configurations { "Debug", "DebugASAN", "DebugOpt", "Release" }
	startproject "AssetCooker"

	project "AssetCooker"

        kind "WindowedApp"
        symbols "On"
        dpiawareness "HighPerMonitor"
		cppdialect "C++20"
		exceptionhandling "Off"
		rtti "Off"
		staticruntime "On" -- Makes the exe a bit bigger but don't need to install the vcredist to work
		flags 
		{
			"MultiProcessorCompile",
			"FatalWarnings"
		}
        
        defines
        {
            "_CRT_SECURE_NO_WARNINGS",
			"IMGUI_USER_CONFIG=<ImGuiConfig.h>",
			"TOML_COMPILER_HAS_EXCEPTIONS=0", -- Not strictly necessart since toml++ detects if exceptions are off, but Intellisense is confused otherwise
			"LUA_USE_LONGJMP", -- Otherwise Lua uses exceptions for error handling (because we're compiling it as c++)
			"BEDROCK_ENABLE_STD", -- Needed until we get rid of all std:: uses
        }

        filter { "toolset:msc*" }
            buildoptions
            {
            	"/utf-8" 
            }
            disablewarnings
            {
                "4244", -- 'return': conversion from '__int64' to 'int', possible loss of data
                "4267", -- 'initializing': conversion from 'size_t' to 'int', possible loss of data
            }

        filter { "configurations:Debug" }
			targetsuffix "Debug"
            defines "ASSERTS_ENABLED"
			optimize "Debug"
			editandcontinue "On"
			
		filter { "configurations:DebugASAN" }
			targetsuffix "DebugASAN"
            defines "ASSERTS_ENABLED"
			optimize "Debug"
			editandcontinue "Off"     -- incompatible with ASAN
			flags "NoIncrementalLink" -- incompatible with ASAN
			sanitize "Address"
			
		filter { "configurations:DebugOpt" }
			targetsuffix "DebugOpt"
            defines "ASSERTS_ENABLED"
			optimize "Full"
			editandcontinue "On"
			
		filter { "configurations:Release" }
			optimize "Full"
			
        filter {}
        
		files 
		{
			"src/enable_utf8.manifest",
            "src/.clang-format",
			"src/**.h",
            "src/**.cpp",
            "src/**.natvis",
			"thirdparty/Bedrock/Bedrock/*.h",
            "thirdparty/Bedrock/Bedrock/*.cpp",
            "thirdparty/Bedrock/Bedrock/*.natvis",
            "thirdparty/imgui/*.h",
            "thirdparty/imgui/*.cpp",
            "thirdparty/imgui/**.natvis",
            "thirdparty/imgui/backends/imgui_impl_dx11.cpp",
            "thirdparty/imgui/backends/imgui_impl_dx11.h",
            "thirdparty/imgui/backends/imgui_impl_win32.cpp",
            "thirdparty/imgui/backends/imgui_impl_win32.h",
            "thirdparty/icons/**.h",
            "thirdparty/WindowsHModular/include/**.h",
            "thirdparty/xxHash/*.h",
            "thirdparty/subprocess/*.h",
			"thirdparty/tomlplusplus/include/**.hpp",
			"thirdparty/tomlplusplus/include/**.inl",
			"thirdparty/lz4/lib/lz4.h",
			"thirdparty/lz4/lib/lz4.c",
			"thirdparty/yaml-cpp/src/**.cpp",
			"thirdparty/yaml-cpp/src/**.h",
			"thirdparty/yaml-cpp/include/**.h",
			"thirdparty/minilua/*.h",
			"data/**.rc",
			"data/**.h",
        }
		
		includedirs 
		{
			".",
            "src",
            "thirdparty",
            "thirdparty/Bedrock",
            "thirdparty/imgui",
            "thirdparty/imgui/backends",
			"thirdparty/WindowsHModular/include",
			"thirdparty/xxHash",
			"thirdparty/tomlplusplus/include",
			"thirdparty/lz4/lib",
			"thirdparty/yaml-cpp/include",
			"thirdparty/minilua",
        }
        
        links
        {
            "D3D11.lib",
        }
		
