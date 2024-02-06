#include "RuleParser.h"
#include "CookingSystem.h"
#include "App.h"

#include <toml++/toml.hpp>


// Formatter for toml errors.
template <> struct std::formatter<toml::parse_error> : std::formatter<std::string_view>
{
	auto format(const toml::parse_error& inError, format_context& ioCtx) const
	{
		return std::format_to(ioCtx.out(), R"({} (line {}, column {}))",
			inError.description(),
			inError.source().begin.line,
			inError.source().begin.column
			);
	}
};


// Formatter for toml node types.
template <> struct std::formatter<toml::node_type> : std::formatter<std::string_view>
{
	auto format(const toml::node_type& inNodeType, format_context& ioCtx) const
	{
		return std::format_to(ioCtx.out(), "{}", toml::impl::node_type_friendly_names[(int)inNodeType]);
	}
};


// Formatter for toml path.
template <> struct std::formatter<toml::path> : std::formatter<std::string_view>
{
	auto format(const toml::path& inPath, format_context& ioCtx) const
	{
		return std::format_to(ioCtx.out(), "{}", inPath.str());
	}
};

struct TOMLReader
{
	template <typename taType>
	static constexpr StringView sTypeName()
	{
		if constexpr (std::is_same_v<taType, StringView> 
			|| std::is_same_v<taType, TempString32>
			|| std::is_same_v<taType, TempString64>
			|| std::is_same_v<taType, TempString128>)
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

	// Helper to get a node based on its name if currently in a table, or based on current index if currently in an array.
	const toml::node* GetNode(StringView inVarName) const
	{
		const Element& current = mStack.back();
		if (current.mNode->is_array())
			return current.mNode->as_array()->get(current.mIndex);
		else
			return current.mNode->as_table()->get(inVarName);
	}

	// Helper to get the path to a node based on its name if currently in a table, or based on current index if currently in an array.
	toml::path GetPath(StringView inVarName) const
	{
		const Element& current = mStack.back();
		if (current.mNode->is_array())
			return mPath + TempString32("[{}]", current.mIndex).AsStringView();
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
		if constexpr (std::is_same_v<taType, StringView> 
			|| std::is_same_v<taType, TempString32>
			|| std::is_same_v<taType, TempString64>
			|| std::is_same_v<taType, TempString128>)
			is_right_type = node->is_string();
		else if constexpr (std::is_same_v<taType, bool>)
			is_right_type = node->is_boolean();
		else if constexpr (std::is_integral_v<taType>)
			is_right_type = node->is_integer();
		else
			is_right_type = node->is<taType>();

		if (!is_right_type)
		{
			gApp.LogError("{} should be a {} but is a {}.", GetPath(inVarName), sTypeName<taType>(), node->type());
			mErrorCount++;
			return false;
		}

		if constexpr (std::is_same_v<taType, StringView>)
			// For StringView allocate a copy into the StringPool
			outVar = mStringPool.AllocateCopy(*node->value<std::string_view>());
		else if constexpr (std::is_same_v<taType, TempString32>
			|| std::is_same_v<taType, TempString64>
			|| std::is_same_v<taType, TempString128>)
			// For TempStrings, just copy into it.
			outVar = StringView(*node->value<std::string_view>());
		else if constexpr (std::is_same_v<taType, bool>)
			outVar = *node->value<bool>();
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
			gApp.LogError("{} ({}) is mandatory but was not found.", GetPath(inVarName), sTypeName<taType>());
			mErrorCount++;
			return false;
		}

		return true;
	}

	TOMLReader(const toml::table& inRootTable, StringPool& ioStringPool)
		: mStringPool(ioStringPool)
	{
		mStack.push_back({ &inRootTable });
	}

	bool OpenTable(StringView inVarName)
	{
		const toml::node* node = GetNode(inVarName);

		// If the variable doesn't exist that's okay.
		if (node == nullptr)
			return false;

		// If it's the wrong type however, that's an error.
		if (!node->is_table())
		{
			gApp.LogError("{} should be a table but is a {}.", GetPath(inVarName), node->type());
			mErrorCount++;
			return false;
		}

		mPath = GetPath(inVarName);
		mStack.push_back({ node });
		return true;
	}

	bool TryOpenTable(StringView inVarName)
	{
		if (!OpenTable(inVarName))
		{
			gApp.LogError("{} (table) is mandatory but was not found.", GetPath(inVarName));
			mErrorCount++;
			return false;
		}

		return true;
	}

	void CloseTable()
	{
		gAssert(mStack.back().mNode->is_table());
		mPath.truncate(1);
		mStack.pop_back();
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
			gApp.LogError("{} should be an array but is a {}.", GetPath(inVarName), node->type());
			mErrorCount++;
			return false;
		}

		mPath = GetPath(inVarName);
		mStack.push_back({ node });
		return true;
	}

	bool OpenArray(StringView inVarName)
	{
		if (!TryOpenArray(inVarName))
		{
			gApp.LogError("{} (array) is mandatory but was not found.", GetPath(inVarName));
			mErrorCount++;
			return false;
		}

		return true;
	}

	void CloseArray()
	{
		gAssert(mStack.back().mNode->is_array());
		mPath.truncate(1);
		mStack.pop_back();
	}

	size_t GetArraySize() const
	{
		const Element& current = mStack.back();
		gAssert(current.mNode->is_array());
		return current.mNode->as_array()->size();
	}

	bool NextArrayElement()
	{
		Element& current = mStack.back();
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
			gApp.LogError("{} (array) is mandatory but was not found.", GetPath(inVarName));
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
	std::vector<Element> mStack;
	toml::path           mPath;
	StringPool&          mStringPool;
	int                  mErrorCount = 0;
};




void gReadRuleFile(StringView inPath)
{
	gApp.Log("Reading Rule file: {}", inPath);

	toml::parse_result rules_toml = toml::parse_file(inPath);
	if (!rules_toml)
	{
		gApp.LogError("Failed to parse Rule file.");
		gApp.LogError("{}", rules_toml.error());
		gApp.SetInitError("Failed to parse Rule file. See log for details.");
	}

	TOMLReader reader(rules_toml.table(), gCookingSystem.GetRuleStringPool());

	defer
	{
		// At the end if there were any error, tell the app to not start.
		if (reader.mErrorCount)
			gApp.SetInitError("Failed to parse Rule file. See log for details.");
	};

	if (!reader.OpenArray("Rule"))
		return;

	while (reader.NextArrayElement())
	{
		if (!reader.OpenTable(""))
			continue;

		defer { reader.CloseTable(); };

		CookingRule& rule = gCookingSystem.AddRule();
		reader.Read   ("Name",				rule.mName);

		if (reader.OpenArray("InputFilters"))
		{
			defer { reader.CloseArray(); };

			while (reader.NextArrayElement())
			{
				if (!reader.OpenTable(""))
					continue;
				defer { reader.CloseTable(); };

				InputFilter& input_filter = rule.mInputFilters.emplace_back();

				TempString128 repo_name;
				if (reader.Read("Repo", repo_name))
				{
					FileRepo* repo = gFileSystem.FindRepo(repo_name.AsStringView());
					if (repo == nullptr)
					{
						gApp.LogError(R"(Repo "{}" not found.)", repo_name.AsStringView());
						reader.mErrorCount++;
					}
					else
					{
						input_filter.mRepoIndex = repo->mIndex;
					}
				}

				reader.TryReadArray("Extensions",			input_filter.mExtensions);
				reader.TryReadArray("DirectoryPrefixes",	input_filter.mDirectoryPrefixes);
				reader.TryReadArray("NamePrefixes",		input_filter.mNamePrefixes);
				reader.TryReadArray("NameSuffixes",		input_filter.mNameSuffixes);
			}
		}

		reader.Read        ("CommandLine",		rule.mCommandLine);
		reader.TryRead     ("Priority",			rule.mPriority);
		reader.TryRead     ("Vresion",			rule.mVersion);
		reader.TryRead     ("MatchMoreRules",	rule.mMatchMoreRules);
		reader.TryRead     ("DepFilePath",		rule.mDepFilePath);
		reader.TryReadArray("InputPaths",		rule.mInputPaths);
		reader.TryReadArray("OutputPaths",		rule.mOutputPaths);
	}
}
