/*****************************************************************************\
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2001-2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jim Garlick <garlick@llnl.gov>.
 *  UCRL-CODE-2003-005.
 *  
 *  This file is part of Pdsh, a parallel remote shell program.
 *  For details, see <http://www.llnl.gov/linux/pdsh/>.
 *  
 *  Pdsh is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  Pdsh is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with Pdsh; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/
#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <genders.h>

#include "hostlist.h"
#include "err.h"
#include "xmalloc.h"
#include "mod.h"
#include "xpopen.h"

#define ALL_NODES NULL

#ifndef GENDERS_ALTNAME_ATTRIBUTE
#  define GENDERS_ALTNAME_ATTRIBUTE   "altname"
#endif

MODULE_TYPE        ( "misc"                                                 );

#if GENDERS_G_ONLY
MODULE_NAME        ( "genders-g"                                            );
#else
MODULE_NAME        ( "genders"                                              );
#endif /* GENDERS_G_ONLY */

MODULE_AUTHOR      ( "Jim Garlick <garlick@llnl.gov>"                       );
MODULE_DESCRIPTION ( "target nodes using libgenders and genders attributes" );

hostlist_t genders_wcoll(opt_t *pdsh_opts);

/* 
 * Export pdsh module operations structure
 */
struct pdsh_module_operations pdsh_module_ops = {
    (ModInitF) NULL, 
    (ModExitF) NULL, 
    (ModReadWcollF) genders_wcoll, 
    (ModPostOpF) NULL
};


/*
 *  Genders module options
 *   -a      select all nodes
 *   -i      select alternate hostnames from genders
 *   -g attr select all nodes with genders attribute "attr"
 *
 */
static bool allnodes   = false;
static bool altnames   = false;
static char *gend_attr = NULL;

static int genders_process_opt(opt_t *, int, char *);

struct pdsh_module_option pdsh_module_options[] = 
 { 
   { 'g', "attribute", "target nodes with specified genders attribute",
     (optFunc) genders_process_opt 
   },
#if !GENDERS_G_ONLY
   { 'i', NULL,        "request canonical hostnames if applicable",
     (optFunc) genders_process_opt
   },
   { 'a', NULL,        "target all nodes", 
     (optFunc) genders_process_opt 
   },
#endif /* !GENDERS_G_ONLY */
   PDSH_OPT_TABLE_END
 };

static hostlist_t _read_genders(char *attr, int iopt);

#if 0
int mod_genders_init(void)
{
    err("genders module initializing\n");
    return 0;
}
#endif

static int
genders_process_opt(opt_t *pdsh_opts, int opt, char *arg)
{
    switch (opt) {
    case 'a': 
        allnodes = true;
        break;
    case 'i':
        altnames = true;
        break;
    case 'g':
        gend_attr = Strdup(arg);
        break;
    default:
        err("%p: genders_process_opt: invalid option `%c'\n", opt);
        return -1;
        break;
    }
    return 0;
}


/* 
 * Verify options passed to this module
 */
static void
_genders_opt_verify(opt_t *opt)
{
    if (allnodes && opt->wcoll && (hostlist_count(opt->wcoll) > 0)) {
        err("%p: Do not specify both -a and -w\n");
        exit(1);
    }

    if (altnames && !allnodes && (gend_attr == NULL)) {
        err("%p: Warning: Ignoring -i without -a");
        altnames = false;
    }

    if (allnodes && (gend_attr != NULL)) {
        err("%s: Warning: Ignoring -a with -g");
        allnodes = false;
    }

}

hostlist_t 
genders_wcoll(opt_t *opt)
{
    _genders_opt_verify(opt);

    if (!allnodes && !altnames && !gend_attr)
        return NULL;

    if (allnodes)  
        gend_attr = ALL_NODES;

    return _read_genders(gend_attr, altnames);
}


static hostlist_t
_genders_to_altnames(genders_t g, char **nodes, int nnodes)
{
    hostlist_t hl = NULL;
    int maxvallen = 0;
    char *altname = NULL;
    char *altattr = GENDERS_ALTNAME_ATTRIBUTE;
    char *host    = NULL;
    int i, rc;

    if ((hl = hostlist_create(NULL)) == NULL)
        errx("%p: genders: hostlist_create: %m\n");

    if ((maxvallen = genders_getmaxvallen(g)) < 0)
        errx("%p: genders: getmaxvallen: %s\n", genders_errormsg(g));

    altname = Malloc(maxvallen + 1);

    for (i = 0; i < nnodes; i++) {
        memset(altname, '\0', maxvallen + 1);

        rc = genders_testattr(g, nodes[i], altattr, altname, maxvallen + 1);
        if (rc < 0) 
            errx("%p: genders: testattr failed: %s\n", genders_errormsg(g));

        host = rc == 0 ? nodes[i] : altname;
        if (hostlist_push_host(hl, host) <= 0)
            err("%p: genders: warning: target `%s' not parsed: %m", host);

    }

    Free((void **) &altname);

    return hl;
}


static hostlist_t 
_genders_to_hostlist(genders_t gh, char **nodes, int nnodes)
{
    hostlist_t hl = NULL;
    int i;

    if ((hl = hostlist_create(NULL)) == NULL)
        errx("%p: genders: hostlist_create failed: %m");

    for (i = 0; i < nnodes; i++) {
        if (hostlist_push_host(hl, nodes[i]) <= 0)
            err("%p: warning: target `%s' not parsed: %m\n", nodes[i]);
    }

    return hl;

}

static genders_t 
_handle_create()
{
    genders_t gh = NULL;

    if ((gh = genders_handle_create()) == NULL)
        errx("%p: Unable to create genders handle: %m\n");

    /* assumes genders file in default location */
    if (genders_load_data(gh, NULL) < 0)
        errx("%p: Unable to open genders file: %s\n", genders_errormsg(gh));

    return gh;
}


static hostlist_t 
_read_genders(char *attr, int iopt)
{
    hostlist_t hl = NULL;
    genders_t  gh  = NULL;
    char **nodes;
    int len, nnodes;

    gh = _handle_create();

    if ((len = genders_nodelist_create(gh, &nodes)) < 0)
        errx("%p: genders: nodelist_create: %s", genders_errormsg(gh));

    if ((nnodes = genders_getnodes(gh, nodes, len, attr, NULL)) < 0) {
        errx("%p: Error querying genders for attr \"%s\": %s\n", 
                attr ?: "(all)", genders_errormsg(gh));
    }

    if (!iopt)
        hl = _genders_to_altnames(gh, nodes, nnodes);
    else
        hl = _genders_to_hostlist(gh, nodes, nnodes);

    if (genders_nodelist_destroy(gh, nodes) < 0) {
        errx("%p: Error destroying genders node list: %s\n",
                genders_errormsg(gh));
    }

    if (genders_handle_destroy(gh) < 0) {
        errx("%p: Error destroying genders handle: %s\n",
                genders_errormsg(gh));
    }

    return hl;
}

/*
 * vi: tabstop=4 shiftwidth=4 expandtab
 */