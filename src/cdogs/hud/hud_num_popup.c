/*
    C-Dogs SDL
    A port of the legendary (and fun) action/arcade cdogs.

    Copyright (c) 2013-2017, 2019 Cong Xu
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    Redistributions of source code must retain the above copyright notice, this
    list of conditions and the following disclaimer.
    Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
    ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
    LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
    SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
    CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
    ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.
*/
#include "hud_num_popup.h"

#include "actors.h"
#include "font.h"
#include "gamedata.h"
#include "hud_defs.h"


// Total number of milliseconds that the numeric popup lasts for
#define TIMER_MS 1000
#define TIMER_OBJECTIVE_MS 2000


void HUDNumPopupsInit(
	HUDNumPopups *popups, const struct MissionOptions *mission)
{
	memset(popups, 0, sizeof *popups);
	CArrayInit(&popups->objective, sizeof(HUDNumPopup));
	CArrayResize(
		&popups->objective, mission->missionData->Objectives.size, NULL);
	CArrayFillZero(&popups->objective);
}

void HUDNumPopupsTerminate(HUDNumPopups *popups)
{
	CArrayTerminate(&popups->objective);
}

static void MergePopups(HUDNumPopup *dst, const HUDNumPopup src);
void HUDNumPopupsAdd(
	HUDNumPopups *popups, const HUDNumPopupType type,
	const int idxOrUID, const int amount)
{
	HUDNumPopup s;
	memset(&s, 0, sizeof s);

	// Index
	int localPlayerIdx = -1;
	switch (type)
	{
	case NUMBER_POPUP_SCORE:
		localPlayerIdx = FindLocalPlayerIndex(idxOrUID);
		if (localPlayerIdx < 0)
		{
			// This popup was for a non-local player; abort
			return;
		}
		s.u.PlayerUID = idxOrUID;
		break;
	case NUMBER_POPUP_OBJECTIVE:
		s.u.ObjectiveIndex = idxOrUID;
		break;
	default:
		CASSERT(false, "unknown HUD popup type");
		break;
	}

	s.Amount = amount;

	// Timers
	switch (type)
	{
	case NUMBER_POPUP_SCORE:
		s.Timer = TIMER_MS;
		s.TimerMax = TIMER_MS;
		break;
	case NUMBER_POPUP_OBJECTIVE:
		s.Timer = TIMER_OBJECTIVE_MS;
		s.TimerMax = TIMER_OBJECTIVE_MS;
		break;
	default:
		CASSERT(false, "unknown HUD popup type");
		break;
	}

	// Merge with existing popups
	switch (type)
	{
	case NUMBER_POPUP_SCORE:
		MergePopups(&popups->score[localPlayerIdx], s);
		break;
	case NUMBER_POPUP_OBJECTIVE:
		MergePopups(CArrayGet(&popups->objective, s.u.ObjectiveIndex), s);
		break;
	default:
		CASSERT(false, "unknown HUD popup type");
		break;
	}
}
static void MergePopups(HUDNumPopup *dst, const HUDNumPopup src)
{
	// Combine popup amounts
	if (dst->Timer <= 0)
	{
		// Old popup finished; simply replace with new
		dst->Amount = src.Amount;
	}
	else
	{
		// Add the updates
		dst->Amount += src.Amount;
	}
	dst->Timer = src.Timer;
	dst->TimerMax = src.TimerMax;
	dst->u.PlayerUID = src.u.PlayerUID;
}

static void NumPopupUpdate(HUDNumPopup *p, const int ms);
void HUDPopupsUpdate(HUDNumPopups *popups, const int ms)
{
	for (int i = 0; i < MAX_LOCAL_PLAYERS; i++)
	{
		NumPopupUpdate(&popups->score[i], ms);
	}
	CA_FOREACH(HUDNumPopup, p, popups->objective)
		NumPopupUpdate(p, ms);
	CA_FOREACH_END()
}
static void NumPopupUpdate(HUDNumPopup *p, const int ms)
{
	if (p->Timer > 0)
	{
		p->Timer -= ms;
	}
}

static void DrawScoreUpdate(const HUDNumPopup *u, const int flags);
void HUDNumPopupsDrawPlayer(
	const HUDNumPopups *popups, const int idx, const int drawFlags)
{
	DrawScoreUpdate(&popups->score[idx], drawFlags);
}

static void DrawNumUpdate(
	const HUDNumPopup *p,
	const char *formatText, const int currentValue, struct vec2i pos, const int flags);
void HUDNumPopupsDrawObjective(
	const HUDNumPopups *popups, const int idx, const struct vec2i pos)
{
	const Objective *o = CArrayGet(&gMission.missionData->Objectives, idx);
	const HUDNumPopup *p = CArrayGet(&popups->objective, idx);
	DrawNumUpdate(p, "%d", o->done, pos, 0);
}

static void DrawScoreUpdate(const HUDNumPopup *u, const int flags)
{
	if (!IsScoreNeeded(gCampaign.Entry.Mode))
	{
		return;
	}
	if (u->Amount == 0)
	{
		return;
	}
	const PlayerData *p = PlayerDataGetByUID(u->u.PlayerUID);
	if (!IsPlayerAlive(p)) return;
	const int rowHeight = 1 + FontH();
	const int y = 5 + rowHeight;
	DrawNumUpdate(u, "Score: %d", p->Stats.Score, svec2i(5, y), flags);
}
// Parameters that define how the numeric update is animated
// The update animates in the following phases:
// 1. Pop up from text position to slightly above
// 2. Fall down from slightly above back to text position
// 3. Persist over text position
#define NUM_UPDATE_POP_UP_DURATION_MS 100
#define NUM_UPDATE_FALL_DOWN_DURATION_MS 100
#define NUM_UPDATE_POP_UP_HEIGHT 5
static void DrawNumUpdate(
	const HUDNumPopup *p,
	const char *formatText, const int currentValue, struct vec2i pos, const int flags)
{
	if (p->Timer <= 0 || p->Amount == 0)
	{
		return;
	}
	color_t color = p->Amount > 0 ? colorGreen : colorRed;

	char s[50];
	if (!(flags & HUDFLAGS_PLACE_RIGHT))
	{
		// Find the right position to draw the popup
		// Make sure the popup is displayed lined up with the lowest digits
		// Find the position of where the normal text is displayed,
		// and move to its right
		sprintf(s, formatText, currentValue);
		pos.x += FontStrW(s);
		// Then find the size of the popup, and move left
		sprintf(s, "%s%d", p->Amount > 0 ? "+" : "", p->Amount);
		pos.x -= FontStrW(s);
		// The final position should ensure the score popup's lowest digit
		// lines up with the normal score's lowest digit
	}
	else
	{
		sprintf(s, "%s%d", p->Amount > 0 ? "+" : "", p->Amount);
	}

	// Now animate the popup based on its stage
	int timer = p->TimerMax - p->Timer;
	if (timer < NUM_UPDATE_POP_UP_DURATION_MS)
	{
		// popup is still popping up
		// calculate height
		int popupHeight =
			timer * NUM_UPDATE_POP_UP_HEIGHT / NUM_UPDATE_POP_UP_DURATION_MS;
		pos.y -= popupHeight;
	}
	else if (timer <
		NUM_UPDATE_POP_UP_DURATION_MS + NUM_UPDATE_FALL_DOWN_DURATION_MS)
	{
		// popup is falling down
		// calculate height
		timer -= NUM_UPDATE_POP_UP_DURATION_MS;
		timer = NUM_UPDATE_FALL_DOWN_DURATION_MS - timer;
		int popupHeight =
			timer * NUM_UPDATE_POP_UP_HEIGHT / NUM_UPDATE_FALL_DOWN_DURATION_MS;
		pos.y -= popupHeight;
	}
	else
	{
		// Change alpha so that the popup fades away
		color.a = (uint8_t)CLAMP(p->Timer * 255 * 2 / p->TimerMax, 0, 255);
	}

	FontOpts opts = FontOptsNew();
	if (flags & HUDFLAGS_PLACE_RIGHT)
	{
		opts.HAlign = ALIGN_END;
	}
	if (flags & HUDFLAGS_PLACE_BOTTOM)
	{
		opts.VAlign = ALIGN_END;
		pos.y += BOTTOM_PADDING;
	}
	opts.Area = gGraphicsDevice.cachedConfig.Res;
	opts.Pad = pos;
	opts.Mask = color;
	opts.Blend = true;
	FontStrOpt(s, svec2i_zero(), opts);
}
