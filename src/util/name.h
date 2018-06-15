/*
Copyright (c) 2013 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Leonardo de Moura
*/
#pragma once
#include <string>
#include <iostream>
#include <functional>
#include <algorithm>
#include <utility>
#include "runtime/optional.h"
#include "runtime/serializer.h"
#include "util/rc.h"
#include "util/pair.h"
#include "util/list.h"
#include "util/object_ref.h"
#include "util/list_ref.h"

namespace lean {
constexpr char const * lean_name_separator = ".";
#ifdef _MSC_VER
constexpr char16_t id_begin_escape = L'\xab';
constexpr char16_t id_end_escape = L'\xbb';
#else
constexpr char16_t id_begin_escape = u'«';
constexpr char16_t id_end_escape = u'»';
#endif

bool is_id_first(unsigned char const * begin, unsigned char const * end);
inline bool is_id_first(char const * begin, char const * end) {
    return is_id_first(reinterpret_cast<unsigned char const *>(begin),
                      reinterpret_cast<unsigned char const *>(end));
}

bool is_id_rest(unsigned char const * begin, unsigned char const * end);
inline bool is_id_rest(char const * begin, char const * end) {
    return is_id_rest(reinterpret_cast<unsigned char const *>(begin),
                      reinterpret_cast<unsigned char const *>(end));
}

enum class name_kind { ANONYMOUS, STRING, NUMERAL };
/** \brief Hierarchical names. */
class name : public object_ref {
public:
    /* Low level primitives */
    static name_kind kind(object * o) { return static_cast<name_kind>(obj_tag(o)); }
    static bool is_anonymous(object * o) { return is_scalar(o); }
    static object * get_prefix(object * o) { return cnstr_obj(o, 0); }
    static object * get_string_obj(object * o) { return cnstr_obj(o, 1); }
    static char const * get_string(object * o) { return string_data(get_string_obj(o)); }
    static object * get_num_obj(object * o) { return cnstr_obj(o, 1); }
    static unsigned get_numeral(object * o) { return unbox(cnstr_obj(o, 1)); }
    static unsigned hash(object * o) { return cnstr_scalar<unsigned>(o, 2*sizeof(object*)); } // NOLINT
    static bool eq_core(object * o1, object * o2);
    static int cmp_core(object * o1, object * o2);
    size_t size_core(bool unicode) const;
private:
    friend name read_name(deserializer & d);
    explicit name(object * r):object_ref(r) { inc(r); }
    explicit name(object_ref && r):object_ref(r) {}
public:
    name():object_ref(box(static_cast<unsigned>(name_kind::ANONYMOUS))) {}
    name(name const & prefix, char const * name);
    name(name const & prefix, unsigned k);
    name(char const * n):name(name(), n) {}
    name(std::string const & s):name(s.c_str()) {}
    name(name const & other):object_ref(other) {}
    name(name && other):object_ref(other) {}
    /**
       \brief Create a hierarchical name using the given strings.
       Example: <code>name{"foo", "bla", "tst"}</code> creates the hierarchical
       name <tt>foo::bla::tst</tt>.
    */
    name(std::initializer_list<char const *> const & l);
    static name const & anonymous();
    /**
        \brief Create a unique internal name that is not meant to exposed
        to the user. Different modules require a unique name.
        The unique name is created using a numeric prefix.
        A module that needs to create several unique names should
        the following idiom:
        <code>
            name unique_prefix = name::mk_internal_unique_name();
            name unique_name_1(unique_prefix, 1);
            ...
            name unique_name_k(unique_prefix, k);
        </code>
    */
    static name mk_internal_unique_name();
    name & operator=(name const & other) { object_ref::operator=(other); return *this; }
    name & operator=(name && other) { object_ref::operator=(other); return *this; }
    unsigned hash() const { return is_scalar(raw()) ? 11 : hash(raw()); }
    /** \brief Return true iff \c n1 is a prefix of \c n2. */
    friend bool is_prefix_of(name const & n1, name const & n2);
    friend bool operator==(name const & a, name const & b) {
        if (a.raw() == b.raw())
            return true;
        if (is_scalar(a.raw()) != is_scalar(b.raw()))
            return false;
        if (a.hash() != b.hash())
            return false;
        return eq_core(a.raw(), b.raw());
    }
    friend bool operator!=(name const & a, name const & b) { return !(a == b); }
    friend bool operator==(name const & a, char const * b);
    friend bool operator!=(name const & a, char const * b) { return !(a == b); }
    /** \brief Total order on hierarchical names. */
    friend int cmp(name const & a, name const & b) { return cmp_core(a.raw(), b.raw()); }
    friend bool operator<(name const & a, name const & b) { return cmp(a, b) < 0; }
    friend bool operator>(name const & a, name const & b) { return cmp(a, b) > 0; }
    friend bool operator<=(name const & a, name const & b) { return cmp(a, b) <= 0; }
    friend bool operator>=(name const & a, name const & b) { return cmp(a, b) >= 0; }
    name_kind kind() const { return kind(raw()); }
    bool is_anonymous() const { return kind() == name_kind::ANONYMOUS; }
    bool is_string() const    { return kind() == name_kind::STRING; }
    bool is_numeral() const   { return kind() == name_kind::NUMERAL; }
    explicit operator bool() const { return kind() != name_kind::ANONYMOUS; }
    unsigned get_numeral() const { lean_assert(is_numeral()); return get_numeral(raw()); }
    char const * get_string() const { lean_assert(is_string()); return get_string(raw()); }
    name const & get_prefix() const {
        if (is_anonymous()) return *this;
        else return static_cast<name const &>(cnstr_obj_ref(*this, 0));
    }
    bool is_atomic() const { return is_anonymous() || kind(get_prefix(raw())) == name_kind::ANONYMOUS; }
    /** \brief Given a name of the form a_1.a_2. ... .a_k, return a_1 if k >= 1, or the empty name otherwise. */
    name get_root() const;
    /** \brief Convert this hierarchical name into a string. */
    std::string to_string(char const * sep = lean_name_separator) const;
    std::string escape(char const * sep = lean_name_separator) const;
    /** \brief Size of the this name (in characters). */
    size_t size() const;
    /** \brief Size of the this name in unicode. */
    size_t utf8_size() const;
    /** \brief Return true iff the name contains only safe ASCII chars */
    bool is_safe_ascii() const;
    friend std::ostream & operator<<(std::ostream & out, name const & n);
    /** \brief Concatenate the two given names. */
    friend name operator+(name const & n1, name const & n2);

    /**
        \brief Given a name of the form a_1.a_2. ... .a_k,
           If a_k is a string,  return a_1.a_2. ... .a_k', where a_k' is the string p concatenated with a_k.
           If a_k is a numeral, return a_1.a_2. ... .p.a_k
    */
    name append_before(char const * p) const;
    /**
        \brief Given a name of the form a_1.a_2. ... .a_k,
           If a_k is a string,  return a_1.a_2. ... .a_k', where a_k' is the string a_k concatenated with s.
           If a_k is a numeral, return a_1.a_2. ... .a_k.s
    */
    name append_after(char const * s) const;

    /**
        \brief Given a name of the form a_1.a_2. ... .a_k,
           If a_k is a string,  return a_1.a_2. ... .a_k', where a_k' is the string a_k concatenated with _i.
           Otherwise add _i as the last component.
    */
    name append_after(unsigned i) const;

    /**
        \brief Given a name of the form a_1.a_2. ... .a_k,
           If a_k is a string, return the name itself.
           Otherwise add the empty string as the last component.
    */
    name get_subscript_base() const;

    /**
        \brief Given a name of the form a_1.a_2. ... .a_k, determine whether it was produced by append_after(unsigned).
    */
    optional<pair<name, unsigned>> is_subscripted() const;

    /**
        \brief If prefix is a prefix of this name, then return a new name where the prefix is replaced with new_prefix.
        Otherwise, return this name.
    */
    name replace_prefix(name const & prefix, name const & new_prefix) const;

    friend void swap(name & a, name & b) { object_ref::swap(a, b); }
    /**
       \brief Quicker version of \c cmp that uses the hashcode.
       Remark: we should not use it when we want to order names using
       lexicographical order.
    */
    friend int quick_cmp(name const & a, name const & b) {
        if (a.raw() == b.raw())
            return 0;
        unsigned h1 = a.hash();
        unsigned h2 = b.hash();
        if (h1 != h2) {
            return h1 < h2 ? -1 : 1;
        } else if (a == b) {
            return 0;
        } else {
            return cmp(a, b);
        }
    }
    void serialize(serializer & s) const { s.write_object(raw()); }
};

name string_to_name(std::string const & str);

struct name_hash { unsigned operator()(name const & n) const { return n.hash(); } };
struct name_eq { bool operator()(name const & n1, name const & n2) const { return n1 == n2; } };
struct name_cmp { int operator()(name const & n1, name const & n2) const { return cmp(n1, n2); } };
struct name_quick_cmp { int operator()(name const & n1, name const & n2) const { return quick_cmp(n1, n2); } };

/** \brief Return true if \c p is part of \c n */
bool is_part_of(std::string const & p, name n);

/**
   \brief Return true iff the two given names are independent.
   That \c a is not a prefix of \c b, nor \c b is a prefix of \c a

   \remark forall a b c d,
               independent(a, b) => independent(join(a, c), join(b, d))
*/
inline bool independent(name const & a, name const & b) {
    return !is_prefix_of(a, b) && !is_prefix_of(b, a);
}

typedef pair<name, name> name_pair;
struct name_pair_quick_cmp {
    int operator()(name_pair const & p1, name_pair const & p2) const {
        int r = quick_cmp(p1.first, p2.first);
        if (r != 0) return r;
        return quick_cmp(p1.second, p2.second);
    }
};

typedef std::function<bool(name const &)> name_predicate; // NOLINT

inline serializer & operator<<(serializer & s, name const & n) { n.serialize(s); return s; }
inline name read_name(deserializer & d) { return name(d.read_object()); }
inline deserializer & operator>>(deserializer & d, name & n) { n = read_name(d); return d; }

/** \brief Return true if it is a lean internal name, i.e., the name starts with a `_` */
bool is_internal_name(name const & n);

typedef list_ref<name> names;
inline serializer & operator<<(serializer & s, names const & ns) { ns.serialize(s); return s; }
inline names read_names(deserializer & d) { return read_list_ref<name>(d); }

void initialize_name();
void finalize_name();
}
