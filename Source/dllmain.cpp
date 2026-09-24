
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

#include <Windows.h>

#ifdef _DEBUG
#include <debugapi.h>
#endif

#include <array>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <string_view>

#include "Headers/MemoryTools.hpp"
#include "Headers/StreamParser.hpp"
#include "Headers/FlatContainers.hpp"





// Debugging macros ---------------------------------------------------------------------------------------------------------------------------------

// In debug builds, Visual Studio forces an unconditional dynamic allocation for each suitable type.
// This makes dynamic containers (e.g. std::vector, std::string) constinit-incompatible, even if empty.
#ifndef _DEBUG
#define RELEASE_CONSTINIT constinit

#else 
#define RELEASE_CONSTINIT

#endif





// Aliases ------------------------------------------------------------------------------------------------------------------------------------------

using Parser = StreamParser::Parser<>;

using MemoryTools::address;

using MemoryTools::AsReference;
using MemoryTools::AsFunction;

using vault = uint32_t;





// Game types ---------------------------------------------------------------------------------------------------------------------------------------

constexpr size_t numVehicles = 8;

using VehicleTypes = std::array<vault, numVehicles>;

static_assert(sizeof(VehicleTypes) == 32, "Layout mismatch");





// Mod data -----------------------------------------------------------------------------------------------------------------------------------------

constexpr std::array sceneNames =
{
	"IntroNisDD",   "EndingNis03",  "IntroNisBL15", "IntroNisBL14",
	"IntroNisBL13", "IntroNisBL12", "IntroNisBL11", "IntroNisBL10",
	"IntroNisBL09", "IntroNisBL07", "IntroNisBL06", "IntroNisBL05",
	"IntroNisBL04", "IntroNisBL03", "IntroNisBL02", "EndingNis04"
};

const VehicleTypes* replacementTypes = nullptr;

RELEASE_CONSTINIT FlatContainers::Map<std::string_view, VehicleTypes> sceneNameToVehicleTypes;





// Auxiliary functions  -----------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] static const VehicleTypes* __fastcall GetReplacementTypes(const char* const sceneName)
{
	return sceneNameToVehicleTypes.get(std::string_view(sceneName)); // avoids length re-calculations
}





// Assembly detours ---------------------------------------------------------------------------------------------------------------------------------

// Prepares (background) vehicles for currently requested NIS cutscene
ASSEMBLY_DETOUR(SceneVehicles, /* begin = */ 0x6F5607, /* end = */ 0x6F5610)
{
	static constexpr address PrepareVehicles = 0x6F2F60;

	__asm
	{
		push eax

		mov ecx, ebx
		call GetReplacementTypes // ecx: sceneName
		mov dword ptr [replacementTypes], eax

		lea ecx, dword ptr [esi - 0x48]
		call dword ptr [PrepareVehicles]

		mov dword ptr [replacementTypes], 0x0

		EXIT_ASSEMBLY_DETOUR(SceneVehicles)
	}
}



// Selects source for background vehicles in NIS cutscene
ASSEMBLY_DETOUR(VehicleSource, 0x6F30CB, 0x6F30D1)
{
	static constexpr address replacementExit = 0x6F3159;

	__asm
	{
		mov eax, dword ptr [replacementTypes]
		test eax, eax
		jne replacement // replace vehicles

		sub ebx, 3
		cmp ebx, 7

		EXIT_ASSEMBLY_DETOUR(VehicleSource)

		replacement:
		mov dword ptr [esp + 0x80], eax

		jmp dword ptr [replacementExit]
	}
}





// Hashing function ---------------------------------------------------------------------------------------------------------------------------------

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





// Initialisation helpers ---------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] static bool IsValidVehicleType(const vault vehicleType)
{
	const auto GetVaultNode     = AsFunction<address __cdecl    (vault,   vault)>        (0x455FD0);
	const auto GetNodeAttribute = AsFunction<address __thiscall (address, vault, size_t)>(0x454190);

	const address node = GetVaultNode("pvehicle"_vlt, vehicleType);
	if (not node) return false; // unknown attribute node

	const address attribute = GetNodeAttribute(node, "CLASS"_vlt, /* index = */ 0);
	if (not attribute) return false; // missing "CLASS" attribute parameter

	switch (AsReference<vault>(attribute + 0x8))
	{
	case     "CAR"_vlt:
	case "TRACTOR"_vlt:
		return true;
	}

	return false;
}



static bool ExtractVehicleTypes
(
	const Parser::Section* const section, 
	VehicleTypes&                vehicleTypes
) {
	static constexpr std::array keys =
	{
		"car1", "car2", "car3", "car4",
		"car5", "car6", "car7", "car8"
	};

	static_assert(keys.size() == numVehicles, "Key-count mismatch");

	for (size_t vehicleID = 0; vehicleID < numVehicles; ++vehicleID)
	{
		std::string_view vehicleName; // must be non-empty and match VltEd node with compatible vehicle type
		if (not Parser::ExtractValues<std::string_view>(section, keys[vehicleID], vehicleName)) return false;

		const vault vehicleType = GetVaultHash(vehicleName);
		if (not IsValidVehicleType(vehicleType)) return false;

		vehicleTypes[vehicleID] = vehicleType;
	}

	return true;
}



static bool ExtractScenes(const Parser& parser)
{
	sceneNameToVehicleTypes.reserve(sceneNames.size());

	for (const std::string_view sceneName : sceneNames)
	{
		VehicleTypes vehicleTypes = {};

		if (ExtractVehicleTypes(parser.GetSection(sceneName), vehicleTypes))
			sceneNameToVehicleTypes.insert(sceneName, vehicleTypes);
	}

	sceneNameToVehicleTypes.shrink_to_fit();

	return (not sceneNameToVehicleTypes.empty());
}





// Hook functions -----------------------------------------------------------------------------------------------------------------------------------

HOOK_ORIGINAL(Initialise);

static void __cdecl Initialise
(
	const size_t  numArgs,
	const address argArray
) {
	CALL_HOOK_ORIGINAL(Initialise, numArgs, argArray);

	#ifdef _DEBUG
	while (not IsDebuggerPresent()); // halt until debugger is attached
	#endif

	// Parse configuration file
	const std::filesystem::path configFile = "scripts/NFSMWAntagoNIStSettings.ini";

	std::ifstream fileStream(configFile);
	if (not fileStream.is_open()) return; // no file

	const Parser parser(fileStream, sceneNames.size(), numVehicles);
	if (not ExtractScenes(parser)) return; // no valid scene(s)

	// Code changes
	PATCH_ASSEMBLY_DETOUR(SceneVehicles);
	PATCH_ASSEMBLY_DETOUR(VehicleSource);
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