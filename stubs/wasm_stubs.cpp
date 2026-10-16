// Replacements for platform pieces that do not exist in the browser build.
#include "precompiled.h"

#include "lib/sysdep/cpu.h"
#include "lobby/IXmppClient.h"
#include "lobby/scripting/JSInterface_Lobby.h"
#include "ps/GameSetup/Atlas.h"
#include "ps/DllLoader.h"
#include "renderer/backend/IDevice.h"
#include "renderer/backend/vulkan/DeviceForward.h"
#include "tools/atlas/GameInterface/GameLoop.h"

// ---- CPU helpers (single-threaded wasm, but use atomics so a pthread build stays correct) ----
bool cpu_CAS(volatile intptr_t* location, intptr_t expected, intptr_t newValue)
{
	return __atomic_compare_exchange_n(location, &expected, newValue, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

bool cpu_CAS64(volatile i64* location, i64 expected, i64 newValue)
{
	return __atomic_compare_exchange_n(location, &expected, newValue, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

const char* cpu_IdentifierString()
{
	return "WebAssembly";
}

// ---- X11 ----
extern "C" int XInitThreads() { return 1; }

// ---- Vulkan: not available in browsers ----
namespace Renderer { namespace Backend { namespace Vulkan {
std::unique_ptr<IDevice> CreateDevice(SDL_Window*) { return nullptr; }
}}}

// ---- Lobby (disabled) ----
IXmppClient* g_XmppClient = nullptr;
bool g_rankedGame = false;
namespace JSI_Lobby { void RegisterScriptFunctions(const ScriptRequest&) {} }

// ---- Atlas editor (not part of the browser build) ----
GameLoopState* g_AtlasGameLoop = nullptr;
bool BeginAtlas(const CmdLineArgs&, const DllLoader&) { return false; }

// ---- wide printf: musl does not understand the MS-style %hs / %hc (narrow string / char in a wide format) ----
#include <wchar.h>
#include <stdarg.h>
#include <string>
extern "C" int __real_vswprintf(wchar_t* s, size_t n, const wchar_t* fmt, va_list ap);
extern "C" int __wrap_vswprintf(wchar_t* s, size_t n, const wchar_t* fmt, va_list ap)
{
	std::wstring f;
	for (const wchar_t* p = fmt; *p;)
	{
		if (*p != L'%') { f.push_back(*p++); continue; }
		f.push_back(*p++);
		if (*p == L'%') { f.push_back(*p++); continue; }
		// flags, width, precision
		while (*p && wcschr(L"-+ #0123456789.*$", *p)) f.push_back(*p++);
		// length modifier: drop a lone 'h' in front of s/c (the standard says %s/%c are already narrow here)
		if (p[0] == L'h' && (p[1] == L's' || p[1] == L'c')) { ++p; continue; }
		while (*p && wcschr(L"hlLjzt", *p)) f.push_back(*p++);
		if (*p) f.push_back(*p++);
	}
	return __real_vswprintf(s, n, f.c_str(), ap);
}
