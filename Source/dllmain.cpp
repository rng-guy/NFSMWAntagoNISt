
// Compatibility checks -----------------------------------------------------------------------------------------------------------------------------

#ifndef _MSC_VER
#error "AntagoNISt requires MSVC."

#elif (_MSC_VER < 1930)
#error "AntagoNISt requires Visual Studio 2022 or newer."

#elif ((not defined(_WIN32)) or defined(_WIN64))
#error "AntagoNISt requires 32-bit Windows."

#elif ((not defined(_MSVC_LANG)) or (_MSVC_LANG < 202002L))
#error "AntagoNISt requires C++20 or newer."

#endif





// Project includes ---------------------------------------------------------------------------------------------------------------------------------

#include <array>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <string_view>

#include <Windows.h>

#include "Headers/StreamParser.hpp"
#include "Headers/MemoryTools.hpp"





// Aliases ------------------------------------------------------------------------------------------------------------------------------------------

using Parser = StreamParser::Parser<>;

using MemoryTools::address;

using MemoryTools::AsReference;
using MemoryTools::AsFunction;

using vault = uint32_t;





// Mod setup ----------------------------------------------------------------------------------------------------------------------------------------

const std::filesystem::path configFile = "scripts/NFSMWAntagoNIStSettings.ini";

// Types and aliases
constexpr size_t numVehicles = 8; // same as vanilla

using Vehicles = std::array<vault, numVehicles>;

static_assert(sizeof(Vehicles) == 32);

struct Scene
{
// Members

	const std::string_view name;
	const size_t           index;

	Vehicles vehicles = {};
};

// Assembly detours
const Vehicles* vehicles = nullptr;

constinit std::array scenes =
{
	Scene("IntroNisBL14", 0),
	Scene("IntroNisBL12", 0),
	Scene("IntroNisBL15", 1),
	Scene("IntroNisBL13", 1),
	Scene("IntroNisBL09", 1),
	Scene("IntroNisBL05", 1),
	Scene("IntroNisBL04", 1),
	Scene("IntroNisBL07", 2),
	Scene("IntroNisBL03", 2),
	Scene("IntroNisBL02", 2),
	Scene("IntroNisBL06", 3),
	Scene("IntroNisBL11", 4),
	Scene("IntroNisBL10", 4),
	Scene("EndingNis04",  5),
	Scene("IntroNisDD",   6),
	Scene("EndingNis03",  7)
};





// Auxiliary functions  -----------------------------------------------------------------------------------------------------------------------------

static const Vehicles* __fastcall GetVehicles(const char* const sceneName)
{
	for (const Scene& scene : scenes)
	{
		if (scene.name == sceneName) return &(scene.vehicles);
	}

	return nullptr;
}





// Assembly detours ---------------------------------------------------------------------------------------------------------------------------------

// Checks name of the upcoming cutscene for the game to play
ASSEMBLY_DETOUR(SceneName, /* begin = */ 0x6F5223, /* end = */ 0x6F5228)
{
	__asm
	{
		// Execute original code first
		mov edi, dword ptr [ebp + 0x8]
		mov esi, ecx

		mov ecx, ebx
		call GetVehicles // ecx: sceneName
		mov dword ptr [vehicles], eax

		EXIT_ASSEMBLY_DETOUR(SceneName)
	}
}



// Retrieves vehicles for upcoming cutscene
ASSEMBLY_DETOUR(SceneVehicles, 0x6F30CB, 0x6F30D1)
{
	static constexpr address listExit = 0x6F3161;

	__asm
	{
		mov eax, dword ptr [vehicles]
		test eax, eax
		jne list // use custom list

		sub ebx, 3
		cmp ebx, 7

		EXIT_ASSEMBLY_DETOUR(SceneVehicles)

		list:
		xor ebx, ebx
		mov dword ptr [esp + 0x80], eax

		jmp dword ptr [listExit]
	}
}





// Initialisation helpers ---------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] static constexpr vault GetVaultHash(std::string_view input)
{
	if (input.empty()) return 0x0;

	vault a = 0x9E3779B9; // golden ratio
	vault b = a;
	vault c = 0xABCDEF00; // MW-specific seed

	const auto Shift = [&input](const size_t i, const size_t n) -> vault
	{
		// Force zero-extension first to avoid underflow in second cast
		return (static_cast<vault>(static_cast<unsigned char>(input[i])) << n);
	};

	const auto MixValues = [&a, &b, &c]() -> void
	{
		a -= b; a -= c; a ^= (c >> 13);
		b -= c; b -= a; b ^= (a <<  8);
		c -= a; c -= b; c ^= (b >> 13);
		a -= b; a -= c; a ^= (c >> 12);
		b -= c; b -= a; b ^= (a << 16);
		c -= a; c -= b; c ^= (b >>  5);
		a -= b; a -= c; a ^= (c >>  3);
		b -= c; b -= a; b ^= (a << 10);
		c -= a; c -= b; c ^= (b >> 15);
	};

	const size_t size = input.size();

	while (input.size() >= 12)
	{
		a += Shift(0, 0) + Shift(1, 8) + Shift( 2, 16) + Shift( 3, 24);
		b += Shift(4, 0) + Shift(5, 8) + Shift( 6, 16) + Shift( 7, 24);
		c += Shift(8, 0) + Shift(9, 8) + Shift(10, 16) + Shift(11, 24);

		MixValues();

		input.remove_prefix(12);
	}

	switch (input.size())
	{
		case 11: c += Shift(10, 24); [[fallthrough]];
		case 10: c += Shift( 9, 16); [[fallthrough]];
		case  9: c += Shift( 8,  8); [[fallthrough]];
		case  8: b += Shift( 7, 24); [[fallthrough]];
		case  7: b += Shift( 6, 16); [[fallthrough]];
		case  6: b += Shift( 5,  8); [[fallthrough]];
		case  5: b += Shift( 4,  0); [[fallthrough]];
		case  4: a += Shift( 3, 24); [[fallthrough]];
		case  3: a += Shift( 2, 16); [[fallthrough]];
		case  2: a += Shift( 1,  8); [[fallthrough]];
		case  1: a += Shift( 0,  0); [[fallthrough]];
		case  0: c += size;
	}

	MixValues();

	return c;
}



[[nodiscard]] static consteval vault operator""_vlt
(
	const char* const string,
	const size_t      length
) {
	return GetVaultHash({string, length});
}



[[nodiscard]] static bool IsValidVehicleType(const vault type)
{
	const auto GetVaultNode          = AsFunction<address __cdecl    (vault, vault)>          (0x455FD0);
	const auto GetVaultNodeAttribute = AsFunction<address __thiscall (address, vault, size_t)>(0x454190);

	const address node = GetVaultNode("pvehicle"_vlt, type);
	if (not node) return false; // unknown attribute node

	const address attribute = GetVaultNodeAttribute(node, "CLASS"_vlt, 0);
	if (not attribute) return false; // missing "CLASS" attribute

	switch (AsReference<vault>(attribute + 0x8))
	{
	case     "CAR"_vlt:
	case "TRACTOR"_vlt:
		return true;
	}

	return false;
}



static bool ExtractScene
(
	const Parser::Section& section, 
	Scene&                 scene
) {
	static constexpr std::array keys =
	{
		"car1", "car2", "car3", "car4",
		"car5", "car6", "car7", "car8"
	};

	static_assert(keys.size() == numVehicles);

	for (size_t vehicleID = 0; vehicleID < numVehicles; ++vehicleID)
	{
		std::string_view vehicleName;
		
		if (not Parser::ExtractValues(section, keys[vehicleID], vehicleName)) return false;

		const vault vehicleType = GetVaultHash(vehicleName);
		if (not IsValidVehicleType(vehicleType)) return false;

		scene.vehicles[vehicleID] = vehicleType;
	}

	return true;
}



static bool ExtractScenes(const Parser& parser)
{
	bool anyExtracted = false;

	for (Scene& scene : scenes)
	{
		bool extracted = false;

		if (const auto* const section = parser.GetSection(scene.name))
			extracted = ExtractScene(*section, scene);

		if (not extracted)
			scene.vehicles = AsReference<Vehicles>(0x8EC0F0 + scene.index * sizeof(Vehicles));

		else anyExtracted = true;
	}

	return anyExtracted;
}





// Hook functions -----------------------------------------------------------------------------------------------------------------------------------

HOOK_ORIGINAL(Initialise);

static void __cdecl Initialise
(
	const size_t  numArgs,
	const address argArray
) {
	CALL_HOOK_ORIGINAL(Initialise, numArgs, argArray);

	// Parse configuration file
	std::ifstream fileStream(configFile);
	if (not fileStream.is_open()) return; // no file

	const Parser parser(fileStream, scenes.size(), numVehicles);
	if (not ExtractScenes(parser)) return; // no valid scene(s)

	// Code changes
	PATCH_ASSEMBLY_DETOUR(SceneName);
	PATCH_ASSEMBLY_DETOUR(SceneVehicles);
}





// DLL hook boilerplate -----------------------------------------------------------------------------------------------------------------------------

BOOL WINAPI DllMain
(
	const HINSTANCE hinstDLL,
	const DWORD     fdwReason,
	const LPVOID    lpvReserved
) {
	if (fdwReason != DLL_PROCESS_ATTACH) return TRUE;

	if (MemoryTools::GetEntryPoint() != 0x3C4040) // .exe-dependent entry point
	{
		MessageBoxA(NULL, "This .exe isn't compatible with AntagoNISt.\nSee AntagoNISt's README for help.", "NFSMW AntagoNISt", MB_ICONERROR);

		return FALSE; // should never happen (assuming the user has actually read the README, which... yeah...)
	}

	PATCH_HOOK_FUNCTION(Initialise, 0x6665B4); // InitializeEverything (0x665FC0)

	return TRUE;
}