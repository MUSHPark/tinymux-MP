// comsys.cpp
//
// $Id: comsys.cpp,v 1.34 2002-07-27 10:49:01 jake Exp $
//
#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include <sys/types.h>

#include "ansi.h"
#include "attrs.h"
#include "comsys.h"
#include "functions.h"
#include "interface.h"
#include "powers.h"

int num_channels;
comsys_t *comsys_table[NUM_COMSYS];

#define DFLT_MAX_LOG        0
#define MIN_RECALL_REQUEST  1
#define DFLT_RECALL_REQUEST 10
#define MAX_RECALL_REQUEST  200

extern int iMod(int x, int y);

// Return value must be free_lbuf'ed.
//
char *RestrictTitleValue(char *pTitleRequest)
{
    // First, remove all '\r\n\t' from the string.
    //
    char *pNewTitle = RemoveSetOfCharacters(pTitleRequest, "\r\n\t");

    // Optimize/terminate any ANSI in the string.
    //
    char NewTitle_ANSI[MAX_TITLE_LEN+1];
    int nVisualWidth;
    int nLen = ANSI_TruncateToField(pNewTitle, sizeof(NewTitle_ANSI),
        NewTitle_ANSI, sizeof(NewTitle_ANSI), &nVisualWidth,
        ANSI_ENDGOAL_NORMAL);
    memcpy(pNewTitle, NewTitle_ANSI, nLen+1);
    return pNewTitle;
}

void do_setcomtitlestatus(dbref player, struct channel *ch, BOOL status)
{
    struct comuser *user = select_user(ch,player);
    if (ch && user)
    {
        user->ComTitleStatus = status;
    }
}

void do_setnewtitle(dbref player, struct channel *ch, char *pValidatedTitle)
{
    struct comuser *user = select_user(ch, player);

    if (ch && user)
    {
        if (user->title)
        {
            MEMFREE(user->title);
            user->title = NULL;
        }
        user->title = StringClone(pValidatedTitle);
    }
}

void load_comsys(char *filename)
{
    int i;
    char buffer[200];

    for (i = 0; i < NUM_COMSYS; i++)
    {
        comsys_table[i] = NULL;
    }

    FILE *fp = fopen(filename, "rb");
    if (!fp)
    {
        Log.tinyprintf("Error: Couldn't find %s." ENDLINE, filename);
    }
    else
    {
        DebugTotalFiles++;
        Log.tinyprintf("LOADING: %s" ENDLINE, filename);
        if (fscanf(fp, "*** Begin %s ***\n", buffer) == 1 && !strcmp(buffer, "CHANNELS"))
        {
            load_channels(fp);
        }
        else
        {
            Log.tinyprintf("Error: Couldn't find Begin CHANNELS in %s.", filename);
            return;
        }

        if (fscanf(fp, "*** Begin %s ***\n", buffer) == 1 && !strcmp(buffer, "COMSYS"))
        {
            load_comsystem(fp);
        }
        else
        {
            Log.tinyprintf("Error: Couldn't find Begin COMSYS in %s.", filename);
            return;
        }

        if (fclose(fp) == 0)
        {
            DebugTotalFiles--;
        }
        Log.tinyprintf("LOADING: %s (done)" ENDLINE, filename);
    }
}

void save_comsys(char *filename)
{
    char buffer[500];

    sprintf(buffer, "%s.#", filename);
    FILE *fp = fopen(buffer, "wb");
    if (!fp)
    {
        Log.tinyprintf("Unable to open %s for writing." ENDLINE, buffer);
        return;
    }
    DebugTotalFiles++;
    fprintf(fp, "*** Begin CHANNELS ***\n");
    save_channels(fp);

    fprintf(fp, "*** Begin COMSYS ***\n");
    save_comsystem(fp);

    if (fclose(fp) == 0)
    {
        DebugTotalFiles--;
    }
    ReplaceFile(buffer, filename);
}

// Aliases must be between 1 and 5 characters. No spaces. No ANSI.
//
char *MakeCanonicalComAlias
(
    const char *pAlias,
    int *nValidAlias,
    BOOL *bValidAlias
)
{
    static char Buffer[ALIAS_SIZE];
    *nValidAlias = 0;
    *bValidAlias = FALSE;

    if (!pAlias)
    {
        return NULL;
    }
    const char *p = pAlias;
    char *q = Buffer;
    int n = 0;
    while (*p)
    {
        if (!Tiny_IsPrint[(unsigned char)*p] || *p == ' ')
        {
            return NULL;
        }
        if (n <= MAX_ALIAS_LEN)
        {
            n++;
            *q++ = *p;
        }
        p++;
    }
    *q = '\0';
    if (  n < 1
       || MAX_ALIAS_LEN < n)
    {
        return FALSE;
    }
    *nValidAlias = n;
    *bValidAlias = TRUE;
    return Buffer;
}

BOOL ParseChannelLine(char *pBuffer, char *pAlias5, char **ppChannelName)
{
    // Fetch alias portion. We need to find the first space.
    //
    char *p = strchr(pBuffer, ' ');
    if (!p)
    {
        return FALSE;
    }

    *p = '\0';
    BOOL bValidAlias;
    int  nValidAlias;
    char *pValidAlias = MakeCanonicalComAlias(pBuffer, &nValidAlias, &bValidAlias);
    if (!bValidAlias)
    {
        return FALSE;
    }
    strcpy(pAlias5, pValidAlias);

    // Skip any leading space before the channel name.
    //
    p++;
    while (Tiny_IsSpace[(unsigned char)*p])
    {
        p++;
    }

    if (*p == '\0')
    {
        return FALSE;
    }

    // The rest of the line is the channel name.
    //
    *ppChannelName = StringClone(p);
    return TRUE;
}

void load_channels(FILE *fp)
{
    int i, j;
    char buffer[LBUF_SIZE];
    comsys_t *c;

    int np = 0;
    fscanf(fp, "%d\n", &np);
    for (i = 0; i < np; i++)
    {
        c = create_new_comsys();
        c->who = 0;
        c->numchannels = 0;
        fscanf(fp, "%d %d\n", &(c->who), &(c->numchannels));
        c->maxchannels = c->numchannels;
        if (c->maxchannels > 0)
        {
            c->alias = (char *)MEMALLOC(c->maxchannels * ALIAS_SIZE);
            (void)ISOUTOFMEMORY(c->alias);
            c->channels = (char **)MEMALLOC(sizeof(char *) * c->maxchannels);
            (void)ISOUTOFMEMORY(c->channels);

            for (j = 0; j < c->numchannels; j++)
            {
                int n = GetLineTrunc(buffer, sizeof(buffer), fp);
                if (buffer[n-1] == '\n')
                {
                    // Get rid of trailing '\n'.
                    //
                    n--;
                    buffer[n] = '\0';
                }
                if (!ParseChannelLine(buffer, c->alias + j * ALIAS_SIZE, c->channels+j))
                {
                    c->numchannels--;
                    j--;
                }
            }
            sort_com_aliases(c);
        }
        else
        {
            c->alias = NULL;
            c->channels = NULL;
        }
        if (Good_obj(c->who))
        {
            add_comsys(c);
        }
        else
        {
            Log.tinyprintf("Invalid dbref %d." ENDLINE, c->who);
        }
        purge_comsystem();
    }
}

void purge_comsystem(void)
{
#ifdef ABORT_PURGE_COMSYS
    return;
#endif // ABORT_PURGE_COMSYS

    comsys_t *c;
    comsys_t *d;
    int i;
    for (i = 0; i < NUM_COMSYS; i++)
    {
        c = comsys_table[i];
        while (c)
        {
            d = c;
            c = c->next;
            if (d->numchannels == 0)
            {
                del_comsys(d->who);
                continue;
            }
            if (isPlayer(d->who))
            {
                continue;
            }
            if (  God(Owner(d->who))
               && Going(d->who))
            {
                del_comsys(d->who);
                continue;
            }
        }
    }
}

void save_channels(FILE *fp)
{
    purge_comsystem();

    comsys_t *c;
    int i, j;
    int np = 0;
    for (i = 0; i < NUM_COMSYS; i++)
    {
        c = comsys_table[i];
        while (c)
        {
            np++;
            c = c->next;
        }
    }

    fprintf(fp, "%d\n", np);
    for (i = 0; i < NUM_COMSYS; i++)
    {
        c = comsys_table[i];
        while (c)
        {
            fprintf(fp, "%d %d\n", c->who, c->numchannels);
            for (j = 0; j < c->numchannels; j++)
            {
                fprintf(fp, "%s %s\n", c->alias + j * ALIAS_SIZE, c->channels[j]);
            }
            c = c->next;
        }
    }
}

comsys_t *create_new_comsys(void)
{
    comsys_t *c = (comsys_t *)MEMALLOC(sizeof(comsys_t));
    (void)ISOUTOFMEMORY(c);

    c->who         = NOTHING;
    c->numchannels = 0;
    c->maxchannels = 0;
    c->alias       = NULL;
    c->channels    = NULL;
    c->next        = NULL;
    return c;
}

comsys_t *get_comsys(dbref which)
{
    if (which < 0)
    {
        return NULL;
    }

    comsys_t *c = comsys_table[which % NUM_COMSYS];

    while (c && (c->who != which))
        c = c->next;

    if (!c)
    {
        c = create_new_comsys();
        c->who = which;
        add_comsys(c);
    }
    return c;
}

void add_comsys(comsys_t *c)
{
    if (c->who < 0 || c->who >= mudstate.db_top)
    {
        Log.tinyprintf("add_comsys: dbref %d out of range [0, %d)" ENDLINE, c->who, mudstate.db_top);
        return;
    }

    c->next = comsys_table[c->who % NUM_COMSYS];
    comsys_table[c->who % NUM_COMSYS] = c;
}

void del_comsys(dbref who)
{
    if (who < 0 || who >= mudstate.db_top)
    {
        Log.tinyprintf("del_comsys: dbref %d out of range [0, %d)" ENDLINE, who, mudstate.db_top);
        return;
    }

    comsys_t *c = comsys_table[who % NUM_COMSYS];

    if (c == NULL)
    {
        return;
    }

    if (c->who == who)
    {
        comsys_table[who % NUM_COMSYS] = c->next;
        destroy_comsys(c);
        return;
    }
    comsys_t *last = c;
    c = c->next;
    while (c)
    {
        if (c->who == who)
        {
            last->next = c->next;
            destroy_comsys(c);
            return;
        }
        last = c;
        c = c->next;
    }
}

void destroy_comsys(comsys_t *c)
{
    int i;

    if (c->alias)
    {
        MEMFREE(c->alias);
        c->alias = NULL;
    }
    for (i = 0; i < c->numchannels; i++)
    {
        MEMFREE(c->channels[i]);
        c->channels[i] = NULL;
    }
    if (c->channels)
    {
        MEMFREE(c->channels);
        c->channels = NULL;
    }
    MEMFREE(c);
    c = NULL;
}

void sort_com_aliases(comsys_t *c)
{
    int i;
    char buffer[10];
    char *s;
    BOOL cont = TRUE;

    while (cont)
    {
        cont = FALSE;
        for (i = 0; i < c->numchannels - 1; i++)
        {
            if (strcmp(c->alias + i * ALIAS_SIZE, c->alias + (i + 1) * ALIAS_SIZE) > 0)
            {
                strcpy(buffer, c->alias + i * ALIAS_SIZE);
                strcpy(c->alias + i * ALIAS_SIZE, c->alias + (i + 1) * ALIAS_SIZE);
                strcpy(c->alias + (i + 1) * ALIAS_SIZE, buffer);
                s = c->channels[i];
                c->channels[i] = c->channels[i + 1];
                c->channels[i + 1] = s;
                cont = TRUE;
            }
        }
    }
}

char *get_channel_from_alias(dbref player, char *alias)
{
    int first, last, current, dir;

    comsys_t *c = get_comsys(player);

    current = first = 0;
    last = c->numchannels - 1;
    dir = 1;

    while (dir && (first <= last))
    {
        current = (first + last) / 2;
        dir = strcmp(alias, c->alias + ALIAS_SIZE * current);
        if (dir < 0)
            last = current - 1;
        else
            first = current + 1;
    }

    if (!dir)
    {
        return c->channels[current];
    }
    else
    {
        return "";
    }
}

void load_comsystem(FILE *fp)
{
    int i, j, dummy;
    int ver = 0;
    struct channel *ch;
    char temp[LBUF_SIZE];

    num_channels = 0;

    int nc = 0;
    fgets(temp, sizeof(temp), fp);
    if (!strncmp(temp, "+V", 2))
    {
        // +V2 has colored headers
        //
        ver = Tiny_atol(temp + 2);
        if (ver < 1 || 3 < ver)
        {
            return;
        }
        fscanf(fp, "%d\n", &nc);
    }
    else
    {
        nc = Tiny_atol(temp);
    }

    num_channels = nc;

    for (i = 0; i < nc; i++)
    {
        ch = (struct channel *)MEMALLOC(sizeof(struct channel));
        (void)ISOUTOFMEMORY(ch);

        int nChannel = GetLineTrunc(temp, sizeof(temp), fp);
        if (nChannel > MAX_CHANNEL_LEN)
        {
            nChannel = MAX_CHANNEL_LEN;
        }
        if (temp[nChannel-1] == '\n')
        {
            // Get rid of trailing '\n'.
            //
            nChannel--;
        }
        memcpy(ch->name, temp, nChannel);
        ch->name[nChannel] = '\0';

        if (ver >= 2)
        {
            int nHeader = GetLineTrunc(temp, sizeof(temp), fp);
            if (nHeader > MAX_HEADER_LEN)
            {
                nHeader = MAX_HEADER_LEN;
            }
            if (temp[nHeader-1] == '\n')
            {
                nHeader--;
            }
            memcpy(ch->header, temp, nHeader);
            ch->header[nHeader] = '\0';
        }

        ch->on_users = NULL;

        hashaddLEN(ch->name, nChannel, (int *)ch, &mudstate.channel_htab);

        ch->type         = 127;
        ch->temp1        = 0;
        ch->temp2        = 0;
        ch->charge       = 0;
        ch->charge_who   = NOTHING;
        ch->amount_col   = 0;
        ch->num_messages = 0;
        ch->chan_obj     = NOTHING;

        if (ver >= 1)
        {
            fscanf(fp, "%d %d %d %d %d %d %d %d\n",
                &(ch->type), &(ch->temp1), &(ch->temp2),
                &(ch->charge), &(ch->charge_who),
                &(ch->amount_col), &(ch->num_messages), &(ch->chan_obj));
        }
        else
        {
            fscanf(fp, "%d %d %d %d %d %d %d %d %d %d\n",
                &(ch->type), &(dummy), &(ch->temp1), &(ch->temp2),
                &(dummy), &(ch->charge), &(ch->charge_who),
                &(ch->amount_col), &(ch->num_messages), &(ch->chan_obj));
        }

        if (ver <= 1)
        {
            // Build colored header if not +V2 or later db.
            //
            if (ch->type & CHANNEL_PUBLIC)
            {
                sprintf(temp, "%s[%s%s%s%s%s]%s", ANSI_CYAN, ANSI_HILITE,
                    ANSI_BLUE, ch->name, ANSI_NORMAL, ANSI_CYAN, ANSI_NORMAL);
            }
            else
            {
                sprintf(temp, "%s[%s%s%s%s%s]%s", ANSI_MAGENTA, ANSI_HILITE,
                    ANSI_RED, ch->name, ANSI_NORMAL, ANSI_MAGENTA,
                    ANSI_NORMAL);
            }
            int vwVisual;
            ANSI_TruncateToField(temp, MAX_HEADER_LEN+1, ch->header,
                MAX_HEADER_LEN+1, &vwVisual, ANSI_ENDGOAL_NORMAL);
        }

        ch->num_users = 0;
        fscanf(fp, "%d\n", &(ch->num_users));
        ch->max_users = ch->num_users;
        if (ch->num_users > 0)
        {
            ch->users = (struct comuser **)calloc(ch->max_users, sizeof(struct comuser *));
            (void)ISOUTOFMEMORY(ch->users);

            int jAdded = 0;
            for (j = 0; j < ch->num_users; j++)
            {
                struct comuser t_user;
                memset(&t_user, 0, sizeof(t_user));

                t_user.who = NOTHING;
                t_user.bUserIsOn = FALSE;
                t_user.ComTitleStatus = FALSE;

                if (ver == 3)
                {
                    fscanf(fp, "%d %d %d\n", &(t_user.who), &(t_user.bUserIsOn),
                        &(t_user.ComTitleStatus));
                }
                else
                {
                    t_user.ComTitleStatus = TRUE;
                    if (ver)
                    {
                        fscanf(fp, "%d %d\n", &(t_user.who), &(t_user.bUserIsOn));
                    }
                    else
                    {
                        fscanf(fp, "%d %d %d", &(t_user.who), &(dummy), &(dummy));
                        fscanf(fp, "%d\n", &(t_user.bUserIsOn));
                    }
                }

                // Read Comtitle.
                //
                int nTitle = GetLineTrunc(temp, sizeof(temp), fp);
                char *pTitle = temp;

                if (  t_user.who >= 0
                   && t_user.who < mudstate.db_top)
                {
                    // Validate comtitle
                    //
                    if (3 < nTitle && temp[0] == 't' && temp[1] == ':')
                    {
                        pTitle = temp+2;
                        nTitle -= 2;
                        if (pTitle[nTitle-1] == '\n')
                        {
                            // Get rid of trailing '\n'.
                            //
                            nTitle--;
                        }
                        if (nTitle <= 0 || MAX_TITLE_LEN < nTitle)
                        {
                            nTitle = 0;
                            pTitle = temp;
                        }
                    }
                    else
                    {
                        nTitle = 0;
                    }

                    struct comuser *user = (struct comuser *)MEMALLOC(sizeof(struct comuser));
                    (void)ISOUTOFMEMORY(user);
                    memcpy(user, &t_user, sizeof(struct comuser));

                    user->title = StringCloneLen(pTitle, nTitle);
                    ch->users[jAdded++] = user;

                    if (  !(isPlayer(user->who))
                       && !(Going(user->who)
                       && (God(Owner(user->who)))))
                    {
                        do_joinchannel(user->who, ch);
                    }
                    user->on_next = ch->on_users;
                    ch->on_users = user;
                }
                else
                {
                    Log.tinyprintf("load_comsystem: dbref %d out of range [0, %d)" ENDLINE, t_user.who, mudstate.db_top);
                }
            }
            ch->num_users = jAdded;
            sort_users(ch);
        }
        else
        {
            ch->users = NULL;
        }
    }
}

void save_comsystem(FILE *fp)
{
    struct channel *ch;
    struct comuser *user;
    int j;

    fprintf(fp, "+V3\n");
    fprintf(fp, "%d\n", num_channels);
    for (ch = (struct channel *)hash_firstentry(&mudstate.channel_htab); ch; ch = (struct channel *)hash_nextentry(&mudstate.channel_htab))
    {
        fprintf(fp, "%s\n", ch->name);
        fprintf(fp, "%s\n", ch->header);

        fprintf(fp, "%d %d %d %d %d %d %d %d\n", ch->type, ch->temp1, ch->temp2, ch->charge, ch->charge_who, ch->amount_col, ch->num_messages, ch->chan_obj);

        // Count the number of 'valid' users to dump.
        //
        int nUsers = 0;
        for (j = 0; j < ch->num_users; j++)
        {
            user = ch->users[j];
            if (user->who >= 0 && user->who < mudstate.db_top)
            {
                nUsers++;
            }
        }

        fprintf(fp, "%d\n", nUsers);
        for (j = 0; j < ch->num_users; j++)
        {
            user = ch->users[j];
            if (user->who >= 0 && user->who < mudstate.db_top)
            {
                user = ch->users[j];
                fprintf(fp, "%d %d %d\n", user->who, user->bUserIsOn, user->ComTitleStatus);
                if (user->title[0] != '\0')
                {
                    fprintf(fp, "t:%s\n", user->title);
                }
                else
                {
                    fprintf(fp, "t:\n");
                }
            }
        }
    }
}

void BuildChannelMessage
(
    BOOL bSpoof,
    const char *pHeader,
    struct comuser *user,
    const char *pPose,
    char **messNormal,
    char **messNoComtitle
)
{
    // Allocate necessary buffers.
    //
    *messNormal = alloc_lbuf("BCM.messNormal");
    *messNoComtitle = NULL;
    if (!bSpoof)
    {
        *messNoComtitle = alloc_lbuf("BCM.messNoComtitle");
    }

    // Comtitle Check
    //
    BOOL hasComTitle = (user->title[0] != '\0');

    char *mnptr  = *messNormal;     // Message without comtitle removal
    char *mncptr = *messNoComtitle; // Message with comtitle removal

    safe_str(pHeader, *messNormal, &mnptr);
    safe_chr(' ', *messNormal, &mnptr);
    if (!bSpoof)
    {
        safe_str(pHeader, *messNoComtitle, &mncptr);
        safe_chr(' ', *messNoComtitle, &mncptr);
    }

    // Don't evaluate a title if there isn't one to parse or evaluation of
    // comtitles is disabled.
    // If they're set spoof, ComTitleStatus doesn't matter.
    if (hasComTitle && (user->ComTitleStatus || bSpoof))
    {
        if (mudconf.eval_comtitle)
        {
            // Evaluate the comtitle as code.
            //
            char TempToEval[LBUF_SIZE];
            strcpy(TempToEval, user->title);
            char *q = TempToEval;
            TinyExec(*messNormal, &mnptr, user->who, user->who, user->who,
                EV_FCHECK | EV_EVAL | EV_TOP, &q, (char **)NULL, 0);
        }
        else
        {
            safe_str(user->title, *messNormal, &mnptr);
        }
        if (!bSpoof)
        {
            safe_chr(' ', *messNormal, &mnptr);
            safe_str(Name(user->who), *messNormal, &mnptr);
            safe_str(Name(user->who), *messNoComtitle, &mncptr);
        }
    }
    else
    {
        safe_str(Name(user->who), *messNormal, &mnptr);
        if (!bSpoof)
        {
            safe_str(Name(user->who), *messNoComtitle, &mncptr);
        }
    }

    if (':' == pPose[0])
    {
        safe_chr(' ', *messNormal, &mnptr);
        safe_str(pPose+1, *messNormal, &mnptr);
        if (!bSpoof)
        {
            safe_chr(' ', *messNoComtitle, &mncptr);
            safe_str(pPose+1, *messNoComtitle, &mncptr);
        }
    }
    else if (';' == pPose[0])
    {
        safe_str(pPose+1, *messNormal, &mnptr);
        if (!bSpoof)
        {
            safe_str(pPose+1, *messNoComtitle, &mncptr);
        }
    }
    else
    {
        safe_str(" says, \"", *messNormal, &mnptr);
        safe_str(pPose, *messNormal, &mnptr);
        safe_chr('"', *messNormal, &mnptr);
        if (!bSpoof)
        {
            safe_str(" says, \"", *messNoComtitle, &mncptr);
            safe_str(pPose, *messNoComtitle, &mncptr);
            safe_chr('"', *messNoComtitle, &mncptr);
        }
    }
    *mnptr = '\0';
    if (!bSpoof)
    {
        *mncptr = '\0';
    }
}

void do_processcom(dbref player, char *arg1, char *arg2)
{
    if ((strlen(arg1) + strlen(arg2)) > 3500)
    {
        arg2[3500] = '\0'; // TODO: Incorrect logic that doesn't hurt anything.
    }
    if (!*arg2)
    {
        raw_notify(player, "No message.");
        return;
    }
    struct channel *ch = select_channel(arg1);
    if (!ch)
    {
        raw_notify(player, tprintf("Unknown channel %s.", arg1));
        return;
    }
    struct comuser *user = select_user(ch, player);
    if (!user)
    {
        raw_notify(player, "You are not listed as on that channel.  Delete this alias and readd.");
        return;
    }
    if (  Gagged(player)
       && !Wizard(player))
    {
        raw_notify(player, "GAGGED players may not speak on channels.");
        return;
    }
    if (!strcmp(arg2, "on"))
    {
        do_joinchannel(player, ch);
    }
    else if (!strcmp(arg2, "off"))
    {
        do_leavechannel(player, ch);
    }
    else if (!user->bUserIsOn)
    {
        raw_notify(player, tprintf("You must be on %s to do that.", arg1));
        return;
    }
    else if (!strcmp(arg2, "who"))
    {
        do_comwho(player, ch);
    }
    else if (  !strncmp(arg2, "last", 4)
            && (  arg2[4] == '\0'
               || (  arg2[4] == ' '
                  && is_integer(arg2 + 5, NULL))))
    {
        // Parse optional number after the 'last' command.
        //
        int nRecall = DFLT_RECALL_REQUEST;
        if (arg2[4] == ' ')
        {
            nRecall = Tiny_atol(arg2 + 5);
        }
        do_comlast(player, ch, nRecall);
    }
    else if (!do_test_access(player, CHANNEL_TRANSMIT, ch))
    {
        raw_notify(player, "That channel type cannot be transmitted on.");
        return;
    }
    else
    {
        if (!payfor(player, Guest(player) ? 0 : ch->charge))
        {
            notify(player, tprintf("You don't have enough %s.", mudconf.many_coins));
            return;
        }
        else
        {
            ch->amount_col += ch->charge;
            giveto(ch->charge_who, ch->charge);
        }

        // BuildChannelMessage allocates messNormal and messNoComtitle,
        // SendChannelMessage frees them.
        //
        char *messNormal;
        char *messNoComtitle;
        BuildChannelMessage((ch->type & CHANNEL_SPOOF) != 0, ch->header, user,
            arg2, &messNormal, &messNoComtitle);
        SendChannelMessage(player, ch, messNormal, messNoComtitle, FALSE);
    }
}

void SendChannelMessage
(
    dbref player,
    struct channel *ch,
    char *msgNormal,
    char *msgNoComtitle,
    BOOL bRaw
)
{
    BOOL bSpoof = ((ch->type & CHANNEL_SPOOF) != 0);
    ch->num_messages++;

    struct comuser *user;
    for (user = ch->on_users; user; user = user->on_next)
    {
        if (  user->bUserIsOn
           && do_test_access(user->who, CHANNEL_RECEIVE, ch))
        {
            if (  user->ComTitleStatus
               || bSpoof
               || msgNoComtitle == NULL)
            {
                notify_with_cause_ooc(user->who, player, msgNormal);
            }
            else
            {
                notify_with_cause_ooc(user->who, player, msgNoComtitle);
            }
        }
    }
        
    dbref obj = ch->chan_obj;
    if (Good_obj(obj))
    {
        dbref aowner;
        int aflags;
        int logmax = DFLT_MAX_LOG;
        char *maxbuf;
        ATTR *attr;
        if (  (attr = atr_str("MAX_LOG"))
           && attr->number)
        {
            maxbuf = atr_get(obj, attr->number, &aowner, &aflags);
            logmax = Tiny_atol(maxbuf);
            free_lbuf(maxbuf);
        }
        if (logmax > 0)
        {
            if (logmax > MAX_RECALL_REQUEST)
            {
                logmax = MAX_RECALL_REQUEST;
                atr_add(ch->chan_obj, attr->number, Tiny_ltoa_t(logmax), GOD,
                    AF_CONST|AF_NOPROG|AF_NOPARSE);
            }
            int atr = mkattr(tprintf("HISTORY_%d", iMod(ch->num_messages, logmax)));
            atr_add(ch->chan_obj, atr, msgNormal, GOD, AF_CONST|AF_NOPROG|AF_NOPARSE);
        }
    }
    else if (ch->chan_obj != NOTHING)
    {
        ch->chan_obj = NOTHING;
    }

    // Since msgNormal and msgNoComTitle are no longer needed, free them here.
    //
    if (!bRaw)
    {
        if (msgNormal)
        {
            free_lbuf(msgNormal);
        }
        if (msgNoComtitle)
        {
            free_lbuf(msgNoComtitle);
        }
    }
}

void do_joinchannel(dbref player, struct channel *ch)
{
    struct comuser **cu;
    int i;

    struct comuser *user = select_user(ch, player);

    if (!user)
    {
        ch->num_users++;
        if (ch->num_users >= ch->max_users)
        {
            ch->max_users += 10;
            cu = (struct comuser **)MEMALLOC(sizeof(struct comuser *) * ch->max_users);
            (void)ISOUTOFMEMORY(cu);

            for (i = 0; i < (ch->num_users - 1); i++)
            {
                cu[i] = ch->users[i];
            }
            MEMFREE(ch->users);
            ch->users = cu;
        }
        user = (struct comuser *)MEMALLOC(sizeof(struct comuser));
        (void)ISOUTOFMEMORY(user);

        for (i = ch->num_users - 1; i > 0 && ch->users[i - 1]->who > player; i--)
        {
            ch->users[i] = ch->users[i - 1];
        }
        ch->users[i] = user;

        user->who            = player;
        user->bUserIsOn      = TRUE;
        user->ComTitleStatus = TRUE;
        user->title          = StringClone("");

        // if (Connected(player))&&(isPlayer(player))
        //
        if (UNDEAD(player))
        {
            user->on_next = ch->on_users;
            ch->on_users  = user;
        }
    }
    else if (!user->bUserIsOn)
    {
        user->bUserIsOn = TRUE;
    }
    else
    {
        raw_notify(player, tprintf("You are already on channel %s.", ch->name));
        return;
    }

    if (!Dark(player))
    {
        char *messNormal, *messNoComtitle;
        BuildChannelMessage((ch->type & CHANNEL_SPOOF) != 0, ch->header, user,
            ":has joined this channel.", &messNormal, &messNoComtitle);
        SendChannelMessage(player, ch, messNormal, messNoComtitle, FALSE);
    }
}

void do_leavechannel(dbref player, struct channel *ch)
{
    struct comuser *user = select_user(ch, player);
    raw_notify(player, tprintf("You have left channel %s.", ch->name));
    if (user->bUserIsOn && !Dark(player))
    {
        char *messNormal, *messNoComtitle;
        BuildChannelMessage((ch->type & CHANNEL_SPOOF) != 0, ch->header, user,
            ":has left this channel.", &messNormal, &messNoComtitle);
        SendChannelMessage(player, ch, messNormal, messNoComtitle, FALSE);
    }
    user->bUserIsOn = FALSE;
}

void do_comwho_line
(
    dbref player,
    struct channel *ch,
    struct comuser *user
)
{
    char *msg;
    char *buff = NULL;

    if (user->title[0] != '\0')
    {
        // There is a comtitle
        //
        if (Staff(player))
        {
            buff = unparse_object(player, user->who, FALSE);
            if (ch->type & CHANNEL_SPOOF)
            {
                msg = tprintf("%s as %s", buff, user->title);
            }
            else
            {
                msg = tprintf("%s as %s %s", buff, user->title, buff);
            }
        }
        else
        {
            if (ch->type & CHANNEL_SPOOF)
            {
                msg = user->title;
            }
            else
            {
                buff = unparse_object(player, user->who, FALSE);
                msg = tprintf("%s %s", user->title, buff);
            }
        }
    }
    else
    {
        buff = unparse_object(player, user->who, FALSE);
        msg = buff;
    }

    raw_notify(player, msg);
    if (buff)
    {
        free_lbuf(buff);
    }
}

void do_comwho(dbref player, struct channel *ch)
{
    struct comuser *user;

    raw_notify(player, "-- Players --");
    for (user = ch->on_users; user; user = user->on_next)
    {
        if (isPlayer(user->who))
        {
            if (Connected(user->who) && (!Dark(user->who) || Wizard_Who(player)))
            {
                if (user->bUserIsOn)
                {
                    do_comwho_line(player, ch, user);
                }
            }
            else if (!Dark(user->who))
            {
                do_comdisconnectchannel(user->who, ch->name);
            }
        }
    }
    raw_notify(player, "-- Objects --");
    for (user = ch->on_users; user; user = user->on_next)
    {
        if (!isPlayer(user->who))
        {
            if (Going(user->who) && God(Owner(user->who)))
            {
                do_comdisconnectchannel(user->who, ch->name);
            }
            else if (user->bUserIsOn)
            {
                do_comwho_line(player, ch, user);
            }
        }
    }
    raw_notify(player, tprintf("-- %s --", ch->name));
}

void do_comlast(dbref player, struct channel *ch, int arg)
{ 
    if (!Good_obj(ch->chan_obj))
    {
        raw_notify(player, "Channel does not have an object.");
        return;
    }
    dbref aowner;
    int aflags;
    dbref obj = ch->chan_obj;
    int logmax = MAX_RECALL_REQUEST;
    ATTR *attr;
    if (  (attr = atr_str("MAX_LOG"))
       && (atr_get_info(obj, attr->number, &aowner, &aflags)))
    {
        char *maxbuf = atr_get(obj, attr->number, &aowner, &aflags);
        logmax = Tiny_atol(maxbuf);
        free_lbuf(maxbuf);
    }
    if (logmax < 1)
    {
        raw_notify(player, "Channel does not log.");
        return;
    }
    if (arg < MIN_RECALL_REQUEST)
    {
        arg = MIN_RECALL_REQUEST;
    }
    if (arg > logmax)
    {
        arg = logmax;
    }

    char *message = alloc_lbuf("do_comlast");
    int histnum = ch->num_messages - arg;

    raw_notify(player, "-- Begin Comsys Recall --");
    for (int count = 0; count < arg; count++)
    {
        histnum++;
        attr = atr_str(tprintf("HISTORY_%d", iMod(histnum, logmax)));
        if (attr)
        {
            message = atr_get(obj, attr->number, &aowner, &aflags);
        }
        raw_notify(player, message);
    }
    raw_notify(player, "-- End Comsys Recall --");
    free_lbuf(message);
}

BOOL do_chanlog(dbref player, char *channel, char *arg)
{
    int value;
    if (  !*arg
       || !is_integer(arg, NULL)
       || (value = Tiny_atol(arg)) > MAX_RECALL_REQUEST)
    {
        return FALSE;
    }
    if (value < 0)
    {
        value = 0;
    }
    struct channel *ch = select_channel(channel);
    if (!Good_obj(ch->chan_obj))
    {
        // No channel object has been set.
        //
        return FALSE;
    }
    int atr = mkattr("MAX_LOG");
    char *oldvalue;
    dbref aowner;
    int aflags;
    if (oldvalue = atr_get(ch->chan_obj, atr, &aowner, &aflags))
    {
        int oldnum = Tiny_atol(oldvalue);
        if (oldnum > value)
        {
            ATTR *hist;
            for (int count = 0; count <= oldnum; count++)
            {
                hist = atr_str(tprintf("HISTORY_%d", count));
                if (hist)
                {
                    atr_clr(ch->chan_obj, hist->number);
                }
            }
        }
        free_lbuf(oldvalue);
    }
    atr_add(ch->chan_obj, atr, Tiny_ltoa_t(value), GOD, AF_CONST|AF_NOPROG|AF_NOPARSE);
    return TRUE;
}

struct channel *select_channel(char *channel)
{
    struct channel *cp = (struct channel *)hashfindLEN(channel, strlen(channel), &mudstate.channel_htab);
    if (!cp)
    {
        return NULL;
    }
    else
    {
        return cp;
    }
}

struct comuser *select_user(struct channel *ch, dbref player)
{
    int first, last, current;
    int dir;

    if (!ch)
    {
        return NULL;
    }

    first = 0;
    last = ch->num_users - 1;
    dir = 1;
    current = (first + last) / 2;

    while (dir && (first <= last))
    {
        current = (first + last) / 2;
        if (ch->users[current] == NULL)
        {
            last--;
            continue;
        }
        if (ch->users[current]->who == player)
            dir = 0;
        else if (ch->users[current]->who < player)
        {
            dir = 1;
            first = current + 1;
        }
        else
        {
            dir = -1;
            last = current - 1;
        }
    }

    if (!dir)
        return ch->users[current];
    else
        return NULL;
}

#define MAX_ALIASES_PER_PLAYER 50

void do_addcom
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int   key,
    int   nargs,
    char *arg1,
    char *arg2
)
{
    if (!mudconf.have_comsys)
    {
        raw_notify(executor, "Comsys disabled.");
        return;
    }
    BOOL bValidAlias;
    int  nValidAlias;
    char *pValidAlias = MakeCanonicalComAlias(arg1, &nValidAlias, &bValidAlias);
    if (!bValidAlias)
    {
        raw_notify(executor, "You need to specify a valid alias.");
        return;
    }
    char *s = arg2;
    if (!*s)
    {
        raw_notify(executor, "You need to specify a channel.");
        return;
    }
    char channel[MAX_CHANNEL_LEN+1];
    char *t = channel;
    while (*s && ((t - channel) < MAX_CHANNEL_LEN))
    {
        if (*s != ' ')
            *t++ = *s++;
        else
            s++;
    }
    *t = '\0';

    int i, j, where;
    char *na;
    char **nc;
    struct channel *ch = select_channel(channel);
    char Buffer[MAX_CHANNEL_LEN+1];
    if (!ch)
    {
        int nVisualWidth;
        ANSI_TruncateToField(channel, sizeof(Buffer), Buffer, sizeof(Buffer), &nVisualWidth, ANSI_ENDGOAL_NORMAL);
        raw_notify(executor, tprintf("Channel %s does not exist yet.", Buffer));
        return;
    }
    if (!do_test_access(executor, CHANNEL_JOIN, ch))
    {
        raw_notify(executor, "Sorry, this channel type does not allow you to join.");
        return;
    }
    comsys_t *c = get_comsys(executor);
    if (c->numchannels >= MAX_ALIASES_PER_PLAYER)
    {
        raw_notify(executor, tprintf("Sorry, but you have reached the maximum number of aliases allowed."));
        return;
    }
    for (j = 0; j < c->numchannels && (strcmp(pValidAlias, c->alias + j * ALIAS_SIZE) > 0); j++)
    {
        // Nothing
        ;
    }
    if (j < c->numchannels && !strcmp(pValidAlias, c->alias + j * ALIAS_SIZE))
    {
        char *p = tprintf("That alias is already in use for channel %s.", c->channels[j]);
        raw_notify(executor, p);
        return;
    }
    if (c->numchannels >= c->maxchannels)
    {
        c->maxchannels += 10;

        na = (char *)MEMALLOC(ALIAS_SIZE * c->maxchannels);
        (void)ISOUTOFMEMORY(na);
        nc = (char **)MEMALLOC(sizeof(char *) * c->maxchannels);
        (void)ISOUTOFMEMORY(nc);

        for (i = 0; i < c->numchannels; i++)
        {
            strcpy(na + i * ALIAS_SIZE, c->alias + i * ALIAS_SIZE);
            nc[i] = c->channels[i];
        }
        if (c->alias)
        {
            MEMFREE(c->alias);
            c->alias = NULL;
        }
        if (c->channels)
        {
            MEMFREE(c->channels);
            c->channels = NULL;
        }
        c->alias = na;
        c->channels = nc;
    }
    where = c->numchannels++;
    for (i = where; i > j; i--)
    {
        strcpy(c->alias + i * ALIAS_SIZE, c->alias + (i - 1) * ALIAS_SIZE);
        c->channels[i] = c->channels[i - 1];
    }

    where = j;
    memcpy(c->alias + where * ALIAS_SIZE, pValidAlias, nValidAlias);
    *(c->alias + where * ALIAS_SIZE + nValidAlias) = '\0';
    c->channels[where] = StringClone(channel);

    if (!select_user(ch, executor))
    {
        do_joinchannel(executor, ch);
    }

    raw_notify(executor, tprintf("Channel %s added with alias %s.", channel, pValidAlias));
}

void do_delcom(dbref executor, dbref caller, dbref enactor, int key, char *arg1)
{
    if (!mudconf.have_comsys)
    {
        raw_notify(executor, "Comsys disabled.");
        return;
    }
    if (!arg1)
    {
        raw_notify(executor, "Need an alias to delete.");
        return;
    }
    comsys_t *c = get_comsys(executor);
    int i;

    for (i = 0; i < c->numchannels; i++)
    {
        if (!strcmp(arg1, c->alias + i * ALIAS_SIZE))
        {
            int itmp, found=0;
            for (itmp = 0;itmp < c->numchannels; itmp++)
            {
                if (!strcmp(c->channels[itmp],c->channels[i]))
                {
                    found++;
                }
            }

            // If we found no other channels, delete it
            //
            if (found <= 1)
            {
                do_delcomchannel(executor, c->channels[i], FALSE);
                raw_notify(executor, tprintf("Channel %s deleted.", c->channels[i]));
                MEMFREE(c->channels[i]);
            }
            else
            {
                raw_notify(executor, tprintf("Alias for channel %s deleted.",
                                           c->channels[i]));
            }
            
            c->channels[i]=NULL;
            c->numchannels--;
            
            for (; i < c->numchannels; i++)
            {
                strcpy(c->alias + i * ALIAS_SIZE, c->alias + (i + 1) * ALIAS_SIZE);
                c->channels[i] = c->channels[i + 1];
            }
            return;
        }
    }
    raw_notify(executor, "Unable to find that alias.");
}

void do_delcomchannel(dbref player, char *channel, BOOL bQuiet)
{
    struct comuser *user;

    struct channel *ch = select_channel(channel);
    if (!ch)
    {
        raw_notify(player, tprintf("Unknown channel %s.", channel));
    }
    else
    {
        int i;
        int j = 0;
        for (i = 0; i < ch->num_users && !j; i++)
        {
            user = ch->users[i];
            if (user->who == player)
            {
                do_comdisconnectchannel(player, channel);
                if (!bQuiet)
                {
                    if (user->bUserIsOn && (!Dark(player)))
                    {
                        char *messNormal, *messNoComtitle;
                        BuildChannelMessage((ch->type & CHANNEL_SPOOF) != 0,
                                            ch->header, user, ":has left this channel.",
                                            &messNormal, &messNoComtitle);
                        SendChannelMessage(player, ch, messNormal, messNoComtitle, FALSE);
                    }
                    raw_notify(player, tprintf("You have left channel %s.", channel));
                }
                

                if (user->title)
                {
                    MEMFREE(user->title);
                    user->title = NULL;
                }
                MEMFREE(user);
                user = NULL;
                j = 1;
            }
        }

        if (j)
        {
            ch->num_users--;
            for (i--; i < ch->num_users; i++)
            {
                ch->users[i] = ch->users[i + 1];
            }
        }
    }
}

void do_createchannel(dbref executor, dbref caller, dbref enactor, int key, char *channel)
{
    struct channel *newchannel;

    if (select_channel(channel))
    {
        raw_notify(executor, tprintf("Channel %s already exists.", channel));
        return;
    }
    if (!*channel)
    {
        raw_notify(executor, "You must specify a channel to create.");
        return;
    }
    if (!(Comm_All(executor)))
    {
        raw_notify(executor, "You do not have permission to do that.");
        return;
    }
    newchannel = (struct channel *)MEMALLOC(sizeof(struct channel));
    (void)ISOUTOFMEMORY(newchannel);

    int   vwChannel;
    unsigned int nNameNoANSI;
    char *pNameNoANSI;
    char Buffer[MAX_HEADER_LEN];
    int nChannel = ANSI_TruncateToField(channel, sizeof(Buffer),
        Buffer, sizeof(Buffer), &vwChannel, ANSI_ENDGOAL_NORMAL);
    if (nChannel == vwChannel)
    {
        // The channel name does not contain ANSI, so first, we add some to
        // get the header.
        //
        const int nMax = MAX_HEADER_LEN - (sizeof(ANSI_HILITE)-1)
                       - (sizeof(ANSI_NORMAL)-1) - 2;
        if (nChannel > nMax)
        {
            nChannel = nMax;
        }
        Buffer[nChannel] = '\0';
        sprintf(newchannel->header, "%s[%s]%s", ANSI_HILITE, Buffer,
            ANSI_NORMAL);

        // Then, we use the non-ANSI part for the name.
        //
        nNameNoANSI = nChannel;
        pNameNoANSI = Buffer;
    }
    else
    {
        // The given channel name does contain ANSI.
        //
        memcpy(newchannel->header, Buffer, nChannel+1);
        pNameNoANSI = strip_ansi(Buffer, &nNameNoANSI);
    }
    if (nNameNoANSI > MAX_CHANNEL_LEN)
    {
        nNameNoANSI = MAX_CHANNEL_LEN;
    }
    memcpy(newchannel->name, pNameNoANSI, nNameNoANSI);
    newchannel->name[nNameNoANSI] = '\0';

    newchannel->type = 127;
    newchannel->temp1 = 0;
    newchannel->temp2 = 0;
    newchannel->charge = 0;
    newchannel->charge_who = executor;
    newchannel->amount_col = 0;
    newchannel->num_users = 0;
    newchannel->max_users = 0;
    newchannel->users = NULL;
    newchannel->on_users = NULL;
    newchannel->chan_obj = NOTHING;
    newchannel->num_messages = 0;

    num_channels++;

    hashaddLEN(newchannel->name, strlen(newchannel->name), (int *)newchannel, &mudstate.channel_htab);

    // Report the channel creation using non-ANSI name.
    //
    raw_notify(executor, tprintf("Channel %s created.", newchannel->name));
}

void do_destroychannel(dbref executor, dbref caller, dbref enactor, int key, char *channel)
{
    struct channel *ch;
    int j;

    if (!mudconf.have_comsys)
    {
        raw_notify(executor, "Comsys disabled.");
        return;
    }
    ch = (struct channel *)hashfindLEN(channel, strlen(channel), &mudstate.channel_htab);

    if (!ch)
    {
        raw_notify(executor, tprintf("Could not find channel %s.", channel));
        return;
    }
    else if (!(Comm_All(executor)) && (executor != ch->charge_who))
    {
        raw_notify(executor, "You do not have permission to do that. ");
        return;
    }
    num_channels--;
    hashdeleteLEN(channel, strlen(channel), &mudstate.channel_htab);

    for (j = 0; j < ch->num_users; j++)
    {
        MEMFREE(ch->users[j]);
        ch->users[j] = NULL;
    }
    MEMFREE(ch->users);
    ch->users = NULL;
    MEMFREE(ch);
    ch = NULL;
    raw_notify(executor, tprintf("Channel %s destroyed.", channel));
}

#if 0
void do_cleanupchannels(void)
{
    struct channel *ch;
    for (ch = (struct channel *)hash_firstentry(&mudstate.channel_htab);
         ch; ch = (struct channel *)hash_nextentry(&mudstate.channel_htab))
    {
        struct comuser *user, *prevuser = NULL;
        for (user = ch->on_users; user; )
        {
            if (isPlayer(user->who))
            {
                if (!(do_test_access(user->who, CHANNEL_JOIN, ch)))
                //if (!Connected(user->who))
                {
                    // Go looking for user in the array.
                    //
                    BOOL bFound = FALSE;
                    int iPos;
                    for (iPos = 0; iPos < ch->num_users && !bFound; iPos++)
                    {
                        if (ch->users[iPos] == user)
                        {
                            bFound = TRUE;
                        }
                    }

                    if (bFound)
                    {
                        // Remove user from the array.
                        //
                        ch->num_users--;
                        for (iPos--; iPos < ch->num_users; iPos++)
                        {
                            ch->users[iPos] = ch->users[iPos+1];
                        }

                        // Save user pointer for later reporting and freeing.
                        //
                        struct comuser *cuVictim = user;

                        // Unlink user from the list, and decide who to look at next.
                        //
                        if (prevuser)
                        {
                            prevuser->on_next = user->on_next;
                        }
                        else
                        {
                            ch->on_users = user->on_next;
                        }
                        user = user->on_next;

                        // Reporting
                        //
                        if (!Dark(cuVictim->who))
                        {
                            char *mess = StartBuildChannelMessage(cuVictim->who,
                                (ch->type & CHANNEL_SPOOF) != 0, ch->header, cuVictim->title,
                                Name(cuVictim->who), ":is booted off the channel by the system.");
                            do_comsend(ch, mess);
                            EndBuildChannelMessage(mess);
                        }
                        raw_notify(cuVictim->who, tprintf("The system has booted you off channel %s.", ch->name));

                        // Freeing
                        //
                        if (cuVictim->title)
                        {
                            MEMFREE(cuVictim->title);
                            cuVictim->title = NULL;
                        }
                        MEMFREE(cuVictim);
                        cuVictim = NULL;

                        continue;
                    }
                }
            }

            prevuser = user;
            user = user->on_next;
        }
    }
}
#endif

void do_listchannels(dbref player)
{
    struct channel *ch;
    char temp[LBUF_SIZE];

    BOOL perm = Comm_All(player);
    if (!perm)
    {
        raw_notify(player, "Warning: Only public channels and your channels will be shown.");
    }
    raw_notify(player, "*** Channel      --Flags--  Obj   Own   Charge  Balance  Users   Messages");

    for (ch = (struct channel *)hash_firstentry(&mudstate.channel_htab);
         ch; ch = (struct channel *)hash_nextentry(&mudstate.channel_htab))
    {
        if (perm || (ch->type & CHANNEL_PUBLIC) || ch->charge_who == player)
        {

            sprintf(temp, "%c%c%c %-13.13s %c%c%c/%c%c%c %5d %5d %8d %8d %6d %10d",
                (ch->type & (CHANNEL_PUBLIC)) ? 'P' : '-',
                (ch->type & (CHANNEL_LOUD)) ? 'L' : '-',
                (ch->type & (CHANNEL_SPOOF)) ? 'S' : '-',
                ch->name,
                (ch->type & (CHANNEL_PL_MULT * CHANNEL_JOIN)) ? 'J' : '-',
                (ch->type & (CHANNEL_PL_MULT * CHANNEL_TRANSMIT)) ? 'X' : '-',
                (ch->type & (CHANNEL_PL_MULT * CHANNEL_RECEIVE)) ? 'R' : '-',
                (ch->type & (CHANNEL_OBJ_MULT * CHANNEL_JOIN)) ? 'j' : '-',
                (ch->type & (CHANNEL_OBJ_MULT * CHANNEL_TRANSMIT)) ? 'x' : '-',
                (ch->type & (CHANNEL_OBJ_MULT * CHANNEL_RECEIVE)) ? 'r' : '-',
                (ch->chan_obj != NOTHING) ? ch->chan_obj : -1,
                ch->charge_who, ch->charge, ch->amount_col, ch->num_users, ch->num_messages);
            raw_notify(player, temp);
        }
    }
    raw_notify(player, "-- End of list of Channels --");
}

void do_comtitle
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int   key,
    int   nargs,
    char *arg1,
    char *arg2
)
{
    char channel[MAX_CHANNEL_LEN+1];

    if (!mudconf.have_comsys)
    {
        raw_notify(executor, "Comsys disabled.");
        return;
    }
    if (!*arg1)
    {
        raw_notify(executor, "Need an alias to do comtitle.");
        return;
    }
    strcpy(channel, get_channel_from_alias(executor, arg1));

    if (channel[0] == '\0')
    {
        raw_notify(executor, "Unknown alias.");
        return;
    }
    struct channel *ch = select_channel(channel);
    if (ch)
    {
        if (select_user(ch, executor))
        {
            if (key == COMTITLE_OFF)
            {
                if ((ch->type & CHANNEL_SPOOF) == 0)
                {
                    raw_notify(executor, tprintf("Comtitles are now off for channel %s", channel));
                    do_setcomtitlestatus(executor, ch, FALSE);
                }
                else
                {
                    raw_notify(executor, "You can not turn off comtitles on that channel.");
                }
            }
            else if (key == COMTITLE_ON)
            {
                raw_notify(executor, tprintf("Comtitles are now on for channel %s", channel));
                do_setcomtitlestatus(executor, ch, TRUE);
            }
            else
            {
                char *pValidatedTitleValue = RestrictTitleValue(arg2);
                do_setnewtitle(executor, ch, pValidatedTitleValue);
                raw_notify(executor, tprintf("Title set to '%s' on channel %s.",
                    pValidatedTitleValue, channel));
            }
        }
    }
    else
    {
        raw_notify(executor, "Illegal comsys alias, please delete.");
    }
}

void do_comlist(dbref executor, dbref caller, dbref enactor, int key)
{
    comsys_t *c;
    int i;

    if (!mudconf.have_comsys)
    {
        raw_notify(executor, "Comsys disabled.");
        return;
    }
    c = get_comsys(executor);

    raw_notify(executor, "Alias     Channel            Status   Title");

    for (i = 0; i < c->numchannels; i++)
    {
        struct comuser *user = select_user(select_channel(c->channels[i]), executor);
        if (user)
        {
            char *p = tprintf("%-9.9s %-18.18s %s %s %s", c->alias + i * ALIAS_SIZE, c->channels[i], (user->bUserIsOn ? "on " : "off"), (user->ComTitleStatus ? "con " : "coff"), user->title);
            raw_notify(executor, p);
        }
        else
        {
            raw_notify(executor, tprintf("Bad Comsys Alias: %s for Channel: %s", c->alias + i * ALIAS_SIZE, c->channels[i]));
        }
    }
    raw_notify(executor, "-- End of comlist --");
}

void do_channelnuke(dbref player)
{
    struct channel *ch;
    int j;

    for (ch = (struct channel *)hash_firstentry(&mudstate.channel_htab);
         ch; ch = (struct channel *)hash_nextentry(&mudstate.channel_htab))
    {
        if (ch->charge_who == player)
        {
            num_channels--;
            hashdeleteLEN(ch->name, strlen(ch->name), &mudstate.channel_htab);

            for (j = 0; j < ch->num_users; j++)
            {
                MEMFREE(ch->users[j]);
                ch->users[j] = NULL;
            }
            MEMFREE(ch->users);
            ch->users = NULL;
            MEMFREE(ch);
            ch = NULL;
        }
    }
}

void do_clearcom(dbref executor, dbref caller, dbref enactor, int unused2)
{
    int i;
    comsys_t *c;

    if (!mudconf.have_comsys)
    {
        raw_notify(executor, "Comsys disabled.");
        return;
    }
    c = get_comsys(executor);

    for (i = (c->numchannels) - 1; i > -1; --i)
    {
        do_delcom(executor, caller, enactor, 0, c->alias + i * ALIAS_SIZE);
    }
}

void do_allcom(dbref executor, dbref caller, dbref enactor, int key, char *arg1)
{
    int i;
    comsys_t *c;

    if (!mudconf.have_comsys)
    {
        raw_notify(executor, "Comsys disabled.");
        return;
    }
    c = get_comsys(executor);

    if (  strcmp(arg1, "who") != 0
       && strcmp(arg1, "on")  != 0
       && strcmp(arg1, "off") != 0)
    {
        raw_notify(executor, "Only options available are: on, off and who.");
        return;
    }
    for (i = 0; i < c->numchannels; i++)
    {
        do_processcom(executor, c->channels[i], arg1);
        if (strcmp(arg1, "who") == 0)
        {
            raw_notify(executor, "");
        }
    }
}

void sort_users(struct channel *ch)
{
    int i;
    int nu;
    BOOL done = FALSE;
    struct comuser *user;

    nu = ch->num_users;
    while (!done)
    {
        done = TRUE;
        for (i = 0; i < (nu - 1); i++)
        {
            if (ch->users[i]->who > ch->users[i + 1]->who)
            {
                user = ch->users[i];
                ch->users[i] = ch->users[i + 1];
                ch->users[i + 1] = user;
                done = FALSE;
            }
        }
    }
}

void do_channelwho(dbref executor, dbref caller, dbref enactor, int key, char *arg1)
{
    struct comuser *user;
    char channel[MAX_CHANNEL_LEN+1];
    char *s, *t, *buff;
    char temp[LBUF_SIZE];
    int i;

    if (!mudconf.have_comsys)
    {
        raw_notify(executor, "Comsys disabled.");
        return;
    }
    s = arg1;
    t = channel;
    while (*s && *s != '/' && ((t - channel) < MAX_CHANNEL_LEN))
    {
        *t++ = *s++;
    }
    *t = 0;

    BOOL flag = FALSE;
    if (*s && *(s + 1))
    {
        flag = (*(s + 1) == 'a');
    }

    struct channel *ch = select_channel(channel);
    if (!ch)
    {
        raw_notify(executor, tprintf("Unknown channel %s.", channel));
        return;
    }
    if (!((Comm_All(executor)) || (executor == ch->charge_who)))
    {
        raw_notify(executor, "You do not have permission to do that. (Not owner or admin.)");
        return;
    }
    raw_notify(executor, tprintf("-- %s --", ch->name));
    raw_notify(executor, tprintf("%-29.29s %-6.6s %-6.6s", "Name", "Status", "Player"));
    for (i = 0; i < ch->num_users; i++)
    {
        user = ch->users[i];
        if ((flag || UNDEAD(user->who)) && (!Dark(user->who) || Wizard_Who(executor)))
        {
            buff = unparse_object(executor, user->who, FALSE);
            sprintf(temp, "%-29.29s %-6.6s %-6.6s", strip_ansi(buff),
                user->bUserIsOn ? "on " : "off",
                isPlayer(user->who) ? "yes" : "no ");
            raw_notify(executor, temp);
            free_lbuf(buff);
        }
    }
    raw_notify(executor, tprintf("-- %s --", ch->name));
}

void do_comdisconnectraw_notify(dbref player, char *chan)
{
    struct channel *ch = select_channel(chan);
    if (!ch) return;

    struct comuser *cu = select_user(ch, player);
    if (!cu) return;

    if ((ch->type & CHANNEL_LOUD) && (cu->bUserIsOn) && (!Dark(player)))
    {
        char *messNormal, *messNoComtitle;
        BuildChannelMessage((ch->type & CHANNEL_SPOOF) != 0, ch->header, cu,
            ":has disconnected.", &messNormal, &messNoComtitle);
        SendChannelMessage(player, ch, messNormal, messNoComtitle, FALSE);
    }
}

void do_comconnectraw_notify(dbref player, char *chan)
{
    struct channel *ch = select_channel(chan);
    if (!ch) return;
    struct comuser *cu = select_user(ch, player);
    if (!cu) return;

    if ((ch->type & CHANNEL_LOUD) && (cu->bUserIsOn) && (!Dark(player)))
    {
        char *messNormal, *messNoComtitle;
        BuildChannelMessage((ch->type & CHANNEL_SPOOF) != 0, ch->header, cu,
            ":has connected.", &messNormal, &messNoComtitle);
        SendChannelMessage(player, ch, messNormal, messNoComtitle, FALSE);
    }
}

void do_comconnectchannel(dbref player, char *channel, char *alias, int i)
{
    struct comuser *user;

    struct channel *ch = select_channel(channel);
    if (ch)
    {
        for (user = ch->on_users;
        user && user->who != player;
        user = user->on_next) ;

        if (!user)
        {
            user = select_user(ch, player);
            if (user)
            {
                user->on_next = ch->on_users;
                ch->on_users = user;
            }
            else
            {
                raw_notify(player, tprintf("Bad Comsys Alias: %s for Channel: %s", alias + i * ALIAS_SIZE, channel));
            }
        }
    }
    else
    {
        raw_notify(player, tprintf("Bad Comsys Alias: %s for Channel: %s", alias + i * ALIAS_SIZE, channel));
    }
}

void do_comdisconnect(dbref player)
{
    int i;
    comsys_t *c;

    c = get_comsys(player);

    for (i = 0; i < c->numchannels; i++)
    {
        do_comdisconnectchannel(player, c->channels[i]);
        do_comdisconnectraw_notify(player, c->channels[i]);
    }
}

void do_comconnect(dbref player)
{
    comsys_t *c;
    int i;

    c = get_comsys(player);

    for (i = 0; i < c->numchannels; i++)
    {
        do_comconnectchannel(player, c->channels[i], c->alias, i);
        do_comconnectraw_notify(player, c->channels[i]);
    }
}


void do_comdisconnectchannel(dbref player, char *channel)
{
    struct comuser *prevuser = NULL;
    struct channel *ch = select_channel(channel);
    if (!ch)
        return;

    struct comuser *user;
    for (user = ch->on_users; user;)
    {
        if (user->who == player)
        {
            if (prevuser)
                prevuser->on_next = user->on_next;
            else
                ch->on_users = user->on_next;
            return;
        }
        else
        {
            prevuser = user;
            user = user->on_next;
        }
    }
}

void do_editchannel
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int   flag,
    int   nargs,
    char *arg1,
    char *arg2
)
{
    if (!mudconf.have_comsys)
    {
        raw_notify(executor, "Comsys disabled.");
        return;
    }
    struct channel *ch = select_channel(arg1);
    if (!ch)
    {
        raw_notify(executor, tprintf("Unknown channel %s.", arg1));
        return;
    }
    if (!((Comm_All(executor)) || (executor == ch->charge_who)))
    {
        raw_notify(executor, "You do not have permission to do that. (Not owner or Admin.)");
        return;
    }

    BOOL add_remove = TRUE;
    char *s = arg2;
    if (*s == '!')
    {
        add_remove = FALSE;
        s++;
    }
    switch (flag)
    {
    case 0:
        {
            dbref who = lookup_player(executor, arg2, TRUE);
            if (NOTHING == who)
            {
                raw_notify(executor, "Invalid player.");
            }
            else
            {
                ch->charge_who = who;
                raw_notify(executor, "Set.");
            }
        }
        break;

    case 1:
        ch->charge = Tiny_atol(arg2);
        raw_notify(executor, "Set.");
        break;

    case 3:
        if (strcmp(s, "join") == 0)
        {
            if (add_remove)
            {
                ch->type |= (CHANNEL_PL_MULT * CHANNEL_JOIN);
                raw_notify(executor, "@cpflags: Set.");
            }
            else
            {
                ch->type &= ~(CHANNEL_PL_MULT * CHANNEL_JOIN);
                raw_notify(executor, "@cpflags: Cleared.");
            }
        }
        else if (strcmp(s, "receive") == 0)
        {
            if (add_remove)
            {
                ch->type |= (CHANNEL_PL_MULT * CHANNEL_RECEIVE);
                raw_notify(executor, "@cpflags: Set.");
            }
            else
            {
                ch->type &= ~(CHANNEL_PL_MULT * CHANNEL_RECEIVE);
                raw_notify(executor, "@cpflags: Cleared.");
            }
        }
        else if (strcmp(s, "transmit") == 0)
        {
            if (add_remove)
            {
                ch->type |= (CHANNEL_PL_MULT * CHANNEL_TRANSMIT);
                raw_notify(executor, "@cpflags: Set.");
            }
            else
            {
                ch->type &= ~(CHANNEL_PL_MULT * CHANNEL_TRANSMIT);
                raw_notify(executor, "@cpflags: Cleared.");
            }
        }
        else
        {
            raw_notify(executor, "@cpflags: Unknown Flag.");
        }
        break;

    case 4:
        if (strcmp(s, "join") == 0)
        {
            if (add_remove)
            {
                ch->type |= (CHANNEL_OBJ_MULT * CHANNEL_JOIN);
                raw_notify(executor, "@coflags: Set.");
            }
            else
            {
                ch->type &= ~(CHANNEL_OBJ_MULT * CHANNEL_JOIN);
                raw_notify(executor, "@coflags: Cleared.");
            }
        }
        else if (strcmp(s, "receive") == 0)
        {
            if (add_remove)
            {
                ch->type |= (CHANNEL_OBJ_MULT * CHANNEL_RECEIVE);
                raw_notify(executor, "@coflags: Set.");
            }
            else
            {
                ch->type &= ~(CHANNEL_OBJ_MULT * CHANNEL_RECEIVE);
                raw_notify(executor, "@coflags: Cleared.");
            }
        }
        else if (strcmp(s, "transmit") == 0)
        {
            if (add_remove)
            {
                ch->type |= (CHANNEL_OBJ_MULT * CHANNEL_TRANSMIT);
                raw_notify(executor, "@coflags: Set.");
            }
            else
            {
                ch->type &= ~(CHANNEL_OBJ_MULT * CHANNEL_TRANSMIT);
                raw_notify(executor, "@coflags: Cleared.");
            }
        }
        else
        {
            raw_notify(executor, "@coflags: Unknown Flag.");
        }
        break;
    }
}

BOOL do_test_access(dbref player, long access, struct channel *chan)
{
    if (Comm_All(player))
    {
        return TRUE;
    }

    // Channel objects allow custom locks for channels.  The normal
    // lock is used to see if they can join that channel. The enterlock
    // is checked to see if they can receive messages on it. The
    // Uselock is checked to see if they can transmit on it. Note:
    // These checks do not supercede the normal channel flags. If a
    // channel is set JOIN for players, ALL players can join the
    // channel, whether or not they pass the lock.  Same for all
    // channel object locks.
    //
    long flag_value = access;
    if (chan->chan_obj != NOTHING && chan->chan_obj != 0)
    {
        if (flag_value & CHANNEL_JOIN)
        {
            if (could_doit(player, chan->chan_obj, A_LOCK))
                return TRUE;
        }
        if (flag_value & CHANNEL_TRANSMIT)
        {
            if (could_doit(player, chan->chan_obj, A_LUSE))
                return TRUE;
        }
        if (flag_value & CHANNEL_RECEIVE)
        {
            if (could_doit(player, chan->chan_obj, A_LENTER))
                return TRUE;
        }
    }

    if (isPlayer(player))
    {
        flag_value *= CHANNEL_PL_MULT;
    }
    else
    {
        flag_value *= CHANNEL_OBJ_MULT;
    }

    // Mask out CHANNEL_PUBLIC, CHANNEL_LOUD, and CHANNEL_SPOOF
    //
    flag_value &= 0xFF;

    return ((long)chan->type & flag_value);
}

// TRUE means continue, FALSE means stop
//
BOOL do_comsystem(dbref who, char *cmd)
{
    char *t;
    char *alias = alloc_lbuf("do_comsystem");
    char *s = alias;
    for (t = cmd; *t && *t != ' '; *s++ = *t++)
    {
        ; // Nothing.
    }

    *s = '\0';

    if (*t)
    {
        t++;
    }

    char *ch = get_channel_from_alias(who, alias);
    if (ch[0] != '\0')
    {
        do_processcom(who, ch, t);
        free_lbuf(alias);
        return FALSE;
    }
    else
    {
        free_lbuf(alias);
    }
    return TRUE;
}

void do_cemit
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int   key,
    int   nargs,
    char *chan,
    char *text
)
{
    if (!mudconf.have_comsys)
    {
        raw_notify(executor, "Comsys disabled.");
        return;
    }
    struct channel *ch = select_channel(chan);
    if (!ch)
    {
        raw_notify(executor, tprintf("Channel %s does not exist.", chan));
        return;
    }
    if ((executor != ch->charge_who) && (!Comm_All(executor)))
    {
        raw_notify(executor, NOPERM_MESSAGE);
        return;
    }
    if (key == CEMIT_NOHEADER)
    {
        SendChannelMessage(executor, ch, text, NULL, TRUE);
    }
    else
    {
        SendChannelMessage(executor, ch, tprintf("%s %s", ch->header, text), NULL, TRUE);
    }
}

void do_chopen
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int   key,
    int   nargs,
    char *chan,
    char *value
)
{
    if (!mudconf.have_comsys)
    {
        raw_notify(executor, "Comsys disabled.");
        return;
    }
    if (key == CSET_LIST)
    {
        do_chanlist(executor, caller, enactor, 1);
        return;
    }

    char *msg = NULL;
    char *buff;
    dbref thing;

    struct channel *ch = select_channel(chan);
    if (!ch)
    {
        msg = tprintf("@cset: Channel %s does not exist.", chan);
        raw_notify(executor, msg);
        return;
    }
    if ((executor != ch->charge_who) && (!Comm_All(executor)))
    {
        raw_notify(executor, NOPERM_MESSAGE);
        return;
    }
    switch (key)
    {
    case CSET_PUBLIC:
        ch->type |= CHANNEL_PUBLIC;
        msg = tprintf("@cset: Channel %s placed on the public listings.", chan);
        break;

    case CSET_PRIVATE:
        ch->type &= ~CHANNEL_PUBLIC;
        msg = tprintf("@cset: Channel %s taken off the public listings." ,chan);
        break;

    case CSET_LOUD:
        ch->type |= CHANNEL_LOUD;
        msg = tprintf("@cset: Channel %s now sends connect/disconnect msgs.", chan);
        break;

    case CSET_QUIET:
        ch->type &= ~CHANNEL_LOUD;
        msg = tprintf("@cset: Channel %s connect/disconnect msgs muted.", chan);
        break;

    case CSET_SPOOF:
        ch->type |= CHANNEL_SPOOF;
        msg = tprintf("@cset: Channel %s set spoofable.", chan);
        break;

    case CSET_NOSPOOF:
        ch->type &= ~CHANNEL_SPOOF;
        msg = tprintf("@cset: Channel %s set unspoofable.", chan);
        break;

    case CSET_OBJECT:
        init_match(executor, value, NOTYPE);
        match_everything(0);
        thing = match_result();

        if (thing == NOTHING)
        {
            ch->chan_obj = thing;
            msg = tprintf("Channel %s is now disassociated from any channel object.", ch->name);
        }
        else if (Good_obj(thing))
        {
            ch->chan_obj = thing;
            buff = unparse_object(executor, thing, FALSE);
            msg = tprintf("Channel %s is now using %s as channel object.", ch->name, buff);
            free_lbuf(buff);
        }
        else
        {
            msg = tprintf("%d is not a valid channel object.", thing);
        }
        break;

    case CSET_HEADER:
        do_cheader(executor, chan, value);
        msg = "Set.";
        break;

    case CSET_LOG:
        if (do_chanlog(executor, chan, value))
        {
            msg = tprintf("@cset: Channel %s maximum history set.", chan);
        }
        else
        {
            msg = tprintf("@cset: Maximum history must be a number less than or equal to %d.", MAX_RECALL_REQUEST);
        }
        break;
    }
    raw_notify(executor, msg);
}

void do_chboot
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int   key,
    int   nargs,
    char *channel,
    char *victim
)
{
    // I sure hope it's not going to be that long.
    //
    if (!mudconf.have_comsys)
    {
        raw_notify(executor, "Comsys disabled.");
        return;
    }
    struct channel *ch = select_channel(channel);
    if (!ch)
    {
        raw_notify(executor, "@cboot: Unknown channel.");
        return;
    }
    struct comuser *user = select_user(ch, executor);
    if (!user)
    {
        raw_notify(executor, "@cboot: You are not on that channel.");
        return;
    }
    if (!(ch->charge_who == executor) && !Comm_All(executor))
    {
        raw_notify(executor, "@cboot: You can't do that!");
        return;
    }
    dbref thing = match_thing(executor, victim);

    if (!Good_obj(thing))
    {
        return;
    }
    struct comuser *vu = select_user(ch, thing);
    if (!vu)
    {
        raw_notify(executor, tprintf("@cboot: %s is not on the channel.", Name(thing)));
        return;
    }

    raw_notify(executor, tprintf("You boot %s off channel %s.",
                                 Name(thing), ch->name));
    raw_notify(thing, tprintf("%s boots you off channel %s.",
                              Name(thing), ch->name));

    if (!(key & CBOOT_QUIET))
    {
        char *mess1, *mess1nct;
        char *mess2, *mess2nct;
        BuildChannelMessage((ch->type & CHANNEL_SPOOF) != 0, ch->header, user,
                            ":boots", &mess1, &mess1nct);
        BuildChannelMessage((ch->type & CHANNEL_SPOOF) != 0, 0, vu,
                            ":off the channel.", &mess2, &mess2nct);
        char *messNormal = alloc_lbuf("do_chboot.messnormal");
        char *messNoComtitle = alloc_lbuf("do_chboot.messnocomtitle");
        char *mnp = messNormal;
        char *mnctp = messNoComtitle;
        if (mess1)
        {
            safe_str(mess1, messNormal, &mnp);
            free_lbuf(mess1);
        }
        if (mess2)
        {
            safe_str(mess2, messNormal, &mnp);
            free_lbuf(mess2);
        }
        *mnp = '\0';
        if (mess1nct)
        {
            safe_str(mess1nct, messNoComtitle, &mnctp);
            free_lbuf(mess1nct);
        }
        if (mess2nct)
        {
            safe_str(mess2nct, messNoComtitle, &mnctp);
            free_lbuf(mess2nct);
        }
        *mnctp = '\0';
        SendChannelMessage(executor, ch, messNormal, messNoComtitle, FALSE);
        do_delcomchannel(thing, channel, FALSE);
    }
    else
    {    
        do_delcomchannel(thing, channel, TRUE);
    }
    
}

void do_cheader(dbref player, char *channel, char *header)
{
    struct channel *ch = select_channel(channel);
    if (!ch)
    {
        raw_notify(player, "That channel does not exist.");
        return;
    }
    if (!(ch->charge_who == player) && !Comm_All(player))
    {
        raw_notify(player, NOPERM_MESSAGE);
        return;
    }
    char *p = RemoveSetOfCharacters(header, "\r\n\t");

    // Optimize/terminate any ANSI in the string.
    //
    char NewHeader_ANSI[MAX_HEADER_LEN+1];
    int nVisualWidth;
    int nLen = ANSI_TruncateToField(p, sizeof(NewHeader_ANSI),
        NewHeader_ANSI, sizeof(NewHeader_ANSI), &nVisualWidth,
        ANSI_ENDGOAL_NORMAL);
    memcpy(ch->header, NewHeader_ANSI, nLen+1);
}

void do_chanlist(dbref executor, dbref caller, dbref enactor, int key)
{
    if (!mudconf.have_comsys)
    {
        raw_notify(executor, "Comsys disabled.");
        return;
    }
    if (key & CLIST_FULL)
    {
        do_listchannels(executor);
        return;
    }

    dbref owner;
    struct channel *ch;
    int flags = 0;
    char *atrstr;
    char *temp = alloc_mbuf("do_chanlist_temp");
    char *buf = alloc_mbuf("do_chanlist_buf");

    if (key & CLIST_HEADERS)
    {
        raw_notify(executor, "*** Channel       Owner           Header");
    }
    else
    {
        raw_notify(executor, "*** Channel       Owner           Description");
    }

    for (ch = (struct channel *)hash_firstentry(&mudstate.channel_htab);
         ch; ch = (struct channel *)hash_nextentry(&mudstate.channel_htab))
    {
        if (  Comm_All(executor)
           || (ch->type & CHANNEL_PUBLIC)
           || ch->charge_who == executor)
        {
            char *pBuffer;
            if (key & CLIST_HEADERS)
            {
                pBuffer = ch->header;
            }
            else
            {
                atrstr = atr_pget(ch->chan_obj, A_DESC, &owner, &flags);
                if ((ch->chan_obj == NOTHING) || !*atrstr)
                    strcpy(buf, "No description.");
                else
                    sprintf(buf, "%-54.54s", atrstr);
                free_lbuf(atrstr);

                pBuffer = buf;
            }
            sprintf(temp, "%c%c%c %-13.13s %-15.15s %-45.45s",
                (ch->type & (CHANNEL_PUBLIC)) ? 'P' : '-',
                (ch->type & (CHANNEL_LOUD)) ? 'L' : '-',
                (ch->type & (CHANNEL_SPOOF)) ? 'S' : '-',
                ch->name, Name(ch->charge_who), pBuffer);

            raw_notify(executor, temp);
        }
    }
    free_mbuf(temp);
    free_mbuf(buf);
    raw_notify(executor, "-- End of list of Channels --");
}

// Returns a player's comtitle for a named channel.
//
FUNCTION(fun_comtitle)
{
    if (!mudconf.have_comsys)
    {
        safe_str("#-1 COMSYS DISABLED", buff, bufc);
        return;
    }

    dbref victim = lookup_player(executor, fargs[0], TRUE);

    if (victim == NOTHING)
    {
        safe_str("#-1 PLAYER DOES NOT EXIST", buff, bufc);
        return;
    }

    struct channel *chn = select_channel(fargs[1]);
    if (!chn)
    {
        safe_str("#-1 CHANNEL DOES NOT EXIST", buff, bufc);
        return;
    }

    comsys_t *c = get_comsys(executor);
    struct comuser *user;

    int i;
    BOOL onchannel = FALSE;
    if (Wizard(executor))
    {
        onchannel = TRUE;
    }
    else
    {
        for (i = 0; i < c->numchannels; i++)
        {
            user = select_user(chn, executor);
            if (user)
            {
                onchannel = TRUE;
                break;
            }
        }
    }

    if (!onchannel)
    {
        safe_noperm(buff, bufc);
        return;
    }

    for (i = 0; i < c->numchannels; i++)
    {
        user = select_user(chn, victim);
        if (user)
        {
          // Do we want this function to evaluate the comtitle or not?
#if 0
          char *nComTitle = GetComtitle(user);
          safe_str(nComTitle, buff, bufc);
          FreeComtitle(nComTitle);
          return;
#else
          safe_str(user->title, buff, bufc);
          return;
#endif
        }
    }
    safe_str("#-1 PLAYER NOT ON THAT CHANNEL", buff, bufc);
}

// Returns a player's comsys alias for a named channel.
//
FUNCTION(fun_comalias)
{
    if (!mudconf.have_comsys)
    {
        safe_str("#-1 COMSYS DISABLED", buff, bufc);
        return;
    }

    dbref victim = lookup_player(executor, fargs[0], TRUE);

    if (victim == NOTHING)
    {
        safe_str("#-1 PLAYER DOES NOT EXIST", buff, bufc);
        return;
    }

    struct channel *chn = select_channel(fargs[1]);
    if (!chn)
    {
        safe_str("#-1 CHANNEL DOES NOT EXIST", buff, bufc);
        return;
    }

    // Wizards can get the comalias for anyone. Players and objects can check
    // for themselves. Objects that Inherit can check for their owners.
    //
    if (  !Wizard(executor)
       && executor != victim
       && (  Owner(executor) != victim
          || !Inherits(executor)))
    {
        safe_noperm(buff, bufc);
        return;
    }

    comsys_t *cc = get_comsys(victim);
    for (int i = 0; i < cc->numchannels; i++)
    {
        if (!strcmp(fargs[1], cc->channels[i]))
        {
            safe_str(cc->alias + i * ALIAS_SIZE, buff, bufc);
            return;
        }
    }
    safe_str("#-1 PLAYER NOT ON THAT CHANNEL", buff, bufc);
}

// Returns a list of channels.
//
FUNCTION(fun_channels)
{
    if (!mudconf.have_comsys)
    {
        safe_str("#-1 COMSYS DISABLED", buff, bufc);
        return;
    }

    dbref who = NOTHING;
    if (nfargs >= 1)
    {
        who = lookup_player(executor, fargs[0], TRUE);
        if (  who == NOTHING
           && _stricmp(fargs[0], "all") != 0)
        {
            safe_str("#-1 PLAYER NOT FOUND", buff, bufc);
            return;
        }
    }

    ITL itl;
    ItemToList_Init(&itl, buff, bufc);
    struct channel *chn;
    for (chn = (struct channel *)hash_firstentry(&mudstate.channel_htab);
         chn;
         chn = (struct channel *)hash_nextentry(&mudstate.channel_htab))
    {
        if (  (  Comm_All(executor)
              || (chn->type & CHANNEL_PUBLIC)
              || chn->charge_who == executor)
           && (  chn->charge_who == who
              || who == NOTHING)
           && !ItemToList_AddString(&itl, chn->name))
        {
            break;
        }
    }
}
