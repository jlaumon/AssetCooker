#include "CookingSystem.h"
#include "App.h"


void CookingSystem::CreateCommandsForFile(FileInfo& ioFile) const
{
	for (const CookingRule& rule : mRules)
	{
		bool match = true;

		for (auto& pattern : rule.mInputPatterns)
		{
			// TODO
			//if (!pattern.mExtension.empty() && gIsEqualCI())
		}
	}
}





bool CookingSystem::ValidateRules()
{
	HashSet<StringView> all_names;
	int                 errors = 0;
	int                 rule_index = 0;
	for (const CookingRule& rule : mRules)
	{
		// Validate the name.
		if (!rule.mName.empty())
		{
			auto [_, inserted] = all_names.insert(rule.mName);
			if (!inserted)
			{
				errors++;
				gApp.LogError(std::format(R"(Found multiple rules with name "{}")", rule.mName));
			}
		}
		else
		{
			errors++;
			gApp.LogError(std::format(R"(Rule with index {} has no name)", rule_index));
		}



		rule_index++;
	}

	return errors == 0;
}
