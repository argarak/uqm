//Copyright Paul Reiche, Fred Ford. 1992-2002

/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <assert.h>

#include "save.h"

#include "build.h"
#include "controls.h"
#include "starmap.h"
#include "encount.h"
#include "libs/file.h"
#include "gamestr.h"
#include "globdata.h"
#include "options.h"
#include "races.h"
#include "shipcont.h"
#include "setup.h"
#include "state.h"
#include "grpinfo.h"
#include "util.h"
#include "hyper.h"
		// for SaveSisHyperState()
#include "planets/planets.h"
		// for SaveSolarSysLocation() and tests
#include "libs/inplib.h"
#include "libs/log.h"
#include "libs/memlib.h"

typedef struct savebuf_struct
{
	char *buf;
	size_t bufsize;
	int index, error;
} SAVEBUF;

// XXX: these should handle endian conversions later
static inline COUNT
cwrite_8 (SAVEBUF *fh, BYTE v)
{
	if (!fh)
		return 0;
	if (!fh->error && fh->index < fh->bufsize)
	{
		fh->buf[fh->index++] = v;
		return 1;
	}
	fh->error = 1;
	return 0;
}

static inline COUNT
cwrite_16 (SAVEBUF *fh, UWORD v)
{
	if (cwrite_8 (fh, (BYTE)(v & 0xFF)) &&
	    cwrite_8 (fh, (BYTE)((v >> 8) & 0xFF)))
		return 1;
	return 0;
}

static inline COUNT
cwrite_32 (SAVEBUF *fh, DWORD v)
{
	if (cwrite_8 (fh, (BYTE)(v & 0xFF)) &&
	    cwrite_8 (fh, (BYTE)((v >> 8) & 0xFF)) &&
	    cwrite_8 (fh, (BYTE)((v >> 16) & 0xFF)) &&
	    cwrite_8 (fh, (BYTE)((v >> 24) & 0xFF)))
		return 1;
	return 0;
}

static inline COUNT
cwrite_a8 (SAVEBUF *fh, const BYTE *ar, COUNT count)
{
	int i;
	for (i = 0; i < count; ++i)
		if (!cwrite_8 (fh, ar[i]))
			return 0;
	return 1;
}

static inline size_t
write_8 (void *fp, BYTE v)
{
	return WriteResFile (&v, 1, 1, fp);
}

static inline size_t
write_16 (void *fp, UWORD v)
{
	return WriteResFile (&v, 2, 1, fp);
}

static inline size_t
write_32 (void *fp, DWORD v)
{
	return WriteResFile (&v, 4, 1, fp);
}

static inline size_t
write_a8 (void *fp, const BYTE *ar, COUNT count)
{
	return WriteResFile (ar, 1, count, fp) == count;
}

static inline size_t
write_str (void *fp, const char *str, COUNT count)
{
	// no type conversion needed for strings
	return write_a8 (fp, (const BYTE *)str, count);
}

static inline size_t
write_a16 (void *fp, const UWORD *ar, COUNT count)
{
	for ( ; count > 0; --count, ++ar)
	{
		if (write_16 (fp, *ar) != 1)
			return 0;
	}
	return 1;
}

static void
SaveEmptyQueue (SAVEBUF *fh)
{
	COUNT num_links = 0;

	// Write the number of entries in the queue.
	cwrite_16 (fh, num_links);
}

static void
SaveShipQueue (SAVEBUF *fh, QUEUE *pQueue)
{
	COUNT num_links;
	HSHIPFRAG hStarShip;

	// Write the number of entries in the queue.
	num_links = CountLinks (pQueue);
	cwrite_16 (fh, num_links);

	hStarShip = GetHeadLink (pQueue);
	while (num_links--)
	{
		HSHIPFRAG hNextShip;
		SHIP_FRAGMENT *FragPtr;
		COUNT Index;

		FragPtr = LockShipFrag (pQueue, hStarShip);
		hNextShip = _GetSuccLink (FragPtr);

		Index = FragPtr->race_id;
		// Write the number identifying this ship type.
		// See races.h; look for the enum containing NUM_AVAILABLE_RACES.
		cwrite_16 (fh, Index);

		// Write SHIP_FRAGMENT elements
		cwrite_8  (fh, FragPtr->captains_name_index);
		cwrite_8  (fh, FragPtr->race_id);
		cwrite_8  (fh, FragPtr->index);
		cwrite_16 (fh, FragPtr->crew_level);
		cwrite_16 (fh, FragPtr->max_crew);
		cwrite_8  (fh, FragPtr->energy_level);
		cwrite_8  (fh, FragPtr->max_energy);

		UnlockShipFrag (pQueue, hStarShip);
		hStarShip = hNextShip;
	}
}

static void
SaveRaceQueue (SAVEBUF *fh, QUEUE *pQueue)
{
	COUNT num_links;
	HFLEETINFO hFleet;

	// Write the number of entries in the queue.
	num_links = CountLinks (pQueue);
	cwrite_16 (fh, num_links);

	hFleet = GetHeadLink (pQueue);
	while (num_links--)
	{
		HFLEETINFO hNextFleet;
		FLEET_INFO *FleetPtr;
		COUNT Index;

		FleetPtr = LockFleetInfo (pQueue, hFleet);
		hNextFleet = _GetSuccLink (FleetPtr);

		Index = GetIndexFromStarShip (pQueue, hFleet);
		// The index is the position in the queue.
		cwrite_16 (fh, Index);

		// Write FLEET_INFO elements
		cwrite_16 (fh, FleetPtr->allied_state);
		cwrite_8  (fh, FleetPtr->days_left);
		cwrite_8  (fh, FleetPtr->growth_fract);
		cwrite_16 (fh, FleetPtr->crew_level);
		cwrite_16 (fh, FleetPtr->max_crew);
		cwrite_8  (fh, FleetPtr->growth);
		cwrite_8  (fh, FleetPtr->max_energy);
		cwrite_16 (fh, FleetPtr->loc.x);
		cwrite_16 (fh, FleetPtr->loc.y);

		cwrite_16 (fh, FleetPtr->actual_strength);
		cwrite_16 (fh, FleetPtr->known_strength);
		cwrite_16 (fh, FleetPtr->known_loc.x);
		cwrite_16 (fh, FleetPtr->known_loc.y);
		cwrite_8  (fh, FleetPtr->growth_err_term);
		cwrite_8  (fh, FleetPtr->func_index);
		cwrite_16 (fh, FleetPtr->dest_loc.x);
		cwrite_16 (fh, FleetPtr->dest_loc.y);

		UnlockFleetInfo (pQueue, hFleet);
		hFleet = hNextFleet;
	}
}

static void
SaveGroupQueue (SAVEBUF *fh, QUEUE *pQueue)
{
	HIPGROUP hGroup, hNextGroup;

	// Write the number of entries in the queue.
	cwrite_16 (fh, CountLinks (pQueue));

	for (hGroup = GetHeadLink (pQueue); hGroup; hGroup = hNextGroup)
	{
		IP_GROUP *GroupPtr;

		GroupPtr = LockIpGroup (pQueue, hGroup);
		hNextGroup = _GetSuccLink (GroupPtr);

		cwrite_16 (fh, GroupPtr->group_counter);
		cwrite_8  (fh, GroupPtr->race_id);
		cwrite_8  (fh, GroupPtr->sys_loc);
		cwrite_8  (fh, GroupPtr->task);
		cwrite_8  (fh, GroupPtr->in_system); /* was crew_level */
		cwrite_8  (fh, GroupPtr->dest_loc);
		cwrite_8  (fh, GroupPtr->orbit_pos);
		cwrite_8  (fh, GroupPtr->group_id); /* was max_energy */
		cwrite_16 (fh, GroupPtr->loc.x);
		cwrite_16 (fh, GroupPtr->loc.y);

		UnlockIpGroup (pQueue, hGroup);
	}
}

static void
SaveEncounter (const ENCOUNTER *EncounterPtr, SAVEBUF *fh)
{
	COUNT i;

	cwrite_16  (fh, EncounterPtr->transition_state);
	cwrite_16  (fh, EncounterPtr->origin.x);
	cwrite_16  (fh, EncounterPtr->origin.y);
	cwrite_16  (fh, EncounterPtr->radius);
	// former STAR_DESC fields
	cwrite_16  (fh, EncounterPtr->loc_pt.x);
	cwrite_16  (fh, EncounterPtr->loc_pt.y);
	cwrite_8   (fh, EncounterPtr->race_id);
	cwrite_8   (fh, EncounterPtr->num_ships);
	cwrite_8   (fh, EncounterPtr->flags);

	// Save each entry in the BRIEF_SHIP_INFO array
	for (i = 0; i < MAX_HYPER_SHIPS; i++)
	{
		const BRIEF_SHIP_INFO *ShipInfo = &EncounterPtr->ShipList[i];

		cwrite_8   (fh, ShipInfo->race_id);
		cwrite_16  (fh, ShipInfo->crew_level);
		cwrite_16  (fh, ShipInfo->max_crew);
		cwrite_8   (fh, ShipInfo->max_energy);
	}

	// Save the stuff after the BRIEF_SHIP_INFO array
	cwrite_32  (fh, EncounterPtr->log_x);
	cwrite_32  (fh, EncounterPtr->log_y);
}

static void
SaveEvent (const EVENT *EventPtr, SAVEBUF *fh)
{
	cwrite_8   (fh, EventPtr->day_index);
	cwrite_8   (fh, EventPtr->month_index);
	cwrite_16  (fh, EventPtr->year_index);
	cwrite_8   (fh, EventPtr->func_index);
}

static void
SaveClockState (const CLOCK_STATE *ClockPtr, SAVEBUF *fh)
{
	cwrite_8   (fh, ClockPtr->day_index);
	cwrite_8   (fh, ClockPtr->month_index);
	cwrite_16  (fh, ClockPtr->year_index);
	cwrite_16  (fh, ClockPtr->tick_count);
	cwrite_16  (fh, ClockPtr->day_in_ticks);
}

static void
SaveGameState (const GAME_STATE *GSPtr, SAVEBUF *fh)
{
	cwrite_8   (fh, GSPtr->glob_flags);
	cwrite_8   (fh, GSPtr->CrewCost);
	cwrite_8   (fh, GSPtr->FuelCost);
	cwrite_a8  (fh, GSPtr->ModuleCost, NUM_MODULES);
	cwrite_a8  (fh, GSPtr->ElementWorth, NUM_ELEMENT_CATEGORIES);
	cwrite_16  (fh, GSPtr->CurrentActivity);

	SaveClockState (&GSPtr->GameClock, fh);

	cwrite_16  (fh, GSPtr->autopilot.x);
	cwrite_16  (fh, GSPtr->autopilot.y);
	cwrite_16  (fh, GSPtr->ip_location.x);
	cwrite_16  (fh, GSPtr->ip_location.y);
	/* STAMP ShipStamp */
	cwrite_16  (fh, GSPtr->ShipStamp.origin.x);
	cwrite_16  (fh, GSPtr->ShipStamp.origin.y);
	cwrite_16  (fh, GSPtr->ShipFacing);
	cwrite_8   (fh, GSPtr->ip_planet);
	cwrite_8   (fh, GSPtr->in_orbit);

	/* VELOCITY_DESC velocity */
	cwrite_16  (fh, GSPtr->velocity.TravelAngle);
	cwrite_16  (fh, GSPtr->velocity.vector.width);
	cwrite_16  (fh, GSPtr->velocity.vector.height);
	cwrite_16  (fh, GSPtr->velocity.fract.width);
	cwrite_16  (fh, GSPtr->velocity.fract.height);
	cwrite_16  (fh, GSPtr->velocity.error.width);
	cwrite_16  (fh, GSPtr->velocity.error.height);
	cwrite_16  (fh, GSPtr->velocity.incr.width);
	cwrite_16  (fh, GSPtr->velocity.incr.height);

	cwrite_32  (fh, GSPtr->BattleGroupRef);

	cwrite_a8  (fh, GSPtr->GameState, sizeof (GSPtr->GameState));
}

static BOOLEAN
SaveSisState (const SIS_STATE *SSPtr, void *fp)
{
	if (
			write_32  (fp, SSPtr->log_x) != 1 ||
			write_32  (fp, SSPtr->log_y) != 1 ||
			write_32  (fp, SSPtr->ResUnits) != 1 ||
			write_32  (fp, SSPtr->FuelOnBoard) != 1 ||
			write_16  (fp, SSPtr->CrewEnlisted) != 1 ||
			write_16  (fp, SSPtr->TotalElementMass) != 1 ||
			write_16  (fp, SSPtr->TotalBioMass) != 1 ||
			write_a8  (fp, SSPtr->ModuleSlots, NUM_MODULE_SLOTS) != 1 ||
			write_a8  (fp, SSPtr->DriveSlots, NUM_DRIVE_SLOTS) != 1 ||
			write_a8  (fp, SSPtr->JetSlots, NUM_JET_SLOTS) != 1 ||
			write_8   (fp, SSPtr->NumLanders) != 1 ||
			write_a16 (fp, SSPtr->ElementAmounts, NUM_ELEMENT_CATEGORIES) != 1 ||

			write_str (fp, SSPtr->ShipName, SIS_NAME_SIZE) != 1 ||
			write_str (fp, SSPtr->CommanderName, SIS_NAME_SIZE) != 1 ||
			write_str (fp, SSPtr->PlanetName, SIS_NAME_SIZE) != 1
		)
		return FALSE;
	else
		return TRUE;
}

static BOOLEAN
SaveSummary (const SUMMARY_DESC *SummPtr, void *fp)
{
	if (
			write_32 (fp, SAVEFILE_TAG) != 1 ||
			write_32 (fp, SUMMARY_TAG) != 1 ||
			write_32 (fp, 160 + strlen(SummPtr->SaveName)) != 1
		)
		return FALSE;
	if (!SaveSisState (&SummPtr->SS, fp))
		return FALSE;

	if (
			write_8  (fp, SummPtr->Activity) != 1 ||
			write_8  (fp, SummPtr->Flags) != 1 ||
			write_8  (fp, SummPtr->day_index) != 1 ||
			write_8  (fp, SummPtr->month_index) != 1 ||
			write_16 (fp, SummPtr->year_index) != 1 ||
			write_8  (fp, SummPtr->MCreditLo) != 1 ||
			write_8  (fp, SummPtr->MCreditHi) != 1 ||
			write_8  (fp, SummPtr->NumShips) != 1 ||
			write_8  (fp, SummPtr->NumDevices) != 1 ||
			write_a8 (fp, SummPtr->ShipList, MAX_BUILT_SHIPS) != 1 ||
			write_a8 (fp, SummPtr->DeviceList, MAX_EXCLUSIVE_DEVICES) != 1 ||
			write_a8 (fp, SummPtr->SaveName, strlen(SummPtr->SaveName)) != 1
		)
		return FALSE;
	else
		return TRUE;
}

static void
SaveStarDesc (const STAR_DESC *SDPtr, SAVEBUF *fh)
{
	cwrite_16 (fh, SDPtr->star_pt.x);
	cwrite_16 (fh, SDPtr->star_pt.y);
	cwrite_8  (fh, SDPtr->Type);
	cwrite_8  (fh, SDPtr->Index);
	cwrite_8  (fh, SDPtr->Prefix);
	cwrite_8  (fh, SDPtr->Postfix);
}

static void
PrepareSummary (SUMMARY_DESC *SummPtr, const char *name)
{
	SummPtr->SS = GlobData.SIS_state;

	SummPtr->Activity = LOBYTE (GLOBAL (CurrentActivity));
	switch (SummPtr->Activity)
	{
		case IN_HYPERSPACE:
			if (inQuasiSpace ())
				SummPtr->Activity = IN_QUASISPACE;
			break;
		case IN_INTERPLANETARY:
			// Get a better planet name for summary
			GetPlanetOrMoonName (SummPtr->SS.PlanetName,
					sizeof (SummPtr->SS.PlanetName));
			if (GET_GAME_STATE (GLOBAL_FLAGS_AND_DATA) == (BYTE)~0)
				SummPtr->Activity = IN_STARBASE;
			else if (playerInPlanetOrbit ())
				SummPtr->Activity = IN_PLANET_ORBIT;
			break;
		case IN_LAST_BATTLE:
			utf8StringCopy (SummPtr->SS.PlanetName,
					sizeof (SummPtr->SS.PlanetName),
					GAME_STRING (PLANET_NUMBER_BASE + 32)); // Sa-Matra
			break;
	}

	SummPtr->MCreditLo = GET_GAME_STATE (MELNORME_CREDIT0);
	SummPtr->MCreditHi = GET_GAME_STATE (MELNORME_CREDIT1);

	{
		HSHIPFRAG hStarShip, hNextShip;

		for (hStarShip = GetHeadLink (&GLOBAL (built_ship_q)), SummPtr->NumShips = 0;
				hStarShip; hStarShip = hNextShip, ++SummPtr->NumShips)
		{
			SHIP_FRAGMENT *StarShipPtr;

			StarShipPtr = LockShipFrag (&GLOBAL (built_ship_q), hStarShip);
			hNextShip = _GetSuccLink (StarShipPtr);
			SummPtr->ShipList[SummPtr->NumShips] = StarShipPtr->race_id;
			UnlockShipFrag (&GLOBAL (built_ship_q), hStarShip);
		}
	}

	SummPtr->NumDevices = InventoryDevices (SummPtr->DeviceList,
			MAX_EXCLUSIVE_DEVICES);

	SummPtr->Flags = GET_GAME_STATE (LANDER_SHIELDS)
			| (GET_GAME_STATE (IMPROVED_LANDER_SPEED) << (4 + 0))
			| (GET_GAME_STATE (IMPROVED_LANDER_CARGO) << (4 + 1))
			| (GET_GAME_STATE (IMPROVED_LANDER_SHOT) << (4 + 2))
			| ((GET_GAME_STATE (CHMMR_BOMB_STATE) < 2 ? 0 : 1) << (4 + 3));

	SummPtr->day_index = GLOBAL (GameClock.day_index);
	SummPtr->month_index = GLOBAL (GameClock.month_index);
	SummPtr->year_index = GLOBAL (GameClock.year_index);
	SummPtr->SaveName[SAVE_NAME_SIZE-1] = 0;
	strncpy (SummPtr->SaveName, name, SAVE_NAME_SIZE-1);
}

static void
SaveProblemMessage (STAMP *MsgStamp)
{
#define MAX_MSG_LINES 1
	RECT r = {{0, 0}, {0, 0}};
	COUNT i;
	TEXT t;
	UNICODE *ppStr[MAX_MSG_LINES];

	// TODO: This should probably just use DoPopupWindow()

	ppStr[0] = GAME_STRING (SAVEGAME_STRING_BASE + 2);

	SetContextFont (StarConFont);

	t.baseline.x = t.baseline.y = 0;
	t.align = ALIGN_CENTER;
	for (i = 0; i < MAX_MSG_LINES; ++i)
	{
		RECT tr;

		t.pStr = ppStr[i];
		if (*t.pStr == '\0')
			break;
		t.CharCount = (COUNT)~0;
		TextRect (&t, &tr, NULL);
		if (i == 0)
			r = tr;
		else
			BoxUnion (&tr, &r, &r);
		t.baseline.y += 11;
	}
	t.baseline.x = ((SIS_SCREEN_WIDTH >> 1) - (r.extent.width >> 1))
			- r.corner.x;
	t.baseline.y = ((SIS_SCREEN_HEIGHT >> 1) - (r.extent.height >> 1))
			- r.corner.y;
	r.corner.x += t.baseline.x - 4;
	r.corner.y += t.baseline.y - 4;
	r.extent.width += 8;
	r.extent.height += 8;

	*MsgStamp = SaveContextFrame (&r);

	BatchGraphics ();
	DrawStarConBox (&r, 2,
			BUILD_COLOR (MAKE_RGB15 (0x10, 0x10, 0x10), 0x19),
			BUILD_COLOR (MAKE_RGB15 (0x08, 0x08, 0x08), 0x1F),
			TRUE, BUILD_COLOR (MAKE_RGB15 (0x0A, 0x0A, 0x0A), 0x08));
	SetContextForeGroundColor (
			BUILD_COLOR (MAKE_RGB15 (0x14, 0x14, 0x14), 0x0F));

	for (i = 0; i < MAX_MSG_LINES; ++i)
	{
		t.pStr = ppStr[i];
		if (*t.pStr == '\0')
			break;
		t.CharCount = (COUNT)~0;
		font_DrawText (&t);
		t.baseline.y += 11;
	}
	UnbatchGraphics ();
}

void
SaveProblem (void)
{
	STAMP s;
	CONTEXT OldContext;

	OldContext = SetContext (SpaceContext);
	SaveProblemMessage (&s);
	FlushGraphics ();

	WaitForAnyButton (TRUE, WAIT_INFINITE, FALSE);

	// Restore the screen under the message
	DrawStamp (&s);
	SetContext (OldContext);
	DestroyDrawable (ReleaseDrawable (s.frame));
}

static void
SaveFlagshipState (void)
{
	if (inHQSpace ())
	{
		// Player is in HyperSpace or QuasiSpace.
		SaveSisHyperState ();
	}
	else if (playerInSolarSystem ())
	{
		SaveSolarSysLocation ();
	}
}

// This function first writes to a memory file, and then writes the whole
// lot to the actual save file at once.
BOOLEAN
SaveGame (COUNT which_game, SUMMARY_DESC *SummPtr, const char *name)
{
	BOOLEAN success;
	void *out_fp, *h;
	SAVEBUF fh_backing, *fh;

	success = TRUE;
	fh = &fh_backing;
	h = HMalloc (128 * 1024);
	if (h == 0)
	{
		log_add (log_Debug, "Insufficient room for save buffers");
	}
	else
	{
		GAME_STATE_FILE *fp;
		DWORD flen;
		COUNT num_links;
		POINT pt;
		STAR_DESC SD;
		char buf[256], file[PATH_MAX];

		fh->buf = h;
		fh->bufsize = 128 * 1024;
		fh->index = 0;
		fh->error = 0;
		success = TRUE;
		if (CurStarDescPtr)
			SD = *CurStarDescPtr;
		else
			memset (&SD, 0, sizeof (SD));

		// XXX: Backup: SaveFlagshipState() overwrites ip_location
		pt = GLOBAL (ip_location);
		SaveFlagshipState ();
		if (LOBYTE (GLOBAL (CurrentActivity)) == IN_INTERPLANETARY
				&& !(GLOBAL (CurrentActivity)
				& (START_ENCOUNTER | START_INTERPLANETARY)))
			PutGroupInfo (GROUPS_RANDOM, GROUP_SAVE_IP);

		SaveGameState (&GlobData.Game_state, fh);

		// XXX: Restore
		GLOBAL (ip_location) = pt;
		// Only relevant when loading a game and must be cleaned
		GLOBAL (in_orbit) = 0;

		SaveRaceQueue (fh, &GLOBAL (avail_race_q));
		// START_INTERPLANETARY is only set when saving from Homeworld
		//   encounter screen. When the game is loaded, the
		//   GenerateOrbitalFunction for the current star system
		//   create the encounter anew and populate the npc queue.
		if (!(GLOBAL (CurrentActivity) & START_INTERPLANETARY))
		{
			if (GLOBAL (CurrentActivity) & START_ENCOUNTER)
				SaveShipQueue (fh, &GLOBAL (npc_built_ship_q));
			else if (LOBYTE (GLOBAL (CurrentActivity)) == IN_INTERPLANETARY)
				// XXX: Technically, this queue does not need to be
				//   saved/loaded at all. IP groups will be reloaded
				//   from group state files. But the original code did,
				//   and so will we until we can prove we do not need to.
				SaveGroupQueue (fh, &GLOBAL (ip_group_q));
			else
				// XXX: empty queue write-out is only needed to maintain
				//   the savegame compatibility
				SaveEmptyQueue (fh);
		}
		SaveShipQueue (fh, &GLOBAL (built_ship_q));

		// Save the number of game events (compressed).
		num_links = CountLinks (&GLOBAL (GameClock.event_q));
		cwrite_16 (fh, num_links);
		// Save the game events themselves (compressed):
		{
			HEVENT hEvent;

			hEvent = GetHeadLink (&GLOBAL (GameClock.event_q));
			while (num_links--)
			{
				HEVENT hNextEvent;
				EVENT *EventPtr;

				LockEvent (hEvent, &EventPtr);
				hNextEvent = GetSuccEvent (EventPtr);

				SaveEvent (EventPtr, fh);

				UnlockEvent (hEvent);
				hEvent = hNextEvent;
			}
		}

		// Save the number of encounters (black globes in HS/QS (compressed))
		num_links = CountLinks (&GLOBAL (encounter_q));
		// Save the encounters themselves (compressed):
		cwrite_16 (fh, num_links);
		{
			HENCOUNTER hEncounter;

			hEncounter = GetHeadLink (&GLOBAL (encounter_q));
			while (num_links--)
			{
				HENCOUNTER hNextEncounter;
				ENCOUNTER *EncounterPtr;

				LockEncounter (hEncounter, &EncounterPtr);
				hNextEncounter = GetSuccEncounter (EncounterPtr);

				SaveEncounter (EncounterPtr, fh);

				UnlockEncounter (hEncounter);
				hEncounter = hNextEncounter;
			}
		}

		// Copy the star info file to the memory file (compressed).
		fp = OpenStateFile (STARINFO_FILE, "rb");
		if (fp)
		{
			flen = LengthStateFile (fp);
			// Write the uncompressed size.
			cwrite_32 (fh, flen);
			while (flen)
			{
				COUNT num_bytes;

				num_bytes = flen >= sizeof (buf) ? sizeof (buf) : (COUNT)flen;
				ReadStateFile (buf, num_bytes, 1, fp);
				cwrite_a8 (fh, buf, num_bytes);

				flen -= num_bytes;
			}
			CloseStateFile (fp);
		}

		// Copy the defined groupinfo file into the memory file (compressed)
		fp = OpenStateFile (DEFGRPINFO_FILE, "rb");
		if (fp)
		{
			flen = LengthStateFile (fp);
			// Write the uncompressed size.
			cwrite_32 (fh, flen);
			while (flen)
			{
				COUNT num_bytes;

				num_bytes = flen >= sizeof (buf) ? sizeof (buf) : (COUNT)flen;
				ReadStateFile (buf, num_bytes, 1, fp);
				cwrite_a8 (fh, buf, num_bytes);

				flen -= num_bytes;
			}
			CloseStateFile (fp);
		}

		// Copy the random groupinfo file into the memory file (compressed)
		fp = OpenStateFile (RANDGRPINFO_FILE, "rb");
		if (fp)
		{
			flen = LengthStateFile (fp);
			// Write the uncompressed size.
			cwrite_32 (fh, flen);
			while (flen)
			{
				COUNT num_bytes;

				num_bytes = flen >= sizeof (buf) ? sizeof (buf) : (COUNT)flen;
				ReadStateFile (buf, num_bytes, 1, fp);
				cwrite_a8 (fh, buf, num_bytes);

				flen -= num_bytes;
			}
			CloseStateFile (fp);
		}

		// Write the current star desc into the memory file (compressed).
		SaveStarDesc (&SD, fh);

		flen = fh->index;

		// Write the memory file to the actual savegame file.
		sprintf (file, "uqmsave.%02u", which_game);
		log_add (log_Debug, "'%s' is %u bytes long", file,
				 flen + 180 + strlen(SummPtr->SaveName));
		if (flen && (out_fp = res_OpenResFile (saveDir, file, "wb")))
		{
			PrepareSummary (SummPtr, name);

			success = SaveSummary (SummPtr, out_fp);
			// Then write the rest of the data.
			if (success && write_32 (out_fp, OMNIBUS_TAG) != 1)
				success = FALSE;
			if (success && write_32 (out_fp, flen) != 1)
				success = FALSE;
			if (success && WriteResFile (h, flen, 1, out_fp) != 1)
				success = FALSE;

			if (res_CloseResFile ((uio_Stream *)out_fp) == 0)
				success = FALSE;

		}
		else
			success = FALSE;

		if (!success)
			DeleteResFile (saveDir, file);
	}

	HFree (h);

	return (success);
}
