#pragma once

#include "Core.h"
#include "FileUtils.h"
#include "Strings.h"
#include "StringPool.h"
#include "App.h"

#include <yaml-cpp/yaml.h>


struct YamlReader
{
	template <typename taType>
	static constexpr StringView sTypeName()
	{
		if constexpr (std::is_same_v<taType, StringView> 
			|| std::is_same_v<taType, TempString32>
			|| std::is_same_v<taType, TempString64>
			|| std::is_same_v<taType, TempString128>
			|| std::is_same_v<taType, TempString256>
			|| std::is_same_v<taType, TempString512>
			|| std::is_same_v<taType, String>)
			return "string";
		else if constexpr (std::is_same_v<taType, bool>)
			return "boolean";
		else if constexpr (std::is_floating_point_v<taType>)
			return "floating_point";
		else if constexpr (std::is_integral_v<taType>)
			return "integer";
		else
			static_assert(!sizeof(taType), "Unknown type");
		return "";
	}

	static constexpr StringView sNodeTypeName(YAML::NodeType::value inNodeType)
	{
		switch (inNodeType)
		{
		case YAML::NodeType::Undefined: return "Undefined";
		case YAML::NodeType::Null:		return "Null";
		case YAML::NodeType::Scalar:	return "Scalar";
		case YAML::NodeType::Sequence:	return "Sequence";
		case YAML::NodeType::Map:		return "Map";
		}

		return "";
	}

	// Helper to get a node based on its name if currently in a map, or based on current index if currently in a sequence.
	YAML::Node GetNode(StringView inVarName) const
	{
		const Element& current = mStack.back();
		if (current.mNode.IsSequence())
			return current.mNode[current.mIndex];
		else
			return current.mNode[std::string_view(inVarName)];
	}

	// Helper to get the path to a node based on its name if currently in a table, or based on current index if currently in an array.
	TempString512 GetPath(StringView inVarName) const
	{
		TempString512 path;
		for (const Element& elem : mStack)
		{
			if (path.Size() != 0 && !gEndsWith(path, "."))
				path.Append(".");

			path.Append(elem.mName);

			if (elem.mNode.IsSequence())
				path.AppendFormat("[{}]", elem.mIndex);
		}

		// Name can be empty if current node is an element in a sequence.
		// In that case the parent node added the index to the path already.
		if (!inVarName.empty())
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
		YAML::Node node = GetNode(inVarName);

		// If the variable doesn't exist that's okay.
		if (!node.IsDefined() || node.IsNull())
			return false;

		// If it's not a scalar, it's an error (should use OpenArray or OpenTable instead).
		if (!node.IsScalar())
		{
			gApp.LogError("{} should be a {} but isn't a {}.", GetPath(inVarName), sTypeName<taType>(), sNodeTypeName(node.Type()));
			mErrorCount++;
			return false;
		}

		if constexpr (std::is_same_v<taType, StringView>)
			// For StringView allocate a copy into the StringPool
			outVar = mStringPool->AllocateCopy(node.as<std::string_view>());
		else if constexpr (std::is_same_v<taType, TempString32>
			|| std::is_same_v<taType, TempString64>
			|| std::is_same_v<taType, TempString128>
			|| std::is_same_v<taType, TempString256>
			|| std::is_same_v<taType, TempString512>
			|| std::is_same_v<taType, String>)
			// For TempStrings and String, just copy into it.
			outVar = StringView(node.as<std::string_view>());
		else
		{
			// If the variable exists but is of the wrong type, that's an error.
			if (!YAML::convert<taType>::decode(node, outVar))
			{
				gApp.LogError(R"({} should be a {} but isn't (value: "{}").)", GetPath(inVarName), sTypeName<taType>(), node.Scalar());
				mErrorCount++;
				return false;
			}
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
		if (GetNode(inVarName).IsDefined())
		{
			gApp.LogError("{} is not allowed, {}", GetPath(inVarName), inReason);
			mErrorCount++;
		}
	}

	bool Init(StringView inPath, StringPool* ioStringPool)
	{
		// Manually check if the file exists otherwise YAML::LoadFile throws an exception (and exceptions are disabled).
		if (!gFileExists(inPath))
		{
			gApp.LogError(R"(Failed to parse YAML file "{}" - File not found.)", inPath);
			return false;
		}

		// Parse the yaml file.
		YAML::Node root_node = YAML::LoadFile(inPath.AsCStr());
		if (!root_node.IsDefined())
		{
			gApp.LogError(R"(Failed to parse YAML file "{}".)", inPath);
			return false;
		}

		mStringPool = ioStringPool;
		mStack.clear();
		mStack.push_back({ root_node });
		return true;
	}

	bool TryOpenTable(StringView inVarName)
	{
		YAML::Node node = GetNode(inVarName);

		// If the variable doesn't exist that's okay.
		if (!node.IsDefined())
			return false;

		// If it's the wrong type however, that's an error.
		if (!node.IsMap())
		{
			gApp.LogError("{} should be a Map but is a {}.", GetPath(inVarName), sNodeTypeName(node.Type()));
			mErrorCount++;
			return false;
		}

		mStack.push_back({ node, inVarName });
		return true;
	}

	bool OpenTable(StringView inVarName)
	{
		if (!TryOpenTable(inVarName))
		{
			gApp.LogError("{} (Map) is mandatory but was not found.", GetPath(inVarName));
			mErrorCount++;
			return false;
		}

		return true;
	}

	void CloseTable()
	{
		gAssert(mStack.back().mNode.IsMap());
		mStack.pop_back();
	}

	bool TryOpenArray(StringView inVarName)
	{
		YAML::Node node = GetNode(inVarName);

		// If the variable doesn't exist that's okay.
		if (!node.IsDefined())
			return false;

		// If it's the wrong type however, that's an error.
		if (!node.IsSequence())
		{
			gApp.LogError("{} should be a Sequence but is a {}.", GetPath(inVarName), sNodeTypeName(node.Type()));
			mErrorCount++;
			return false;
		}

		mStack.push_back({ node, inVarName });
		return true;
	}

	bool OpenArray(StringView inVarName)
	{
		if (!TryOpenArray(inVarName))
		{
			gApp.LogError("{} (Sequence) is mandatory but was not found.", GetPath(inVarName));
			mErrorCount++;
			return false;
		}

		return true;
	}

	void CloseArray()
	{
		gAssert(mStack.back().mNode.IsSequence());
		mStack.pop_back();
	}

	size_t GetArraySize() const
	{
		const Element& current = mStack.back();
		gAssert(current.mNode.IsSequence());
		return current.mNode.size();
	}

	bool NextArrayElement()
	{
		Element& current = mStack.back();
		gAssert(current.mNode.IsSequence());

		// Reached the end of the array?
		if (++current.mIndex >= (int)current.mNode.size())
			return false;

		return true;
	}

	template <typename taContainer>
	bool TryReadArray(StringView inVarName, taContainer& ioContainer)
	{
		if (!TryOpenArray(inVarName))
			return false;

		ioContainer.reserve(ioContainer.size() + GetArraySize());

		while (NextArrayElement())
			Read("", ioContainer.emplace_back());

		CloseArray();
		return true;
	}

	template <typename taContainer>
	bool ReadArray(StringView inVarName, taContainer& ioContainer)
	{
		if (!TryReadArray(inVarName, ioContainer))
		{
			gApp.LogError("{} (Sequence) is mandatory but was not found.", GetPath(inVarName));
			mErrorCount++;
			return false;
		}

		return true;
	}

	struct Element
	{
		const YAML::Node mNode  = {}; // Either a map or a sequence.
		StringView       mName;       // The name of that node, if there is one.
		int              mIndex = -1; // Index in the sequence if node is an sequence.
	};
	std::vector<Element> mStack;
	StringPool*          mStringPool = nullptr;
	int                  mErrorCount = 0;
};



