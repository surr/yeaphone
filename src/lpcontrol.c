/****************************************************************************
 *
 *  File: lpcontrol.c
 *
 *  Copyright (C) 2006 - 2008  Thomas Reitmayr <treitmayr@devbase.at>
 *
 ****************************************************************************
 *
 *  This file is part of Yeaphone.
 *
 *  Yeaphone is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 ****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <mediastreamer2/mssndcard.h>

/* needed?
#include "coreapi/exevents.h"
#include "coreapi/private.h"
#include "mediastreamer2/mediastream.h"
*/
#include "lpcontrol.h"
#include "yldisp.h"
#include "ylsysfs.h"
#include "ypmainloop.h"
#include "ypconfig.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef DMALLOC
#include <dmalloc.h>
#endif


#define RING_VOL_STEP  10
#define SPKR_VOL_STEP  10
#define RING_VOL_MIN   30
#define SPKR_VOL_MIN   30


/***************************************************************************
 *
 *  Types
 *
 ***************************************************************************/

typedef struct lpcontrol_data_s {
  int autoregister;
  GeneralStateChange callback;
  
  LinphoneCoreVTable *vtable;
  LinphoneCore *core_state;
  
  char configfile_name[PATH_MAX];
  
  MSSndCard *sndcard;
  
} lpcontrol_data_t;


/***************************************************************************
 *
 *  Forward declarations 
 *
 ***************************************************************************/
 
static void lpc_dummy();
static void lpc_display_something(LinphoneCore * lc, const char *something);
static void lpc_display_status(LinphoneCore * lc, const char *something);
static void lpc_display_warning(LinphoneCore * lc, const char *something);
static void lpc_display_url(LinphoneCore * lc, const char *something, const char *url);
static void lpc_call_received(LinphoneCore *lc, const char *from);
static void lpc_prompt_for_auth(LinphoneCore *lc, const char *realm, const char *username);
static void lpc_notify_presence_received(LinphoneCore *lc,LinphoneFriend *fid);
static void lpc_new_unknown_subscriber(LinphoneCore *lc, LinphoneFriend *lf,
                                       const char *url);
static void lpc_bye_received(LinphoneCore *lc, const char *from);
static void lpc_text_received(LinphoneCore *lc, LinphoneChatRoom *cr,
                              const char *from, const char *msg);

/***************************************************************************
 *
 * Global variables 
 *
 ***************************************************************************/

LinphoneCoreVTable lpc_vtable = {
  show:                   lpc_dummy,
  inv_recv:               lpc_call_received,
  bye_recv:               lpc_bye_received, 
  notify_presence_recv:   lpc_notify_presence_received,
  new_unknown_subscriber: lpc_new_unknown_subscriber,
  auth_info_requested:    lpc_prompt_for_auth,
  display_status:         lpc_display_status,
  display_message:        lpc_display_something,
  display_warning:        lpc_display_warning,
  display_url:            lpc_display_url,
  display_question:       lpc_dummy,
  call_log_updated:       lpc_dummy,
  text_received:          lpc_text_received,
  general_state:          NULL,
  dtmf_received:          NULL,
  refer_received:         NULL,
  buddy_info_updated:     NULL,
  notify_recv:            NULL
};


lpcontrol_data_t lpstates_data = {
  sndcard: NULL
};


/***************************************************************************
 *
 * Linphone core callbacks
 *
 ***************************************************************************/

static void lpc_dummy() {
}


static void lpc_display_something(LinphoneCore *lc, const char *something) {
  fprintf(stdout, "%s\n", something);
}


static void lpc_display_status(LinphoneCore *lc, const char *something) {
  fprintf(stdout, "%s\n", something);
}


static void lpc_display_warning(LinphoneCore * lc, const char *something) {
  fprintf(stdout, "Warning: %s\n", something);
}


static void lpc_display_url(LinphoneCore * lc, const char *something, const char *url) {
  fprintf (stdout, "%s: %s\n", something, url);
}


static void lpc_call_received(LinphoneCore *lc, const char *from) {
  return;
}


static void lpc_prompt_for_auth(LinphoneCore *lc, const char *realm, const char *username) {
  fprintf(stderr, "(lpc_call_received) Please use linphonec to enter authentication information!\n");
}


static void lpc_notify_presence_received(LinphoneCore *lc,LinphoneFriend *fid) {
  printf("Friend presence change\n");
  // todo: update Friend list state (unimplemented)
}


static void lpc_new_unknown_subscriber(LinphoneCore *lc, LinphoneFriend *lf,
                                       const char *url) {
  printf("Friend %s requested subscription "
         "(accept/deny is not implemented yet)\n", url); 
  // This means that this person wishes to be notified 
  // of your presence information (online, busy, away...).
}


static void lpc_bye_received(LinphoneCore *lc, const char *from) {
  /*lpcontrol_data_t *lpd_ptr = lc->data;*/
}


static void lpc_text_received(LinphoneCore *lc, LinphoneChatRoom *cr,
                              const char *from, const char *msg) {
  printf("%s: %s\n", from, msg);
}

/***************************************************************************
 *
 * Central functions
 *
 ***************************************************************************/

void lpcontrol_timer_callback(int id, int group, void *private_data) {
  lpcontrol_data_t *lpd_ptr = private_data;
  
  linphone_core_iterate(lpd_ptr->core_state);
}

/*****************************************************************/

void lpstates_callback_wrapper(struct _LinphoneCore *lc,
                               LinphoneGeneralState *gstate)
{
  if (gstate->new_state == GSTATE_POWER_OFF)
    yp_ml_remove_event(-1, LPCONTROL_TIMER_ID);

  if (lpstates_data.callback)
    lpstates_data.callback(lc, gstate);
}

/*****************************************************************/

void set_lpstates_callback(GeneralStateChange callback) 
{
  lpstates_data.callback = callback;
}

/*****************************************************************/

void override_soundcards()
{
  ylsysfs_model model;
  int card;
  char pcm_name[20];
  char *ringer;
  const char* devid;
  
  model = ylsysfs_get_model();

  if (lpstates_data.sndcard != NULL)
    ms_snd_card_destroy(lpstates_data.sndcard);
  
  ringer = ypconfig_get_value("ringer-device");

  card = ylsysfs_get_alsa_card();
  if (card >= 0) {
    // get ALSA sound card name
    snprintf(pcm_name, sizeof(pcm_name), "plughw:%d", card);
    lpstates_data.sndcard = ms_alsa_card_new_custom(pcm_name, pcm_name);
    ms_snd_card_manager_add_card(ms_snd_card_manager_get(),lpstates_data.sndcard);
    devid = ms_snd_card_get_string_id(lpstates_data.sndcard);

    linphone_core_set_playback_device(lpstates_data.core_state, devid);
    linphone_core_set_capture_device(lpstates_data.core_state, devid);

    if (ringer) {
      linphone_core_set_ringer_device(lpstates_data.core_state, ringer);
      if (!linphone_core_get_ringer_device(lpstates_data.core_state)) {
        printf("Ringer device %s not found, falling back to handset's ringer\n", ringer);
        linphone_core_set_ringer_device(lpstates_data.core_state, devid);
      }
    }
    else
      linphone_core_set_ringer_device(lpstates_data.core_state, devid);

    printf("ringer device = %s\n",
           linphone_core_get_ringer_device(lpstates_data.core_state));
    printf("playback device = %s\n",
           linphone_core_get_playback_device(lpstates_data.core_state));
  }

  if (!ringer && ((model == YL_MODEL_P1K) || (model == YL_MODEL_P1KH))) {
    /* we use the ringer on the handset */
    linphone_core_set_ring(lpstates_data.core_state, "/dev/null");
  }
}

/*****************************************************************/

void lpstates_submit_command(lpstates_command_t command, char *arg)
{
  char *cp = arg;
  int level;
  
  /*printf("command %d with arg '%s'\n", command, arg);*/
  
  switch (command) {
    case LPCOMMAND_STARTUP:
      yp_ml_schedule_periodic_timer(LPCONTROL_TIMER_ID, 200, 1,
                                    lpcontrol_timer_callback, &lpstates_data);
      lpstates_data.core_state = linphone_core_new(lpstates_data.vtable,
          lpstates_data.configfile_name, 0, &lpstates_data);
      setLinphoneCore(lpstates_data.core_state);
      override_soundcards();
      break;
      
    case LPCOMMAND_SHUTDOWN:
      linphone_core_terminate_call(lpstates_data.core_state, NULL);
      linphone_core_destroy(lpstates_data.core_state);
      break;
      
    case LPCOMMAND_CALL:
      linphone_core_invite(lpstates_data.core_state, arg);
      break;
      
    case LPCOMMAND_DTMF:
      while (isdigit(*cp) || *cp == '#' || *cp == '*') {
        linphone_core_send_dtmf(lpstates_data.core_state, *cp);
        cp++;
        if (*cp)
          usleep(200000);
      }
      break;
      
    case LPCOMMAND_PICKUP:
      linphone_core_accept_call(lpstates_data.core_state, NULL);
      break;
      
    case LPCOMMAND_HANGUP:
      linphone_core_terminate_call(lpstates_data.core_state, NULL);
      break;
      
    case LPCOMMAND_RING_VOLUP:
      /*
      level = linphone_core_get_ring_level(lpstates_data.core_state);
      level += RING_VOL_STEP;
      if (level > 100) level = 100;
      linphone_core_set_ring_level(lpstates_data.core_state, level);
      */
      break;
    
    case LPCOMMAND_RING_VOLDN:
      /*
      level = linphone_core_get_ring_level(lpstates_data.core_state);
      level -= RING_VOL_STEP;
      if (level < RING_VOL_MIN) level = RING_VOL_MIN;
      linphone_core_set_ring_level(lpstates_data.core_state, level);
      */
      break;
    
    case LPCOMMAND_SPKR_VOLUP:
      level = linphone_core_get_play_level(lpstates_data.core_state);
      level += SPKR_VOL_STEP;
      if (level > 100) level = 100;
      linphone_core_set_play_level(lpstates_data.core_state, level);
      break;
    
    case LPCOMMAND_SPKR_VOLDN:
      level = linphone_core_get_play_level(lpstates_data.core_state);
      level -= SPKR_VOL_STEP;
      if (level < SPKR_VOL_MIN) level = SPKR_VOL_MIN;
      linphone_core_set_play_level(lpstates_data.core_state, level);
      break;
    
    default:
      break;
  }
}

/*****************************************************************/

void start_lpcontrol(int autoregister, void *userdata) {
  lpstates_data.autoregister = autoregister;
  lpstates_data.vtable = &lpc_vtable;
  
//  linphone_core_enable_logs(stdout);
  linphone_core_disable_logs();
  
  snprintf(lpstates_data.configfile_name, PATH_MAX, "%s/.linphonerc", getenv("HOME"));

  lpc_vtable.general_state = lpstates_callback_wrapper;

  if (autoregister) {
    lpstates_submit_command(LPCOMMAND_STARTUP, NULL);
  }
}

/*****************************************************************/

