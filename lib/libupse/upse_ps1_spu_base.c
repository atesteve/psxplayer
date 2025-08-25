/*
 * UPSE: the unix playstation sound emulator.
 *
 * Filename: upse_ps1_spu_base.c
 * Purpose: libupse: PS1 SPU base code
 *
 * Copyright (c) 2007 William Pitcock <nenolod@sacredspiral.co.uk>
 * Portions copyright (c) 2002 Pete Bernert <BlackDove@addcom.de>
 *
 * UPSE is free software, released under the GNU General Public License,
 * version 2.
 *
 * A copy of the GNU General Public License, version 2, is included in
 * the UPSE source kit as COPYING.
 *
 * UPSE is offered without any warranty of any kind, explicit or implicit.
 */

#define _IN_SPU

#include "upse-types.h"
#include "upse-internal.h"
#include "upse-ps1-spu-base.h"
#include "upse-spu-internal.h"
#include "upse-ps1-spu-features.h"

#include "upse.h"
#include "upse-ps1-memory-manager.h"

#include "Neill/spu.h"
#include "Neill/spucore.h"

////////////////////////////////////////////////////////////////////////
// MAIN SPU FUNCTION
// here is the main job handler... thread, timer or direct func call
// basically the whole sound processing is done in this fat func!
////////////////////////////////////////////////////////////////////////

// Counting to 65536 results in full volume offage.
void upse_ps1_spu_setlength(upse_spu_state_t *spu, s32 stop, s32 fade)
{
    _ENTER;

    _DEBUG("stop [%d] fade [%d]", stop, fade);

    if (stop == 0)
    {
	spu->decaybegin = 0;
        _LEAVE;
    }
    else
    {
	s64 stop_cycles = (stop * 169344ull) / 10;
	s64 fade_cycles = (fade * 169344ull) / 10;

	spu->decaybegin = stop_cycles;
	spu->decayend = stop_cycles + fade_cycles;
    }

    _LEAVE;
}

void upse_ps1_spu_setvolume(upse_spu_state_t *spu, int volume)
{
    _ENTER;

    _LEAVE;
}

int upse_ps1_spu_seek(upse_module_instance_t *ins, u32 t)
{
    upse_spu_state_t *spu = ins->spu;

    _ENTER;

    spu->seektime = (t * 169344ull) / 10;
    if (spu->seektime > spu->cyclecount)
    {
        _LEAVE;
	return 1;
    }

    _LEAVE;

    return 0;
}

u32 upse_ps1_spu_tell_seek(upse_module_instance_t *ins)
{
    upse_spu_state_t *spu = ins->spu;
    u32 ret;

    _ENTER;

    ret = (spu->cyclecount * 10) / 169344;

    _LEAVE;

    return ret;
}

#define CLIP(_x) {if(_x>32767) _x=32767; if(_x<-32767) _x=-32767;}

extern float multiplier;

int upse_ps1_spu_render(upse_spu_state_t *spu, u32 cycles)
{
    if ( spu == NULL )
      return (0);

    s32 dosamples;
    s32 temp;

    const int multFactor = 384*multiplier;

    spu->nextirq += cycles;
    dosamples = spu->nextirq / multFactor;
    if (!dosamples)
	return (1);
    spu->nextirq -= dosamples * multFactor;
    temp = dosamples;

	spu_render( spu->pCore, spu->pS, temp );

	for ( dosamples = 0; dosamples < temp; dosamples++ )
	{
		if (spu->decaybegin != 0 && spu->cyclecount >= spu->decaybegin)
		{
			s32 dmul;
			if (spu->decaybegin != 0)
			{
				s32 sl = spu->pS[dosamples * 2];
				s32 sr = spu->pS[dosamples * 2 + 1];
				if (spu->cyclecount >= spu->decayend)
					return (0);
				dmul = 256 - (256 * (spu->cyclecount - spu->decaybegin) / (spu->decayend - spu->decaybegin));
				_DEBUG("dmul: %d", dmul);
				sl = (sl * dmul) >> 8;
				sr = (sr * dmul) >> 8;
				spu->pS[dosamples * 2] = sl;
				spu->pS[dosamples * 2 + 1] = sr;
			}
		}

		spu->cyclecount += multFactor;
	}

	spu->pS += 2 * temp;

    return (1);
}

void upse_ps1_spu_stop(upse_module_instance_t *ins)
{
    upse_spu_state_t *spu = ins->spu;
    if ( spu == NULL )
      return;

    spu->decaybegin = 1;
    spu->decayend = 0;
}

void upse_ps1_spu_set_audio_callback(upse_module_instance_t *ins, upse_audio_callback_func_t func, const void *user_data)
{
    upse_spu_state_t *spu = ins->spu;

    _ENTER;

    spu->cb = func;
    spu->cb_userdata = user_data;

    _DEBUG("set audio callback function to <%p>", spu->cb);
    _DEBUG("set audio callback userdata to <%p>", spu->cb_userdata);

    _LEAVE;
}

void upse_ps1_spu_finalize(upse_spu_state_t *spu)
{
    if ((spu->seektime != (u32) ~ 0) && spu->seektime > spu->cyclecount)
    {
	spu->pS = (s16 *) spu->pSpuBuffer;

	if (spu->cb != NULL)
	    spu->cb(0, 0, spu->cb_userdata);
    }
    else if ((u8 *) spu->pS > ((u8 *) spu->pSpuBuffer + 1024))
    {
#ifdef FEAT_NYQUIST_MODULATION
        upse_spu_nyquist_filter_process(spu, (s16 *) spu->pSpuBuffer, ((u8 *) spu->pS - (u8 *) spu->pSpuBuffer) / 4);
#endif

#ifdef FEAT_CORLETT_IIR_FILTERS
        upse_spu_lowpass_filter_process(spu, (s16 *) spu->pSpuBuffer, ((u8 *) spu->pS - (u8 *) spu->pSpuBuffer) / 4);
#endif

	if (spu->cb != NULL)
	    spu->cb((u8 *) spu->pSpuBuffer, (u8 *) spu->pS - (u8 *) spu->pSpuBuffer, spu->cb_userdata);

	spu->pS = (s16 *) spu->pSpuBuffer;
    }
}

int upse_ps1_spu_finalize_count(upse_spu_state_t *spu, s16 ** s)
{
    if ((spu->seektime != (u32) ~ 0) && spu->seektime > spu->cyclecount)
    {
        spu->pS = (s16 *) spu->pSpuBuffer;
        *s = NULL;
        return 1;
    }
    else if ((u8 *) spu->pS > ((u8 *) spu->pSpuBuffer + 1024))
    {
        unsigned samples_rendered = ( (u8 *) spu->pS - (u8 *) spu->pSpuBuffer ) / 4;

#ifdef FEAT_NYQUIST_MODULATION
        upse_spu_nyquist_filter_process(spu, (s16 *) spu->pSpuBuffer, ((u8 *) spu->pS - (u8 *) spu->pSpuBuffer) / 4);
#endif

#ifdef FEAT_CORLETT_IIR_FILTERS
        upse_spu_lowpass_filter_process(spu, (s16 *) spu->pSpuBuffer, samples_rendered);
#endif

        spu->pS = (s16 *) spu->pSpuBuffer;
        *s = spu->pS;
        return samples_rendered;
    }

    return 0;
}

////////////////////////////////////////////////////////////////////////
// INIT/EXIT STUFF
////////////////////////////////////////////////////////////////////////

static void
upse_ps1_spu_setup_streams(upse_spu_state_t *spu)
{
    if (spu == NULL)
        return;

    spu->pS = (s16 *) spu->pSpuBuffer;
}

upse_spu_state_t *
upse_ps1_spu_open(upse_module_instance_t *ins)
{
	static int initialized = 0;

    upse_spu_state_t *spu = upse_ps1_alloc(ins, sizeof(upse_spu_state_t));

	if ( !initialized )
	{
		spu_init();
		spucore_init();
		initialized = 1;
	}

	spu->pCore = upse_ps1_alloc(ins, spu_get_state_size(1));
	spu_clear_state(spu->pCore, 1);

    spu->cyclecount = spu->nextirq = 0;
    spu->seektime = (u32) ~ 0;

    spu->ins = ins;

    upse_ps1_spu_setup_streams(spu);		// prepare streaming

    upse_spu_lowpass_filter_redesign(spu, 44100);

    return spu;
}

void
upse_ps1_spu_close(upse_spu_state_t *spu)
{
    if (spu == NULL)
	return;

	free(spu->pCore);

    free(spu);
}
