/*
Copyright (c) 2015 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Leonardo de Moura
*/
#include <vector>
#include <string>
#include "util/io.h"
#include "util/option_declarations.h"
#include "kernel/environment.h"
#include "kernel/local_ctx.h"
#include "library/abstract_type_context.h"
#include "library/trace.h"

namespace lean {
static name_set *            g_trace_classes = nullptr;
static name_map<name_set>  * g_trace_aliases = nullptr;
MK_THREAD_LOCAL_GET_DEF(std::vector<name>, get_enabled_trace_classes);
MK_THREAD_LOCAL_GET_DEF(std::vector<name>, get_disabled_trace_classes);
MK_THREAD_LOCAL_GET_DEF(environment, get_dummy_env);
MK_THREAD_LOCAL_GET_DEF(options,     get_dummy_options);
LEAN_THREAD_VALUE(bool,                g_silent, false);
LEAN_THREAD_PTR(environment,           g_env);
LEAN_THREAD_PTR(options,               g_opts);
LEAN_THREAD_PTR(abstract_type_context, g_ctx);

void register_trace_class(name const & n) {
    register_option(name("trace") + n, data_value_kind::Bool, "false",
                    "(trace) enable/disable tracing for the given module and submodules");
    g_trace_classes->insert(n);
}

void register_trace_class_alias(name const & n, name const & alias) {
    name_set new_s;
    if (auto s = g_trace_aliases->find(n))
        new_s = *s;
    new_s.insert(alias);
    g_trace_aliases->insert(n, new_s);
}

bool is_trace_enabled() {
    return !get_enabled_trace_classes().empty();
}

static void update_class(std::vector<name> & cs, name const & c) {
    if (std::find(cs.begin(), cs.end(), c) == cs.end()) {
        cs.push_back(c);
    }
}

static void enable_trace_class(name const & c) {
    update_class(get_enabled_trace_classes(), c);
}

static void disable_trace_class(name const & c) {
    update_class(get_disabled_trace_classes(), c);
}

static bool is_trace_class_set_core(std::vector<name> const & cs, name const & n) {
    for (name const & p : cs) {
        if (is_prefix_of(p, n)) {
            return true;
        }
    }
    return false;
}

static bool is_trace_class_set(std::vector<name> const & cs, name const & n) {
    if (is_trace_class_set_core(cs, n))
        return true;
    auto it = n;
    while (true) {
        if (auto s = g_trace_aliases->find(it)) {
            bool found = false;
            s->for_each([&](name const & alias) {
                    if (!found && is_trace_class_set_core(cs, alias))
                        found = true;
                });
            if (found)
                return true;
        }
        if (it.is_atomic())
            return false;
        it = it.get_prefix();
    }
}

bool is_trace_class_enabled(name const & n) {
    if (!is_trace_enabled())
        return false;
    if (is_trace_class_set(get_disabled_trace_classes(), n))
        return false; // it was explicitly disabled
    return is_trace_class_set(get_enabled_trace_classes(), n);
}


void scope_trace_env::init(environment * env, options * opts, abstract_type_context * ctx) {
    m_enable_sz  = get_enabled_trace_classes().size();
    m_disable_sz = get_disabled_trace_classes().size();
    m_old_env    = g_env;
    m_old_opts   = g_opts;
    m_old_ctx    = g_ctx;
    g_env        = env;
    g_ctx        = ctx;
    name trace("trace");
    if (opts && g_opts != opts) {
        opts->for_each([&](name const & n) {
                if (is_prefix_of(trace, n)) {
                    name cls        = n.replace_prefix(trace, name());
                    if (opts->get_bool(n, false))
                        enable_trace_class(cls);
                    else
                        disable_trace_class(cls);
                }
            });
    }
    g_opts = opts;
}

scope_trace_env::scope_trace_env(environment const & env, options const & o, abstract_type_context & ctx) {
    init(const_cast<environment*>(&env), const_cast<options*>(&o), &ctx);
}

scope_trace_env::scope_trace_env(environment const & env, abstract_type_context & ctx) {
    init(const_cast<environment*>(&env), g_opts, &ctx);
}

scope_trace_env::scope_trace_env(options const & o) {
    init(g_env, const_cast<options*>(&o), g_ctx);
}

scope_trace_env::~scope_trace_env() {
    g_env  = const_cast<environment*>(m_old_env);
    g_opts = const_cast<options*>(m_old_opts);
    g_ctx  = m_old_ctx;
    get_enabled_trace_classes().resize(m_enable_sz);
    get_disabled_trace_classes().resize(m_disable_sz);
}

std::ostream & tout() {
    return std::cerr;
}

std::ostream & operator<<(std::ostream & ios, tclass const & c) {
    ios << "[" << c.m_cls << "] ";
    return ios;
}

void initialize_trace() {
    g_trace_classes = new name_set();
    g_trace_aliases = new name_map<name_set>();

    register_trace_class(name{"debug"});
}

void finalize_trace() {
    delete g_trace_classes;
    delete g_trace_aliases;
}

scope_traces_as_string::scope_traces_as_string() {
    m_redirected_ios = std::unique_ptr<io_state>(new io_state(get_global_ios()));
    m_buffer = std::make_shared<string_output_channel>();
    m_redirected_ios->set_regular_channel(m_buffer);
    m_redirected_ios->set_diagnostic_channel(m_buffer);
    m_scoped_ios = std::unique_ptr<scope_global_ios>(new scope_global_ios(*m_redirected_ios));
}

scope_traces_as_string::~scope_traces_as_string() {
}

/*
@[export lean_mk_metavar_ctx]
def mkMetavarContext : Unit → MetavarContext := fun _ => {}
*/
extern "C" lean_object* lean_mk_metavar_ctx(lean_object*);

/*
@[export lean_pp_expr]
def ppExprLegacy (env : Environment) (mctx : MetavarContext) (lctx : LocalContext) (opts : Options) (e : Expr) : IO Format :=
*/
extern "C" object * lean_pp_expr(object * env, object * mctx, object * lctx, object * opts, object * e, object * w);

/*
@[export lean_format_pretty]
def pretty (f : Format) (w : Nat := defWidth) : String :=
*/
extern "C" object * lean_format_pretty(object * f, object * w);

std::string pp_expr(environment const & env, options const & opts, expr const & e) {
    local_ctx lctx;
    object_ref fmt = get_io_result<object_ref>(lean_pp_expr(env.to_obj_arg(), lean_mk_metavar_ctx(lean_box(0)), lctx.to_obj_arg(), opts.to_obj_arg(),
                                                            e.to_obj_arg(), io_mk_world()));
    string_ref str(lean_format_pretty(fmt.to_obj_arg(), lean_unsigned_to_nat(80)));
    return str.to_std_string();
}

void trace_expr(environment const & env, options const & opts, expr const & e) {
    tout() << pp_expr(env, opts, e);
}

std::string trace_pp_expr(expr const & e) {
    return pp_expr(*g_env, *g_opts, e);
}
}