/* $Id$ */

/*
 * Copyright (c) 2012 Thomas Adam <thomas@xteddy.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>

#include <ctype.h>
#include <stdlib.h>

#include <string.h>

#include "tmux.h"

#define CMD_CHOOSE_TREE_WINDOW_ACTION "select-window -t '%%'"
#define CMD_CHOOSE_TREE_SESSION_ACTION "switch-client -t '%%'"
#define CMD_CHOOSE_TREE_WINDOW_TEMPLATE \
    DEFAULT_WINDOW_TEMPLATE " \"#{pane_title}\""

/*
 * Enter choice mode to choose a session and/or window.
 */

int	cmd_choose_tree_exec(struct cmd *, struct cmd_ctx *);

void	cmd_choose_tree_callback(struct window_choose_data *);
void	cmd_choose_tree_free(struct window_choose_data *);

const struct cmd_entry cmd_choose_tree_entry = {
	"choose-tree", NULL,
	"S:W:swb:c:t:", 0, 1,
	"[-sw] [-b session-template] [-c window template] [-S format] " \
	"[-W format] " CMD_TARGET_WINDOW_USAGE,
	0,
	NULL,
	NULL,
	cmd_choose_tree_exec
};

const struct cmd_entry cmd_choose_session_entry = {
	"choose-session", NULL,
	"F:t:", 0, 1,
	CMD_TARGET_WINDOW_USAGE " [-F format] [template]",
	0,
	NULL,
	NULL,
	cmd_choose_tree_exec
};

const struct cmd_entry cmd_choose_window_entry = {
	"choose-window", NULL,
	"F:t:", 0, 1,
	CMD_TARGET_WINDOW_USAGE "[-F format] [template]",
	0,
	NULL,
	NULL,
	cmd_choose_tree_exec
};

enum cmd_retval
cmd_choose_tree_exec(struct cmd *self, struct cmd_ctx *ctx)
{
	struct args			*args = self->args;
	struct winlink			*wl, *wm;
	struct session			*s, *s2;
	struct window_choose_data	*wcd = NULL;
	const char			*ses_template, *win_template;
	char				*final_win_action, *final_win_template;
	const char			*ses_action, *win_action;
	u_int				 cur_win, idx_ses, win_ses;
	u_int				 wflag, sflag;

	ses_template = win_template = NULL;
	ses_action = win_action = NULL;

	if (ctx->curclient == NULL) {
		ctx->error(ctx, "must be run interactively");
		return (CMD_RETURN_ERROR);
	}

	s = ctx->curclient->session;

	if ((wl = cmd_find_window(ctx, args_get(args, 't'), NULL)) == NULL)
		return (CMD_RETURN_ERROR);

	if (window_pane_set_mode(wl->window->active, &window_choose_mode) != 0)
		return (CMD_RETURN_NORMAL);

	/* Sort out which command this is. */
	wflag = sflag = 0;
	if (self->entry == &cmd_choose_session_entry) {
		sflag = 1;
		if ((ses_template = args_get(args, 'F')) == NULL)
			ses_template = DEFAULT_SESSION_TEMPLATE;

		if (args->argc != 0)
			ses_action = args->argv[0];
		else
			ses_action = CMD_CHOOSE_TREE_SESSION_ACTION;
	} else if (self->entry == &cmd_choose_window_entry) {
		wflag = 1;
		if ((win_template = args_get(args, 'F')) == NULL)
			win_template = CMD_CHOOSE_TREE_WINDOW_TEMPLATE;

		if (args->argc != 0)
			win_action = args->argv[0];
		else
			win_action = CMD_CHOOSE_TREE_WINDOW_ACTION;
	} else {
		wflag = args_has(args, 'w');
		sflag = args_has(args, 's');

		if ((ses_action = args_get(args, 'b')) == NULL)
			ses_action = CMD_CHOOSE_TREE_SESSION_ACTION;

		if ((win_action = args_get(args, 'c')) == NULL)
			win_action = CMD_CHOOSE_TREE_WINDOW_ACTION;

		if ((ses_template = args_get(args, 'S')) == NULL)
			ses_template = DEFAULT_SESSION_TEMPLATE;

		if ((win_template = args_get(args, 'W')) == NULL)
			win_template = CMD_CHOOSE_TREE_WINDOW_TEMPLATE;
	}

	/*
	 * If not asking for windows and sessions, assume no "-ws" given and
	 * hence display the entire tree outright.
	 */
	if (!wflag && !sflag)
		wflag = sflag = 1;

	/*
	 * If we're drawing in tree mode, including sessions, then pad the
	 * window template, otherwise just render the windows as a flat list
	 * without any padding.
	 */
	if (wflag && sflag)
		xasprintf(&final_win_template, "    --> %s", win_template);
	else if (wflag)
		final_win_template = xstrdup(win_template);
	else
		final_win_template = NULL;

	idx_ses = cur_win = -1;
	RB_FOREACH(s2, sessions, &sessions) {
		idx_ses++;

		/*
		 * If we're just choosing windows, jump straight there. Note
		 * that this implies the current session, so only choose
		 * windows when the session matches this one.
		 */
		if (wflag && !sflag) {
			if (s != s2)
				continue;
			goto windows_only;
		}

		wcd = window_choose_add_session(wl->window->active,
			ctx, s2, ses_template, (char *)ses_action, idx_ses);

		/* If we're just choosing sessions, skip choosing windows. */
		if (sflag && !wflag) {
			if (s == s2)
				cur_win = idx_ses;
			continue;
		}
windows_only:
		win_ses = -1;
		RB_FOREACH(wm, winlinks, &s2->windows) {
			win_ses++;
			if (sflag && wflag)
				idx_ses++;

			if (wm == s2->curw && s == s2) {
				if (wflag && !sflag) {
					/*
					 * Then we're only counting windows.
					 * So remember which is the current
					 * window in the list.
					 */
					cur_win = win_ses;
				} else
					cur_win = idx_ses;
			}

			xasprintf(&final_win_action, "%s ; %s", win_action,
				wcd ? wcd->command : "");

			window_choose_add_window(wl->window->active,
				ctx, s2, wm, final_win_template,
				final_win_action, idx_ses);

			free(final_win_action);
		}
		/*
		 * If we're just drawing windows, don't consider moving on to
		 * other sessions as we only list windows in this session.
		 */
		if (wflag && !sflag)
			break;
	}
	free(final_win_template);

	window_choose_ready(wl->window->active, cur_win,
		cmd_choose_tree_callback, cmd_choose_tree_free);

	return (CMD_RETURN_NORMAL);
}

void
cmd_choose_tree_callback(struct window_choose_data *cdata)
{
	if (cdata == NULL)
		return;

	if (cdata->client->flags & CLIENT_DEAD)
		return;

	window_choose_ctx(cdata);
}

void
cmd_choose_tree_free(struct window_choose_data *cdata)
{
	cdata->session->references--;
	cdata->client->references--;

	free(cdata->ft_template);
	free(cdata->command);
	format_free(cdata->ft);
	free(cdata);

}
