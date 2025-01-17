/*                 __                  __
  *                /_/\  __  __  __    /_/\  ______
  *               _\_\/ / /\/ /\/ /\  _\_\/ / ____ \
  *              / /\  / / / / / / / / /\  / /\_ / /\
  *         __  / / / / /_/ /_/ / / / / / / / / / / /
  *        / /_/ / / /_________/ / /_/ / /_/ / /_/ /
  *        \____/ /  \_________\/  \_\/  \_\/  \_\/
  *         \___\/
  *
  *
  *
  *     jwin.c
  *
  *     Windows(R) style GUI for Allegro.
  *     by Jeremy Craner
  *
  *     Most routines are adaptations of Allegro code.
  *     Allegro is by Shawn Hargreaves, et al.
  *
  *     Version: 3/22/00
  *     Allegro version: 3.1x  (don't know if it works with WIP)
  *
  */

#ifndef _EDITBOX_H_
#define _EDITBOX_H_

#include "zc_alleg.h"
#include <string>
//#ifdef __cplusplus
//extern "C"
//{
//#endif

enum {eb_wrap_none, eb_wrap_char, eb_wrap_word};
enum {eb_crlf_n, eb_crlf_nr, eb_crlf_r, eb_crlf_rn, eb_crlf_any};
enum {eb_scrollbar_optional, eb_scrollbar_on, eb_scrollbar_off};
enum {eb_handle_vscroll, eb_handle_hscroll};

int32_t d_editbox_proc(int32_t msg, DIALOG *d, int32_t c);

typedef struct editbox_data
{
	// char **text;
	std::string text;
	int32_t showcursor;
	int32_t lines;
	int32_t currtextline;
	int32_t list_width;
	int32_t currxpos;
	int32_t fakexpos;
	int32_t xofs;
	int32_t yofs;
	int32_t maxchars;
	int32_t maxlines;
	int32_t wrapping;
	int32_t insertmode;
	int32_t currchar;
	int32_t tabdisplaystyle;
	int32_t crlfdisplaystyle;
	int32_t newcrlftype;
	int32_t vsbarstyle;
	int32_t hsbarstyle;
	FONT *font;
	//char *clipboard;
	//int32_t clipboardsize;
	std::string clipboard;
	int32_t defaulttabsize;
	int32_t tabunits;
	int32_t customtabs;
	int32_t *customtabpos;
	int32_t numchars;
	int32_t selstart;
	int32_t selend;
	int32_t selfg;
	int32_t selbg;
	int32_t postpaste_dontmove;
} editbox_data;

//#ifdef __cplusplus
//}
//#endif
#endif

/***  The End  ***/

