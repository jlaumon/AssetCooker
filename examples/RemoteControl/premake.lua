solution "RemoteControl"
	
	platforms { "x64" }
	configurations { "Debug" }
	startproject "RemoteControl"

	project "RemoteControl"

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

		filter { "toolset:msc*" }
			buildoptions
			{
				"/utf-8" 
			}

		filter { "configurations:Debug" }
			defines "ASSERTS_ENABLED"
			optimize "Debug"
			editandcontinue "On"
			
		filter {}
		
		files 
		{
			"src/**.h",
			"src/**.cpp",
			"src/**.natvis",
			"../../thirdparty/Bedrock/Bedrock/*.h",
			"../../thirdparty/Bedrock/Bedrock/*.cpp",
			"../../thirdparty/Bedrock/Bedrock/*.natvis",
			"../../thirdparty/imgui/*.h",
			"../../thirdparty/imgui/*.cpp",
			"../../thirdparty/imgui/**.natvis",
			"../../thirdparty/imgui/backends/imgui_impl_dx11.cpp",
			"../../thirdparty/imgui/backends/imgui_impl_dx11.h",
			"../../thirdparty/imgui/backends/imgui_impl_win32.cpp",
			"../../thirdparty/imgui/backends/imgui_impl_win32.h",
			"../../api/*.h",
			"../../api/*.c",
		}
		
		includedirs 
		{
			".",
			"src",
			"../../api",
			"../../thirdparty/Bedrock",
			"../../thirdparty/imgui",
			"../../thirdparty/imgui/backends",
		}
		
		links
		{
			"D3D11.lib",
		}
		
