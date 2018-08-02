/*
  Copyright (c) 2008-2012 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "statedump.h"
#include "stack.h"
#include "libglusterfs-messages.h"

call_frame_t *
create_frame (xlator_t *xl, call_pool_t *pool)
{
        call_stack_t    *stack = NULL;
        call_frame_t    *frame = NULL;

        if (!xl || !pool) {
                return NULL;
        }

        stack = mem_get0 (pool->stack_mem_pool);
        if (!stack)
                return NULL;

        INIT_LIST_HEAD (&stack->myframes);

        frame = mem_get0 (pool->frame_mem_pool);
        if (!frame) {
                mem_put (stack);
                return NULL;
        }

        frame->root = stack;
        frame->this = xl;
        LOCK_INIT (&frame->lock);
        INIT_LIST_HEAD (&frame->frames);
        list_add (&frame->frames, &stack->myframes);

        stack->pool = pool;
        stack->ctx = xl->ctx;

        if (frame->root->ctx->measure_latency) {
                timespec_now (&stack->tv);
                memcpy (&frame->begin, &stack->tv,
                        sizeof (stack->tv));
        }

        LOCK (&pool->lock);
        {
                list_add (&stack->all_frames, &pool->all_frames);
                pool->cnt++;
        }
        UNLOCK (&pool->lock);
        GF_ATOMIC_INC (pool->total_count);

        LOCK_INIT (&stack->stack_lock);

        return frame;
}

void
call_stack_set_groups (call_stack_t *stack, int ngrps, gid_t **groupbuf_p)
{
        /* We take the ownership of the passed group buffer. */

        if (ngrps <= SMALL_GROUP_COUNT) {
                memcpy (stack->groups_small, *groupbuf_p,
                        sizeof (gid_t) * ngrps);
                stack->groups = stack->groups_small;
                GF_FREE (*groupbuf_p);
        } else {
                stack->groups_large = *groupbuf_p;
                stack->groups = stack->groups_large;
        }

        stack->ngrps = ngrps;
        /* Set a canary. */
        *groupbuf_p = (void *)0xdeadf00d;
}

void
gf_proc_dump_call_frame (call_frame_t *call_frame, const char *key_buf,...)
{

        char prefix[GF_DUMP_MAX_BUF_LEN];
        va_list ap;
        call_frame_t my_frame;
        int  ret = -1;
        char timestr[256] = {0,};

        if (!call_frame)
                return;

        GF_ASSERT (key_buf);

        memset(&my_frame, 0, sizeof(my_frame));
        va_start(ap, key_buf);
        vsnprintf(prefix, GF_DUMP_MAX_BUF_LEN, key_buf, ap);
        va_end(ap);

        ret = TRY_LOCK(&call_frame->lock);
        if (ret)
                goto out;

        memcpy(&my_frame, call_frame, sizeof(my_frame));
        UNLOCK(&call_frame->lock);

        if (my_frame.root->ctx->measure_latency) {
                gf_time_fmt (timestr, sizeof (timestr), my_frame.begin.tv_sec,
                             gf_timefmt_FT);
                snprintf (timestr + strlen (timestr),
                          sizeof (timestr) - strlen (timestr),
                          ".%"GF_PRI_SNSECONDS, my_frame.begin.tv_nsec);
                gf_proc_dump_write("frame-creation-time", "%s", timestr);
                gf_proc_dump_write("timings", "%ld.%"GF_PRI_SNSECONDS
                                   " -> %ld.%"GF_PRI_SNSECONDS,
                                   my_frame.begin.tv_sec,
                                   my_frame.begin.tv_nsec,
                                   my_frame.end.tv_sec,
                                   my_frame.end.tv_nsec);
        }

        gf_proc_dump_write("frame", "%p", call_frame);
        gf_proc_dump_write("ref_count", "%d", my_frame.ref_count);
        gf_proc_dump_write("translator", "%s", my_frame.this->name);
        gf_proc_dump_write("complete", "%d", my_frame.complete);

        if (my_frame.parent)
                gf_proc_dump_write("parent", "%s", my_frame.parent->this->name);

        if (my_frame.wind_from)
                gf_proc_dump_write("wind_from", "%s", my_frame.wind_from);

        if (my_frame.wind_to)
                gf_proc_dump_write("wind_to", "%s", my_frame.wind_to);

        if (my_frame.unwind_from)
                gf_proc_dump_write("unwind_from", "%s", my_frame.unwind_from);

        if (my_frame.unwind_to)
                gf_proc_dump_write("unwind_to", "%s", my_frame.unwind_to);

        ret = 0;
out:
        if (ret) {
                gf_proc_dump_write("Unable to dump the frame information",
                                   "(Lock acquisition failed) %p", my_frame);
                return;
        }
}


void
gf_proc_dump_call_stack (call_stack_t *call_stack, const char *key_buf,...)
{
        char prefix[GF_DUMP_MAX_BUF_LEN];
        va_list ap;
        call_frame_t *trav;
        int32_t i = 1, cnt = 0;
        char timestr[256] = {0,};

        if (!call_stack)
                return;

        GF_ASSERT (key_buf);

        va_start(ap, key_buf);
        vsnprintf(prefix, GF_DUMP_MAX_BUF_LEN, key_buf, ap);
        va_end(ap);

        cnt = call_frames_count (call_stack);
        gf_time_fmt (timestr, sizeof (timestr), call_stack->tv.tv_sec,
                     gf_timefmt_FT);
        snprintf (timestr + strlen (timestr),
                  sizeof (timestr) - strlen (timestr),
                  ".%"GF_PRI_SNSECONDS, call_stack->tv.tv_nsec);
        gf_proc_dump_write("callstack-creation-time", "%s", timestr);

        gf_proc_dump_write("stack", "%p", call_stack);
        gf_proc_dump_write("uid", "%d", call_stack->uid);
        gf_proc_dump_write("gid", "%d", call_stack->gid);
        gf_proc_dump_write("pid", "%d", call_stack->pid);
        gf_proc_dump_write("unique", "%Ld", call_stack->unique);
        gf_proc_dump_write("lk-owner", "%s", lkowner_utoa (&call_stack->lk_owner));
        gf_proc_dump_write("ctime", "%lld.%"GF_PRI_SNSECONDS,
                           call_stack->tv.tv_sec, call_stack->tv.tv_nsec);

        if (call_stack->type == GF_OP_TYPE_FOP)
                gf_proc_dump_write("op", "%s",
                                   (char *)gf_fop_list[call_stack->op]);
        else
                gf_proc_dump_write("op", "stack");

        gf_proc_dump_write("type", "%d", call_stack->type);
        gf_proc_dump_write("cnt", "%d", cnt);

        list_for_each_entry (trav, &call_stack->myframes, frames) {
                gf_proc_dump_add_section("%s.frame.%d", prefix, i);
                gf_proc_dump_call_frame(trav, "%s.frame.%d", prefix, i);
                i++;
        }
}

void
gf_proc_dump_pending_frames (call_pool_t *call_pool)
{

        call_stack_t     *trav = NULL;
        int              i = 1;
        int              ret = -1;
        gf_boolean_t     section_added = _gf_true;

        if (!call_pool)
                return;

        ret = TRY_LOCK (&(call_pool->lock));
        if (ret)
                goto out;


        gf_proc_dump_add_section("global.callpool");
        section_added = _gf_true;
        gf_proc_dump_write("callpool_address","%p", call_pool);
        gf_proc_dump_write("callpool.cnt","%d", call_pool->cnt);


        list_for_each_entry (trav, &call_pool->all_frames, all_frames) {
                gf_proc_dump_add_section("global.callpool.stack.%d",i);
                gf_proc_dump_call_stack(trav, "global.callpool.stack.%d", i);
                i++;
        }
        UNLOCK (&(call_pool->lock));

        ret = 0;
out:
        if (ret) {
                if (_gf_false == section_added)
                        gf_proc_dump_add_section("global.callpool");
                gf_proc_dump_write("Unable to dump the callpool",
                                   "(Lock acquisition failed) %p",
                                   call_pool);
        }
        return;
}

void
gf_proc_dump_call_frame_to_dict (call_frame_t *call_frame,
                                 char *prefix, dict_t *dict)
{
        int             ret = -1;
        char            key[GF_DUMP_MAX_BUF_LEN] = {0,};
        char            msg[GF_DUMP_MAX_BUF_LEN] = {0,};
        call_frame_t    tmp_frame = {0,};

        if (!call_frame || !dict)
                return;

        ret = TRY_LOCK (&call_frame->lock);
        if (ret)
                return;
        memcpy (&tmp_frame, call_frame, sizeof (tmp_frame));
        UNLOCK (&call_frame->lock);

        snprintf (key, sizeof (key), "%s.refcount", prefix);
        ret = dict_set_int32 (dict, key, tmp_frame.ref_count);
        if (ret)
                return;

        snprintf (key, sizeof (key), "%s.translator", prefix);
        ret = dict_set_dynstr (dict, key, gf_strdup (tmp_frame.this->name));
        if (ret)
                return;

        snprintf (key, sizeof (key), "%s.complete", prefix);
        ret = dict_set_int32 (dict, key, tmp_frame.complete);
        if (ret)
                return;

        if (tmp_frame.root->ctx->measure_latency) {
                snprintf (key, sizeof (key), "%s.timings", prefix);
                snprintf (msg, sizeof (msg), "%ld.%"GF_PRI_SNSECONDS
                          " -> %ld.%"GF_PRI_SNSECONDS,
                          tmp_frame.begin.tv_sec, tmp_frame.begin.tv_nsec,
                          tmp_frame.end.tv_sec, tmp_frame.end.tv_nsec);
                ret = dict_set_str (dict, key, msg);
                if (ret)
                        return;
        }

        if (tmp_frame.parent) {
                snprintf (key, sizeof (key), "%s.parent", prefix);
                ret = dict_set_dynstr (dict, key,
                                    gf_strdup (tmp_frame.parent->this->name));
                if (ret)
                        return;
        }

        if (tmp_frame.wind_from) {
                snprintf (key, sizeof (key), "%s.windfrom", prefix);
                ret = dict_set_dynstr (dict, key,
                                       gf_strdup (tmp_frame.wind_from));
                if (ret)
                        return;
        }

        if (tmp_frame.wind_to) {
                snprintf (key, sizeof (key), "%s.windto", prefix);
                ret = dict_set_dynstr (dict, key,
                                       gf_strdup (tmp_frame.wind_to));
                if (ret)
                        return;
        }

        if (tmp_frame.unwind_from) {
                snprintf (key, sizeof (key), "%s.unwindfrom", prefix);
                ret = dict_set_dynstr (dict, key,
                                       gf_strdup (tmp_frame.unwind_from));
                if (ret)
                        return;
        }

        if (tmp_frame.unwind_to) {
                snprintf (key, sizeof (key), "%s.unwind_to", prefix);
                ret = dict_set_dynstr (dict, key,
                                       gf_strdup (tmp_frame.unwind_to));
        }

        return;
}

void
gf_proc_dump_call_stack_to_dict (call_stack_t *call_stack,
                                 char *prefix, dict_t *dict)
{
        int             ret = -1;
        char            key[GF_DUMP_MAX_BUF_LEN] = {0,};
        call_frame_t    *trav = NULL;
        int             i = 0;
        int             count = 0;

        if (!call_stack || !dict)
                return;

        count = call_frames_count (call_stack);
        snprintf (key, sizeof (key), "%s.uid", prefix);
        ret = dict_set_int32 (dict, key, call_stack->uid);
        if (ret)
                return;

        snprintf (key, sizeof (key), "%s.gid", prefix);
        ret = dict_set_int32 (dict, key, call_stack->gid);
        if (ret)
                return;

        snprintf (key, sizeof (key), "%s.pid", prefix);
        ret = dict_set_int32 (dict, key, call_stack->pid);
        if (ret)
                return;

        snprintf (key, sizeof (key), "%s.unique", prefix);
        ret = dict_set_uint64 (dict, key, call_stack->unique);
        if (ret)
                return;

        snprintf (key, sizeof (key), "%s.op", prefix);
        if (call_stack->type == GF_OP_TYPE_FOP)
                ret = dict_set_str (dict, key,
                                    (char *)gf_fop_list[call_stack->op]);
        else
                ret = dict_set_str (dict, key, "other");

        if (ret)
                return;

        snprintf (key, sizeof (key), "%s.type", prefix);
        ret = dict_set_int32 (dict, key, call_stack->type);
        if (ret)
                return;

        snprintf (key, sizeof (key), "%s.count", prefix);
        ret = dict_set_int32 (dict, key, count);
        if (ret)
                return;

        list_for_each_entry (trav, &call_stack->myframes, frames) {
                snprintf (key, sizeof (key), "%s.frame%d",
                          prefix, i);
                gf_proc_dump_call_frame_to_dict (trav, key, dict);
                i++;
        }

        return;
}

void
gf_proc_dump_pending_frames_to_dict (call_pool_t *call_pool, dict_t *dict)
{
        int             ret = -1;
        call_stack_t    *trav = NULL;
        char            key[GF_DUMP_MAX_BUF_LEN] = {0,};
        int             i = 0;

        if (!call_pool || !dict)
                return;

        ret = TRY_LOCK (&call_pool->lock);
        if (ret) {
                gf_msg (THIS->name, GF_LOG_WARNING, errno,
                        LG_MSG_LOCK_FAILURE, "Unable to dump call "
                        "pool to dict.");
                return;
        }

        ret = dict_set_int32 (dict, "callpool.count", call_pool->cnt);
        if (ret)
                goto out;

        list_for_each_entry (trav, &call_pool->all_frames, all_frames) {
                snprintf (key, sizeof (key), "callpool.stack%d", i);
                gf_proc_dump_call_stack_to_dict (trav, key, dict);
                i++;
        }

out:
        UNLOCK (&call_pool->lock);

        return;
}

gf_boolean_t
__is_fuse_call (call_frame_t *frame)
{
        gf_boolean_t    is_fuse_call = _gf_false;
        GF_ASSERT (frame);
        GF_ASSERT (frame->root);

        if (NFS_PID != frame->root->pid)
                is_fuse_call = _gf_true;
        return is_fuse_call;
}
