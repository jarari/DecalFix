#include <version.h>

#include "hooks.h"

F4SEPluginLoad(const F4SE::LoadInterface* a_f4se)
{
	F4SE::Init(a_f4se, {
		.log = true,
		.logName = "DecalFix",
		.trampoline = true,
		.trampolineSize = 128
	});

	Hooks::Install();

	return true;
}

extern "C"
{
	F4SE_EXPORT bool F4SEPlugin_Query(const F4SE::QueryInterface*, F4SE::PluginInfo* a_info)
	{
		a_info->name = Version::PROJECT.data();
		a_info->infoVersion = F4SE::PluginInfo::kVersion;
		a_info->version = Version::MAJOR;
		return true;
	}
}
