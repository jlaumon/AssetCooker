/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once

#include "Core.h"
#include "Strings.h"

#include <toml++/toml.hpp>


inline TempString gToString(const toml::parse_error& inError)
{
	return gTempFormat(R"(%s (line %u, column %u)",
		inError.description().data(),
		inError.source().begin.line,
		inError.source().begin.column
		);
}


constexpr StringView gToStringView(toml::node_type inNodeType)
{
	auto sv = toml::impl::node_type_friendly_names[(int)inNodeType];
	return { sv.data(), (int)sv.size() };
}


inline toml::path operator+ (const toml::path& lhs, StringView rhs)
{
	std::string_view std_view(rhs.Data(), rhs.Size());
	return lhs + std_view;
}


struct TomlReader
{
	template <typename taType>
	static constexpr const char* sTypeName()
	{
		if constexpr (cIsSame<taType, StringView> 
			|| cIsSame<taType, TempString>
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

	// Helper to get a node based on its name if currently in a table, or based on current index if currently in an array.
	const toml::node* GetNode(StringView inVarName) const
	{
		const Element& current = mStack.Back();
		if (current.mNode->is_array())
			return current.mNode->as_array()->get(current.mIndex);
		else
			return current.mNode->as_table()->get(std::string_view(inVarName.Data(), inVarName.Size()));
	}

	// Helper to get the path to a node based on its name if currently in a table, or based on current index if currently in an array.
	toml::path GetPath(StringView inVarName) const
	{
		const Element& current = mStack.Back();
		if (current.mNode->is_array())
			return mPath + gTempFormat("[%d]", current.mIndex);
		else
			return mPath + inVarName;
	}

	template <typename taType>
	bool TryRead(StringView inVarName, taType& outVar)
	{
		const toml::node* node = GetNode(inVarName);

		// If the variable doesn't exist that's okay.
		if (!node)
			return false;

		// If the variable exists but is of the wrong type, that's an error.
		bool is_right_type;
		if constexpr (cIsSame<taType, StringView> 
			|| cIsSame<taType, TempString>
			|| cIsSame<taType, String>)
			is_right_type = node->is_string();
		else if constexpr (cIsSame<taType, bool>)
			is_right_type = node->is_boolean();
		else if constexpr (std::is_floating_point_v<taType>)
			is_right_type = node->is_floating_point();
		else if constexpr (std::is_integral_v<taType>)
			is_right_type = node->is_integer();
		else
			is_right_type = node->is<taType>();

		if (!is_right_type)
		{
			gAppLogError("%s should be a %s but is a %s.", 
				GetPath(inVarName).str().c_str(), sTypeName<taType>(), gToStringView(node->type()).AsCStr());
			mErrorCount++;
			return false;
		}

		if constexpr (cIsSame<taType, StringView>)
		{
			std::string_view std_view = *node->value<std::string_view>();
			// For StringView allocate a copy into the StringPool
			outVar = mStringPool->AllocateCopy(StringView(std_view.data(), (int)std_view.size()));
		}
		else if constexpr (cIsSame<taType, TempString>
			|| cIsSame<taType, String>)
		{
			std::string_view std_view = *node->value<std::string_view>();
			// For TempString and String, just copy into it.
			outVar = StringView(std_view.data(), (int)std_view.size());
		}
		else if constexpr (cIsSame<taType, bool>)
			outVar = *node->value<bool>();
		else if constexpr (std::is_floating_point_v<taType>)
			outVar = *node->value<taType>();
		else if constexpr (std::is_integral_v<taType>)
			outVar = (taType)*node->value<int64>();
		else
			outVar = *node.value<taType>();
		return true;
	}

	template <typename taType>
	bool Read(StringView inVarName, taType& outVar)
	{
		if (!TryRead(inVarName, outVar))
		{
			gAppLogError("%s (%s) is mandatory but was not found.", GetPath(inVarName).str().c_str(), sTypeName<taType>());
			mErrorCount++;
			return false;
		}

		return true;
	}

	void NotAllowed(StringView inVarName, StringView inReason)
	{
		if (GetNode(inVarName) != nullptr)
		{
			gAppLogError("%s is not allowed, %s", GetPath(inVarName).str().c_str(), inReason.AsCStr());
			mErrorCount++;
		}
	}

	bool Init(StringView inPath, StringPool* ioStringPool)
	{
		// Parse the toml file.
		mParsedFile = toml::parse_file(std::string_view(inPath.Data(), inPath.Size()));
		if (!mParsedFile)
		{
			gAppLogError(R"(Failed to parse TOML file "%s".)", inPath.AsCStr());
			gAppLogError("%s", gToString(mParsedFile.error()).AsCStr());
			return false;
		}

		mStringPool = ioStringPool;
		mStack.Clear();
		mStack.PushBack({ &mParsedFile.table() });
		return true;
	}

	bool TryOpenTable(StringView inVarName)
	{
		const toml::node* node = GetNode(inVarName);

		// If the variable doesn't exist that's okay.
		if (node == nullptr)
			return false;

		// If it's the wrong type however, that's an error.
		if (!node->is_table())
		{
			gAppLogError("%s should be a table but is a %s.", GetPath(inVarName).str().c_str(), gToStringView(node->type()).AsCStr());
			mErrorCount++;
			return false;
		}

		mPath = GetPath(inVarName);
		mStack.PushBack({ node });
		return true;
	}

	bool OpenTable(StringView inVarName)
	{
		if (!TryOpenTable(inVarName))
		{
			gAppLogError("%s (table) is mandatory but was not found.", GetPath(inVarName).str().c_str());
			mErrorCount++;
			return false;
		}

		return true;
	}

	void CloseTable()
	{
		gAssert(mStack.Back().mNode->is_table());
		mPath.truncate(1);
		mStack.PopBack();
	}

	bool TryOpenArray(StringView inVarName)
	{
		const toml::node* node = GetNode(inVarName);

		// If the variable doesn't exist that's okay.
		if (node == nullptr)
			return false;

		// If it's the wrong type however, that's an error.
		if (!node->is_array())
		{
			gAppLogError("%s should be an array but is a %s.", GetPath(inVarName).str().c_str(), gToStringView(node->type()).AsCStr());
			mErrorCount++;
			return false;
		}

		mPath = GetPath(inVarName);
		mStack.PushBack({ node });
		return true;
	}

	bool OpenArray(StringView inVarName)
	{
		if (!TryOpenArray(inVarName))
		{
			gAppLogError("%s (array) is mandatory but was not found.", GetPath(inVarName).str().c_str());
			mErrorCount++;
			return false;
		}

		return true;
	}

	void CloseArray()
	{
		gAssert(mStack.Back().mNode->is_array());
		mPath.truncate(1);
		mStack.PopBack();
	}

	int GetArraySize() const
	{
		const Element& current = mStack.Back();
		gAssert(current.mNode->is_array());
		return (int)current.mNode->as_array()->size();
	}

	bool NextArrayElement()
	{
		Element& current = mStack.Back();
		gAssert(current.mNode->is_array());

		// Reached the end of the array?
		if (++current.mIndex >= (int)current.mNode->as_array()->size())
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
			gAppLogError("%s (array) is mandatory but was not found.", GetPath(inVarName).str().c_str());
			mErrorCount++;
			return false;
		}

		return true;
	}

	struct Element
	{
		const toml::node* mNode = nullptr;	// Either a table or an array.
		int               mIndex = -1;		// Index in the array if node is an array.
	};
	Vector<Element>      mStack;
	toml::path           mPath;
	StringPool*          mStringPool = nullptr;
	int                  mErrorCount = 0;
	toml::parse_result   mParsedFile;
};



