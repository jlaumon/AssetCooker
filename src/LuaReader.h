/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once

#include "Core.h"
#include "FileUtils.h"
#include "Strings.h"
#include "StringPool.h"
#include "App.h"

#include "minilua.h"

#include <Bedrock/TypeTraits.h>


struct LuaReader
{
	template <typename taType>
	static constexpr StringView sTypeName()
	{
		if constexpr (cIsSame<taType, StringView> 
			|| cIsSame<taType, FixedString32>
			|| cIsSame<taType, FixedString64>
			|| cIsSame<taType, FixedString128>
			|| cIsSame<taType, FixedString256>
			|| cIsSame<taType, FixedString512>
			|| cIsSame<taType, TempPath>
			|| cIsSame<taType, String>)
			return "string";
		else if constexpr (cIsSame<taType, bool>)
			return "boolean";
		else if constexpr (std::is_floating_point_v<taType>)
			return "floating_point";
		else if constexpr (std::is_integral_v<taType>)
			return "integer";
		else
			static_assert(!sizeof(taType), "Unknown type");
		return "";
	}

	enum class LuaType
	{
		None = -1,
		Nil,
		Boolean,
		LightUserData,
		Number,
		String,
		Table,
		Function,
		UserData,
		Thread
	};

	static constexpr StringView sNodeTypeName(LuaType inType)
	{
		switch (inType)
		{
		case LuaType::None:				return "None";
		case LuaType::Nil:				return "Bil";
		case LuaType::Boolean:			return "Boolean";
		case LuaType::LightUserData:	return "LightUserData";
		case LuaType::Number:			return "Number";
		case LuaType::String:			return "String";
		case LuaType::Table:			return "Table";
		case LuaType::Function:			return "Function";
		case LuaType::UserData:			return "UserData";
		case LuaType::Thread:			return "Thread";
		}
		return "";
	}

	// Helper to push a node on the top of the stack, based on its name if currently in a table, or based on current index if currently in a sequence.
	LuaType Push(StringView inVarName) const
	{
		if (mStack.Empty())
		{
			// If we're not already in a table, get a global variable.
			gAssert(!inVarName.Empty());
			return (LuaType)lua_getglobal(mLuaState, inVarName.AsCStr());
		}
		else
		{
			// Otherwise get a variable in the current table.

			// Push the key on the stack.
			const Element& current = mStack.Back();
			if (inVarName.Empty()) // If current is a sequence
				lua_pushinteger(mLuaState, current.mIndex + 1);
			else
				lua_pushstring(mLuaState, inVarName.AsCStr());

			// Get the value for that key (replaces it on the stack).
			return (LuaType)lua_gettable(mLuaState, -2);
		}
	}

	// Helper to get the path to a node based on its name if currently in a table, or based on current index if currently in an array.
	FixedString512 GetPath(StringView inVarName) const
	{
		FixedString512 path;
		for (const Element& elem : mStack)
		{
			if (path.Size() != 0 && !gEndsWith(path, "."))
				path.Append(".");

			path.Append(elem.mName);

			if (elem.mIndex != -1)
				path.AppendFormat("[{}]", elem.mIndex + 1);
		}

		// Name can be empty if current node is an element in a sequence.
		// In that case the parent node added the index to the path already.
		if (!inVarName.Empty())
		{
			if (path.Size() != 0 && !gEndsWith(path, "."))
				path.Append(".");

			path.Append(inVarName);
		}

		return path;
	}

	template <typename taType>
	bool TryRead(StringView inVarName, taType& outVar)
	{
		LuaType node_type = Push(inVarName);
		defer { lua_pop(mLuaState, 1); };

		// If the variable doesn't exist that's okay.
		if (node_type == LuaType::None || node_type == LuaType::Nil)
			return false;

		// If the variable exists but is of the wrong type, that's an error.
		bool is_right_type = false;
		if constexpr (cIsSame<taType, StringView> 
			|| cIsSame<taType, FixedString32>
			|| cIsSame<taType, FixedString64>
			|| cIsSame<taType, FixedString128>
			|| cIsSame<taType, FixedString256>
			|| cIsSame<taType, FixedString512>
			|| cIsSame<taType, TempPath>)
			is_right_type = (node_type == LuaType::String);
		else if constexpr (cIsSame<taType, bool>)
			is_right_type = (node_type == LuaType::Boolean);
		else if constexpr (std::is_floating_point_v<taType> || std::is_integral_v<taType>)
			is_right_type = (node_type == LuaType::Number);

		if (!is_right_type)
		{
			gApp.LogError("{} should be a {} but is a {}.", GetPath(inVarName), sTypeName<taType>(), sNodeTypeName(node_type));
			mErrorCount++;
			return false;
		}

		if constexpr (cIsSame<taType, StringView>)
		{
			// For StringView allocate a copy into the StringPool
			size_t      str_len = 0;
			const char* str     = lua_tolstring(mLuaState, -1, &str_len);
			outVar = mStringPool->AllocateCopy({ str, (int)str_len });
		}
		else if constexpr (cIsSame<taType, FixedString32>
			|| cIsSame<taType, FixedString64>
			|| cIsSame<taType, FixedString128>
			|| cIsSame<taType, FixedString256>
			|| cIsSame<taType, FixedString512>
			|| cIsSame<taType, TempPath>
			|| cIsSame<taType, String>)
		{
			// For FixedStrings and String, just copy into it.
			size_t      str_len = 0;
			const char* str     = lua_tolstring(mLuaState, -1, &str_len);
			outVar = StringView(str, str_len);
		}
		else if constexpr (cIsSame<taType, bool>)
		{
			outVar = lua_toboolean(mLuaState, -1);
		}
		else if constexpr (std::is_floating_point_v<taType>)
		{
			outVar = lua_tonumber(mLuaState, -1);
		}
		else if constexpr (std::is_integral_v<taType>)
		{
			outVar = lua_tointeger(mLuaState, -1);
		}
		else
		{
			gAssert(false); // Should have early out already.
			return false;
		}

		return true;
	}

	template <typename taType>
	bool Read(StringView inVarName, taType& outVar)
	{
		if (!TryRead(inVarName, outVar))
		{
			gApp.LogError("{} ({}) is mandatory but was not found.", GetPath(inVarName), sTypeName<taType>());
			mErrorCount++;
			return false;
		}

		return true;
	}

	void NotAllowed(StringView inVarName, StringView inReason)
	{
		LuaType node_type = Push(inVarName);
		defer { lua_pop(mLuaState, 1); };

		if (node_type != LuaType::None && node_type != LuaType::Nil)
		{
			gApp.LogError("{} is not allowed, {}", GetPath(inVarName), inReason);
			mErrorCount++;
		}
	}

	~LuaReader()
	{
		if (mLuaState)
			lua_close(mLuaState);
	}

	bool Init(StringView inPath, StringPool* ioStringPool)
	{
		// Create a new lua state.
		if (mLuaState)
			lua_close(mLuaState);
		mLuaState = luaL_newstate();

		// Open the std libs.
		luaL_openlibs(mLuaState);

		// Load and run the file.
		int ret = luaL_dofile(mLuaState, inPath.AsCStr());
		if (ret != LUA_OK)
		{
			gApp.LogError(R"(Failed to load "{}" - {}.)", inPath, lua_tostring(mLuaState, -1));
			return false;
		}

		mStringPool = ioStringPool;
		mStack.Clear();
		return true;
	}

	bool TryOpenTable(StringView inVarName)
	{
		LuaType node_type = Push(inVarName);

		// If the variable doesn't exist that's okay.
		if (node_type == LuaType::None || node_type == LuaType::Nil)
		{
			lua_pop(mLuaState, 1);
			return false;
		}

		// If it's the wrong type however, that's an error.
		if (node_type != LuaType::Table)
		{
			gApp.LogError("{} should be a {} but is a {}.", GetPath(inVarName), sNodeTypeName(LuaType::Table), sNodeTypeName(node_type));
			mErrorCount++;
			lua_pop(mLuaState, 1);
			return false;
		}

		mStack.PushBack({ inVarName });
		return true;
	}

	bool OpenTable(StringView inVarName)
	{
		if (!TryOpenTable(inVarName))
		{
			gApp.LogError("{} ({}) is mandatory but was not found.", GetPath(inVarName), sNodeTypeName(LuaType::Table));
			mErrorCount++;
			return false;
		}

		return true;
	}

	void CloseTable()
	{
		gAssert(lua_istable(mLuaState, -1));
		lua_pop(mLuaState, 1);
		mStack.PopBack();
	}

	bool TryOpenArray(StringView inVarName)
	{
		// Array and tables are the same thing in lua.
		return TryOpenTable(inVarName);
	}

	bool OpenArray(StringView inVarName)
	{
		// Array and tables are the same thing in lua.
		return OpenTable(inVarName);
	}

	void CloseArray()
	{
		// Array and tables are the same thing in lua.
		return CloseTable();
	}

	int GetArraySize() const
	{
		gAssert(lua_istable(mLuaState, -1));
		return (int)lua_rawlen(mLuaState, -1);
	}

	bool NextArrayElement()
	{
		gAssert(lua_istable(mLuaState, -1));
		Element& current = mStack.Back();

		// Reached the end of the array?
		if (++current.mIndex >= GetArraySize())
			return false;

		return true;
	}

	template <typename taContainer>
	bool TryReadArray(StringView inVarName, taContainer& ioContainer)
	{
		if (!TryOpenArray(inVarName))
			return false;

		ioContainer.Reserve(ioContainer.Size() + GetArraySize());

		while (NextArrayElement())
			Read("", ioContainer.EmplaceBack());

		CloseArray();
		return true;
	}

	template <typename taContainer>
	bool ReadArray(StringView inVarName, taContainer& ioContainer)
	{
		if (!TryReadArray(inVarName, ioContainer))
		{
			gApp.LogError("{} ({}) is mandatory but was not found.", GetPath(inVarName), sNodeTypeName(LuaType::Table));
			mErrorCount++;
			return false;
		}

		return true;
	}

	struct Element
	{
		StringView       mName;       // The name of that node, if there is one.
		int              mIndex = -1; // Index in the sequence if node is an sequence.
	};
	Vector<Element>      mStack;
	lua_State*           mLuaState   = nullptr;
	StringPool*          mStringPool = nullptr;
	int                  mErrorCount = 0;
};



