/*  The whole game database, through the real parser.
 *
 *  Companion to dbparse.cpp, which covers the patch lines. This covers
 *  everything else in bin/resources/GameIndex.yaml: the game fixes, speed
 *  hacks, GS hardware fixes, rounding and clamping modes and the memory
 *  card filters.
 *
 *  The failure it exists to catch is the same one that hid the "bytes"
 *  operand size. A name the parser does not recognise is reported with a
 *  console error and then dropped -- the entry loses its fix and the game
 *  quietly misbehaves, with nothing failing and nothing to notice. A
 *  rename on either side of the database, or a fix removed from the enum
 *  while entries still ask for it, looks exactly like that.
 *
 *  So this parses every entry and requires no errors and no warnings. It
 *  links pcsx2/GameDatabase.cpp and the real YAML reader rather than
 *  reimplementing either.
 *
 *  What it catches, checked by corrupting the database and re-running:
 *  renaming a gamefix so the parser no longer knows it -- "XGKickHackk"
 *  -- is reported and fails the run. That is the WRC-class failure, a
 *  name in the database that the code has stopped recognising.
 *
 *  What it does not catch, and both are worth knowing:
 *    - a renamed key. Changing "speedHacks:" to "speedHackss:" makes the
 *      whole block invisible and the parser says nothing, because an
 *      unrecognised key is skipped rather than reported. Every entry
 *      under it is silently lost.
 *    - an out-of-range value. "autoFlush: 99" loads without complaint;
 *      the numeric gsHWFixes are range-checked nowhere.
 *
 *  Both are real silent failures this does not close. Catching them means
 *  the parser reporting unknown keys and validating ranges, which is a
 *  change to GameDatabase.cpp rather than to a test.
 *
 *  nativeScaling and halfPixelOffset are the exception: the database
 *  numbers them as PCSX2 does, which is not the core's enum order, so
 *  check_gs_mode_numbering() applies each value the database uses and
 *  compares the result with the mode that value names.
 *
 *  Usage: tests/patch/dbload <path-to-GameIndex.yaml>
 */

#include <stdio.h>
#include <libretro.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <string>
#include <vector>

#include "Common.h"
#include "GameDatabase.h"

static int g_errors   = 0;
static int g_warnings = 0;
static int g_shown    = 0;

static void note(const char* kind, const char* fmt, va_list a)
{
	if (g_shown++ < 8)
	{
		printf("  %s: ", kind);
		vprintf(fmt, a);
		printf("\n");
	}
}

static void test_log(enum retro_log_level level, const char* fmt, ...)
{
	va_list a;
	if (level != RETRO_LOG_ERROR && level != RETRO_LOG_WARN)
		return;
	va_start(a, fmt);
	if (level == RETRO_LOG_ERROR) { g_errors++;   note("error", fmt, a); }
	else                          { g_warnings++; note("warning", fmt, a); }
	va_end(a);
}
extern "C" retro_log_printf_t log_cb = test_log;

/* The database reads its yaml through this. */
static std::string s_yaml;
namespace Host
{
	std::optional<std::string> ReadResourceFileToString(const char* name)
	{
		(void)name;
		return s_yaml;
	}
}

/* The three gsHWFixes fields whose values are function names --
 * getSkipCount, beforeDraw and moveHandler -- are looked up in tables that
 * live in GSHwHack.cpp, and linking that pulls in the GS renderer: sixty
 * more symbols for three fields. They are stubbed to a valid id here, so
 * this test does not validate those names. That leaves 349 lines in the
 * database unchecked, and they are the ones most likely to break on a GS
 * refactor, so it is worth knowing they are not covered rather than
 * assuming a green run includes them. */
std::optional<int> GSLookupGetSkipCountFunctionId(const std::string_view&)
{ return 0; }
std::optional<int> GSLookupBeforeDrawFunctionId(const std::string_view&)
{ return 0; }
std::optional<int> GSLookupMoveHandlerFunctionId(const std::string_view&)
{ return 0; }

/* Memory card naming, reached for the memcardFilters field. */
void FileMcd_GetDefaultName(char* buf, size_t n, unsigned) { if (n) buf[0] = 0; }
unsigned FileMcd_GetMtapPort(unsigned) { return 0; }
unsigned FileMcd_GetMtapSlot(unsigned) { return 0; }
bool FileMcd_IsMultitapSlot(unsigned) { return false; }

/* Apply one GS hardware fix to a default config, as a game entry would. */
static Pcsx2Config::GSOptions apply_fix(GameDatabaseSchema::GSHWFixId id, int value)
{
	GameDatabaseSchema::GameEntry entry;
	Pcsx2Config::GSOptions gs;
	entry.gsHWFixes.push_back(std::make_pair(id, (s32)value));
	entry.applyGSHardwareFixes(gs);
	return gs;
}

static void expect(bool ok, const char* what, int value)
{
	if (ok)
		return;
	printf("  %s %d: not the expected result\n", what, value);
	g_errors++;
}

/* The database's numbers are PCSX2's: nativeScaling 0 Off, 1 Normal,
 * 2 Aggressive, 3/4 the same two keeping the upscale; halfPixelOffset 0-5
 * as the core, 5 being Native with texture offset. Each value must map to
 * exactly the mode it names. The core orders its own GSNativeScaling
 * Normal, Aggressive, Off, so a cast would swap Aggressive for Off. Values
 * outside the table must change nothing. */
static void check_gs_mode_numbering(void)
{
	using GameDatabaseSchema::GSHWFixId;
	static const struct { int value; GSNativeScaling mode; } kScaling[] = {
		{0, NativeScaling_Off}, {1, NativeScaling_Normal}, {2, NativeScaling_Aggressive},
		{3, NativeScaling_NormalUpscaled}, {4, NativeScaling_AggressiveUpscaled},
	};
	static const struct { int value; GSHalfPixelOffset mode; } kOffset[] = {
		{0, GSHalfPixelOffset::Off}, {1, GSHalfPixelOffset::Normal},
		{2, GSHalfPixelOffset::Special}, {3, GSHalfPixelOffset::SpecialAggressive},
		{4, GSHalfPixelOffset::Native}, {5, GSHalfPixelOffset::NativeWTexOffset},
	};
	const Pcsx2Config::GSOptions defaults;
	size_t i;

	for (i = 0; i < sizeof(kScaling) / sizeof(kScaling[0]); i++)
	{
		Pcsx2Config::GSOptions gs = apply_fix(GSHWFixId::NativeScaling, kScaling[i].value);
		expect(gs.UserHacks_NativeScaling == kScaling[i].mode, "nativeScaling", kScaling[i].value);
	}
	for (i = 0; i < sizeof(kOffset) / sizeof(kOffset[0]); i++)
	{
		Pcsx2Config::GSOptions gs = apply_fix(GSHWFixId::HalfPixelOffset, kOffset[i].value);
		expect(gs.UserHacks_HalfPixelOffset == kOffset[i].mode, "halfPixelOffset", kOffset[i].value);
	}

	/* The two tables cover every mode: none is left without a database value
	 * to name it (and none is named twice). */
	expect(sizeof(kScaling) / sizeof(kScaling[0]) == (size_t)NativeScaling_MaxCount, "nativeScaling modes", (int)NativeScaling_MaxCount);
	expect(sizeof(kOffset) / sizeof(kOffset[0]) == (size_t)GSHalfPixelOffset::MaxCount, "halfPixelOffset modes", (int)GSHalfPixelOffset::MaxCount);
	for (i = 0; i < sizeof(kScaling) / sizeof(kScaling[0]); i++)
	{
		for (size_t j = i + 1; j < sizeof(kScaling) / sizeof(kScaling[0]); j++)
			expect(kScaling[i].mode != kScaling[j].mode, "nativeScaling mode named twice", kScaling[j].value);
	}

	/* The user's own native scaling option stands over the database. */
	{
		GameDatabaseSchema::GameEntry entry;
		Pcsx2Config::GSOptions gs;
		gs.NativeScalingSet = true;
		gs.UserHacks_NativeScaling = NativeScaling_Off;
		entry.gsHWFixes.push_back(std::make_pair(GSHWFixId::NativeScaling, (s32)2));
		entry.applyGSHardwareFixes(gs);
		expect(gs.UserHacks_NativeScaling == NativeScaling_Off, "nativeScaling over a user option", 2);
	}

	/* Out of range: ignored, and logged as a warning (counted by main). */
	{
		const int before = g_warnings;
		const int shown = g_shown;
		g_shown = 8; /* these warnings are expected; keep them off the log */
		Pcsx2Config::GSOptions a = apply_fix(GSHWFixId::NativeScaling, 5);
		Pcsx2Config::GSOptions b = apply_fix(GSHWFixId::HalfPixelOffset, 6);
		Pcsx2Config::GSOptions c = apply_fix(GSHWFixId::NativeScaling, -1);
		expect(a.UserHacks_NativeScaling == defaults.UserHacks_NativeScaling, "nativeScaling", 5);
		expect(b.UserHacks_HalfPixelOffset == defaults.UserHacks_HalfPixelOffset, "halfPixelOffset", 6);
		expect(c.UserHacks_NativeScaling == defaults.UserHacks_NativeScaling, "nativeScaling", -1);
		expect(g_warnings - before == 3, "out-of-range warnings", g_warnings - before);
		g_warnings = before;
		g_shown = shown;
	}

	/* The "already satisfied" test must agree with what applying does. */
	for (i = 0; i < sizeof(kScaling) / sizeof(kScaling[0]); i++)
	{
		Pcsx2Config::GSOptions gs = apply_fix(GSHWFixId::NativeScaling, kScaling[i].value);
		expect(GameDatabaseSchema::GameEntry::configMatchesHWFix(gs, GSHWFixId::NativeScaling, kScaling[i].value),
		       "nativeScaling match", kScaling[i].value);
	}
	for (i = 0; i < sizeof(kOffset) / sizeof(kOffset[0]); i++)
	{
		Pcsx2Config::GSOptions gs;
		gs.UpscaleMultiplier = 2.0f;
		gs.UserHacks_HalfPixelOffset = kOffset[i].mode;
		expect(GameDatabaseSchema::GameEntry::configMatchesHWFix(gs, GSHWFixId::HalfPixelOffset, kOffset[i].value),
		       "halfPixelOffset match", kOffset[i].value);
	}
}

/* Every nativeScaling and halfPixelOffset value the database uses must be
 * one the core translates; applying it warns otherwise. */
static void check_database_modes(void)
{
	static const struct { const char* key; GameDatabaseSchema::GSHWFixId id; } kKeys[] = {
		{"    nativeScaling: ", GameDatabaseSchema::GSHWFixId::NativeScaling},
		{"    halfPixelOffset: ", GameDatabaseSchema::GSHWFixId::HalfPixelOffset},
	};
	size_t k;
	for (k = 0; k < sizeof(kKeys) / sizeof(kKeys[0]); k++)
	{
		size_t pos = 0;
		const size_t keylen = strlen(kKeys[k].key);
		bool seen[16] = {};
		while ((pos = s_yaml.find(kKeys[k].key, pos)) != std::string::npos)
		{
			const int value = atoi(s_yaml.c_str() + pos + keylen);
			pos += keylen;
			if (value >= 0 && value < 16 && !seen[value])
			{
				seen[value] = true;
				apply_fix(kKeys[k].id, value);
			}
			else if (value < 0 || value >= 16)
				g_errors++;
		}
	}
}

int main(int argc, char** argv)
{
	const char* path = (argc > 1) ? argv[1] : "bin/resources/GameIndex.yaml";
	FILE* f = fopen(path, "rb");
	long size;
	int entries = 0;

	if (!f) { fprintf(stderr, "cannot open %s\n", path); return 2; }
	fseek(f, 0, SEEK_END); size = ftell(f); fseek(f, 0, SEEK_SET);
	s_yaml.resize((size_t)size);
	if (fread(&s_yaml[0], 1, (size_t)size, f) != (size_t)size)
	{ fclose(f); fprintf(stderr, "short read\n"); return 2; }
	fclose(f);

	GameDatabase::ensureLoaded();

	/* Spot-check that entries actually came out, so an empty parse cannot
	 * pass by reporting nothing. WRC 4's serial is the one this suite grew
	 * out of; the other two are an ordinary retail entry and one carrying
	 * a gsHWFixes block. */
	{
		static const char* kSerials[] = {
			"SCES-52389",   /* WRC 4, gameFixes + patches */
			"SLUS-20062",   /* a plain entry */
			"SCED-52880",   /* WRC 4 demo, gsHWFixes + roundModes */
		};
		size_t i;
		for (i = 0; i < sizeof(kSerials)/sizeof(kSerials[0]); i++)
		{
			const GameDatabaseSchema::GameEntry* e =
				GameDatabase::findGame(kSerials[i]);
			if (e) entries++;
			else
			{
				printf("  %s is not in the loaded database\n", kSerials[i]);
				g_errors++;
			}
		}
	}

	check_gs_mode_numbering();
	check_database_modes();

	printf("dbload: %d errors, %d warnings, %d/3 spot-checked serials found\n",
	       g_errors, g_warnings, entries);
	return (g_errors != 0 || g_warnings != 0 || entries != 3);
}

std::string libretro_content;


/* Reached from Pcsx2Config's paths and the memory map; neither is entered
 * by a database load. */
extern "C" {
	/* path_is_valid and path_mkdir are the two file_path.c leaves out of
	 * this build; the rest of that file links. */
	int path_is_valid(const char*) { return 0; }
	int path_mkdir(const char*) { return 0; }
}
void vtlb_Alloc_Ppmap(void) { }
