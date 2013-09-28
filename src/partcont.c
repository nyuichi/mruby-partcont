#include <stdlib.h>
#include "mruby.h"
#include "mruby/array.h"
#include "mruby/class.h"
#include "mruby/proc.h"
#include "mruby/value.h"
#include "mruby/variable.h"
#include "mruby/data.h"

/* mark return from context modifying method */
#define MARK_CONTEXT_MODIFY(c) (c)->ci->target_class = NULL

struct mrb_continuation {
  void *dummy;
};

static struct mrb_data_type mrb_continuation_type = { "Continuation", mrb_free };

#define mrb_obj_alloc_fiber(mrb) ((struct RFiber *)mrb_obj_alloc((mrb), MRB_TT_FIBER, mrb_class_get((mrb), "Fiber")))

static mrb_value
mrb_continuation_initialize(mrb_state *mrb, mrb_value self)
{
  struct RFiber *fib;

  fib = mrb_obj_alloc_fiber(mrb);
  fib->cxt = mrb->c;

  /* save current context as a fiber */
  mrb_iv_set(mrb, self, mrb_intern(mrb, "__fiber__"), mrb_obj_value(fib));

  return self;
}

static mrb_value
mrb_continuation_initialize_copy(mrb_state *mrb, mrb_value copy)
{
  mrb_value src;

  mrb_get_args(mrb, "o", &src);
  if (mrb_obj_equal(mrb, copy, src))
    return copy;
  if (! mrb_obj_is_instance_of(mrb, src, mrb_obj_class(mrb, copy))) {
    mrb_raise(mrb, E_TYPE_ERROR, "wrong argument class");
  }
  if (!DATA_PTR(copy)) {
    DATA_PTR(copy) = mrb_malloc(mrb, sizeof(struct mrb_continuation));
    DATA_TYPE(copy) = &mrb_continuation_type;
  }
  *(struct mrb_continuation *)DATA_PTR(copy) = *(struct mrb_continuation *)DATA_PTR(src);
  return copy;
}

static mrb_value
continuation_result(mrb_state *mrb, mrb_value *a, int len)
{
  if (len == 0) return mrb_nil_value();
  if (len == 1) return a[0];
  return mrb_ary_new_from_values(mrb, len, a);
}

static struct mrb_context *
clone_context(mrb_state *mrb, struct mrb_context *ctx)
{
  static const struct mrb_context mrb_context_zero = { 0 };
  struct mrb_context *c;
  size_t stack_size, stack_offset, ci_size, ci_offset;

  c = (struct mrb_context *)mrb_malloc(mrb, sizeof(struct mrb_context));
  *c = mrb_context_zero;

  stack_size = ctx->stend - ctx->stbase;
  stack_offset = ctx->stack - ctx->stbase;
  c->stbase = (mrb_value *)mrb_calloc(mrb, stack_size, sizeof(mrb_value));
  c->stend = c->stbase + stack_size;
  c->stack = c->stbase + stack_offset;
  memcpy(c->stbase, ctx->stbase, stack_size * sizeof(mrb_value));

  ci_size = ctx->ciend - ctx->cibase;
  ci_offset = ctx->ci - ctx->cibase;
  c->cibase = (mrb_callinfo *)mrb_calloc(mrb, ci_size, sizeof(mrb_callinfo));
  c->ciend = c->cibase + ci_size;
  c->ci = c->cibase + ci_offset;
  memcpy(c->cibase, ctx->cibase, ci_size * sizeof(mrb_callinfo));

  c->rescue = (mrb_code **)mrb_calloc(mrb, ctx->rsize, sizeof(mrb_code *));
  c->rsize = ctx->rsize;
  c->ensure = (struct RProc **)mrb_calloc(mrb, ctx->esize, sizeof(struct RProc *));
  c->esize = ctx->esize;

  c->prev = ctx->prev;
  c->status = ctx->status;
  c->fib = ctx->fib;

  return c;
}

#define mrb_fiber_ptr(v) ((struct RFiber *)(mrb_ptr(v)))

static mrb_value
mrb_continuation_call(mrb_state *mrb, mrb_value self)
{
  mrb_value fib, *a;
  struct mrb_context *c;
  int len;

  fib = mrb_iv_get(mrb, self, mrb_intern(mrb, "__fiber__"));

  /* clone context */
  c = clone_context(mrb, mrb_fiber_ptr(fib)->cxt);

  /* `reset` */
  mrb->c->status = MRB_FIBER_RESUMED;
  c->prev = mrb->c;
  c->status = MRB_FIBER_RUNNING;
  mrb->c = c;

  MARK_CONTEXT_MODIFY(c);

  mrb_get_args(mrb, "*", &a, &len);
  return continuation_result(mrb, a, len);
}

#ifndef FIBER_STACK_INIT_SIZE
#  define FIBER_STACK_INIT_SIZE 64
#endif
#ifndef FIBER_CI_INIT_SIZE
#  define FIBER_CI_INIT_SIZE 8
#endif

static mrb_value
mrb_kernel_reset(mrb_state *mrb, mrb_value self)
{
  static const struct mrb_context mrb_context_zero = { 0 };
  struct mrb_context *c;
  struct RProc *p;
  mrb_callinfo *ci;
  mrb_value blk;

  mrb_get_args(mrb, "&", &blk);

  if (mrb_nil_p(blk)) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "tried to reset without a block");
  }
  p = mrb_proc_ptr(blk);
  if (MRB_PROC_CFUNC_P(p)) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "tried to reset from C defined method");
  }

  c = (struct mrb_context*)mrb_malloc(mrb, sizeof(struct mrb_context));
  *c = mrb_context_zero;

  /* initialize VM stack */
  c->stbase = (mrb_value *)mrb_calloc(mrb, FIBER_STACK_INIT_SIZE, sizeof(mrb_value));
  c->stend = c->stbase + FIBER_STACK_INIT_SIZE;
  c->stack = c->stbase;

  /* copy receiver from a block */
  c->stack[0] = mrb->c->stack[0];

  /* initialize callinfo stack */
  c->cibase = (mrb_callinfo *)mrb_calloc(mrb, FIBER_CI_INIT_SIZE, sizeof(mrb_callinfo));
  c->ciend = c->cibase + FIBER_CI_INIT_SIZE;
  c->ci = c->cibase;

  /* adjust return callinfo */
  ci = c->ci;
  ci->target_class = p->target_class;
  ci->proc = p;
  ci->argc = 0;
  ci->pc = p->body.irep->iseq;
  ci->nregs = p->body.irep->nregs;
  ci[1] = ci[0];
  c->ci++;                      /* push dummy callinfo */

  c->fib = NULL;

  /* delegate process */
  mrb->c->status = MRB_FIBER_RESUMED;
  c->prev = mrb->c;
  c->status = MRB_FIBER_RUNNING;
  mrb->c = c;

  MARK_CONTEXT_MODIFY(c);
  return c->ci->proc->env->stack[0];
}

static mrb_value
mrb_kernel_shift(mrb_state *mrb, mrb_value self)
{
  static const struct mrb_context mrb_context_zero = { 0 };
  struct mrb_context *c;
  struct RProc *p;
  mrb_callinfo *ci;
  mrb_value blk, k;

  mrb_get_args(mrb, "&", &blk);

  if (mrb_nil_p(blk)) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "tried to shift without a block");
  }
  p = mrb_proc_ptr(blk);
  if (MRB_PROC_CFUNC_P(p)) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "tried to shift from C defined method");
  }

  c = (struct mrb_context*)mrb_malloc(mrb, sizeof(struct mrb_context));
  *c = mrb_context_zero;

  /* initialize VM stack */
  c->stbase = (mrb_value *)mrb_calloc(mrb, FIBER_STACK_INIT_SIZE, sizeof(mrb_value));
  c->stend = c->stbase + FIBER_STACK_INIT_SIZE;
  c->stack = c->stbase;

  /* copy receiver from a block */
  c->stack[0] = mrb->c->stack[0];

  /* initialize callinfo stack */
  c->cibase = (mrb_callinfo *)mrb_calloc(mrb, FIBER_CI_INIT_SIZE, sizeof(mrb_callinfo));
  c->ciend = c->cibase + FIBER_CI_INIT_SIZE;
  c->ci = c->cibase;

  /* adjust return callinfo */
  ci = c->ci;
  ci->target_class = p->target_class;
  ci->proc = p;
  ci->pc = p->body.irep->iseq;
  ci->nregs = p->body.irep->nregs;
  ci[1] = ci[0];
  c->ci++;                      /* push dummy callinfo */

  c->fib = NULL;

  /* create and push continuation object */
  k = mrb_obj_new(mrb, mrb_class_get(mrb, "Continuation"), 0, NULL);
  c->stack[1] = k;
  c->cibase->argc = 1;

  /* delegate process */
  mrb->c->status = MRB_FIBER_RESUMED;
  c->prev = mrb->c->prev;
  c->status = MRB_FIBER_RUNNING;
  mrb->c = c;

  MARK_CONTEXT_MODIFY(c);
  return c->ci->proc->env->stack[0];
}

void
mrb_mruby_partcont_gem_init(mrb_state* mrb)
{
  struct RClass *cc;

  cc = mrb_define_class(mrb, "Continuation", mrb->object_class);
  MRB_SET_INSTANCE_TT(cc, MRB_TT_DATA);
  mrb_define_method(mrb, cc, "initialize", mrb_continuation_initialize, MRB_ARGS_NONE());
  mrb_define_method(mrb, cc, "initialize_copy", mrb_continuation_initialize_copy, MRB_ARGS_REQ(1));
  mrb_define_method(mrb, cc, "call", mrb_continuation_call, MRB_ARGS_ANY());
  mrb_define_method(mrb, cc, "[]", mrb_continuation_call, MRB_ARGS_ANY());

  mrb_define_class_method(mrb, mrb->kernel_module, "reset", mrb_kernel_reset, MRB_ARGS_BLOCK());
  mrb_define_method(mrb, mrb->kernel_module, "reset", mrb_kernel_reset, MRB_ARGS_BLOCK());
  mrb_define_class_method(mrb, mrb->kernel_module, "shift", mrb_kernel_shift, MRB_ARGS_BLOCK());
  mrb_define_method(mrb, mrb->kernel_module, "shift", mrb_kernel_shift, MRB_ARGS_BLOCK());
}

void
mrb_mruby_partcont_gem_final(mrb_state* mrb)
{
}
