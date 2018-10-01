/-
Copyright (c) 2018 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.
Author: Sebastian Ullrich

Term-level parsers
-/
prelude
import init.lean.parser.level init.lean.parser.notation

namespace lean
namespace parser
open combinators parser.has_view monad_parsec

local postfix `?`:10000 := optional
local postfix *:10000 := combinators.many
local postfix +:10000 := combinators.many1

set_option class.instance_max_depth 200

@[derive parser.has_tokens parser.has_view]
def ident_univ_spec.parser : basic_parser :=
node! ident_univ_spec [".{", levels: level.parser+, "}"]

@[derive parser.has_tokens parser.has_view]
protected def term.ident.parser : term_parser :=
node! term.ident [id: ident.parser, univ: (monad_lift ident_univ_spec.parser)?]

namespace term
/-- Access leading term -/
def get_leading : trailing_term_parser := read
instance : has_tokens get_leading := default _
instance : has_view get_leading syntax := default _

@[derive parser.has_tokens parser.has_view]
def paren.parser : term_parser :=
node! «paren» ["(":max_prec,
  content: node! paren_content [
    term: term.parser,
    special: node_choice! paren_special {
      /- Do not allow trailing comma. Looks a bit weird and would clash with
      adding support for tuple sections (https://downloads.haskell.org/~ghc/8.2.1/docs/html/users_guide/glasgow_exts.html#tuple-sections). -/
      tuple: node! tuple [", ", tail: sep_by (term.parser 0) (symbol ", ") ff],
      typed: node! typed [" : ", type: term.parser],
    }?,
  ]?,
  ")"
]

@[derive parser.has_tokens parser.has_view]
def hole.parser : term_parser :=
node! hole [hole: symbol "_" max_prec]

@[derive parser.has_tokens parser.has_view]
def sort.parser : term_parser :=
node_choice! sort {"Sort":max_prec, "Type":max_prec}

section binder
@[derive has_tokens has_view]
def binder_ident.parser : term_parser :=
node_choice! binder_ident {id: ident.parser, hole: hole.parser}

@[derive has_tokens has_view]
def binder_content.parser : term_parser :=
node! binder_content [
  ids: binder_ident.parser+,
  type: node! binder_content_type [":", type: term.parser 0]?,
  default: node_choice! binder_default {
    val: node! binder_default_val [":=", term: term.parser 0],
    tac: node! binder_default_tac [".", term: term.parser 0]
  }?
]

@[derive parser.has_tokens parser.has_view]
def anonymous_constructor.parser : term_parser :=
node! anonymous_constructor ["⟨":max_prec, args: sep_by (term.parser 0) (symbol ","), "⟩"]

/- All binders must be surrounded with some kind of bracket. (e.g., '()', '{}', '[]').
   We use this feature when parsing examples/definitions/theorems. The goal is to avoid counter-intuitive
   declarations such as:

     example p : false := trivial
     def main proof : false := trivial

   which would be parsed as

     example (p : false) : _ := trivial

     def main (proof : false) : _ := trivial

   where `_` in both cases is elaborated into `true`. This issue was raised by @gebner in the slack channel.


   Remark: we still want implicit delimiters for lambda/pi expressions. That is, we want to
   write

       fun x : t, s
   or
       fun x, s

   instead of

       fun (x : t), s -/
@[derive has_tokens has_view]
def bracketed_binder.parser : term_parser :=
node_choice! bracketed_binder {
  explicit: node! explicit_binder ["(", content: node_choice! explicit_binder_content {
    «notation»: command.notation_like.parser,
    other: binder_content.parser
  }, right: symbol ")"],
  implicit: node! implicit_binder ["{", content: binder_content.parser, "}"],
  strict_implicit: node! strict_implicit_binder [
    left: unicode_symbol "⦃" "{{",
    content: binder_content.parser,
    right: unicode_symbol "⦄" "}}",
  ],
  inst_implicit: node! inst_implicit_binder ["[":max_prec, content: longest_match [
    node! inst_implicit_named_binder [id: ident.parser, " : ", type: term.parser 0],
    node! inst_implicit_anonymous_binder [type: term.parser 0]
  ], "]"],
  anonymous_constructor: anonymous_constructor.parser,
}

@[derive has_tokens has_view]
def binders.parser : term_parser :=
node! binders [
  leading_ids: binder_ident.parser*,
  remainder: node_choice! binders_remainder {
    type: node! binders_types [":", type: term.parser 0],
    -- we allow mixing like in `a (b : β) c`, but not `a : α (b : β) c : γ`
    mixed: node_choice! mixed_binder {
      bracketed: bracketed_binder.parser,
      id: ident.parser,
    }+,
  }?
]

end binder

@[derive parser.has_tokens parser.has_view]
def lambda.parser : term_parser :=
node! lambda [
  op: unicode_symbol "λ" "fun" max_prec,
  binders: binders.parser,
  ",",
  body: term.parser 0
]

@[derive parser.has_tokens parser.has_view]
def assume.parser : term_parser :=
node! «assume» [
  "assume ":max_prec,
  binders: node_choice! assume_binders {
    anonymous: node! assume_anonymous [": ", type: term.parser],
    binders: binders.parser
  },
  ", ",
  body: term.parser 0
]

@[derive parser.has_tokens parser.has_view]
def pi.parser : term_parser :=
node! pi [
  op: any_of [unicode_symbol "Π" "Pi" max_prec, unicode_symbol "∀" "forall" max_prec],
  binders: binders.parser,
  ",",
  range: term.parser 0
]

@[derive parser.has_tokens parser.has_view]
def explicit_ident.parser : term_parser :=
node! explicit [
  mod: node_choice! explicit_modifier {
    explicit: symbol "@" max_prec,
    partial_explicit: symbol "@@" max_prec
  },
  id: term.ident.parser
]

@[derive parser.has_tokens parser.has_view]
def from.parser : term_parser :=
node! «from» ["from ", proof: term.parser]

@[derive parser.has_tokens parser.has_view]
def have.parser : term_parser :=
node! «have» [
  "have ",
  id: (try node! have_id [id: ident.parser, " : "])?,
  prop: term.parser,
  proof: node_choice! have_proof {
    term: node! have_term [" := ", term: term.parser],
    «from»: node! have_from [", ", «from»: from.parser],
  },
  ", ",
  body: term.parser,
]

@[derive parser.has_tokens parser.has_view]
def show.parser : term_parser :=
node! «show» [
  "show ",
  prop: term.parser,
  ", ",
  «from»: from.parser,
]

@[derive parser.has_tokens parser.has_view]
def match.parser : term_parser :=
node! «match» [
  "match ",
  scrutinees: sep_by1 term.parser (symbol ", ") ff,
  type: node! match_type [" : ", type: term.parser]?,
  " with ",
  opt_bar: (symbol " | ")?,
  equations: sep_by1
    node! «match_equation» [
      lhs: sep_by1 term.parser (symbol ", ") ff, ":=", rhs: term.parser]
    (symbol " | ") ff,
]

@[derive parser.has_tokens parser.has_view]
def if.parser : term_parser :=
node! «if» [
  "if ",
  id: (try node! if_id [id: ident.parser, " : "])?,
  prop: term.parser,
  " then ",
  then_branch: term.parser,
  " else ",
  else_branch: term.parser,
]

-- TODO(Sebastian): replace with attribute
@[derive has_tokens]
def builtin_leading_parsers : list term_parser := [
  term.ident.parser,
  number.parser,
  paren.parser,
  hole.parser,
  sort.parser,
  lambda.parser,
  pi.parser,
  anonymous_constructor.parser,
  explicit_ident.parser,
  have.parser,
  show.parser,
  assume.parser,
  match.parser,
  if.parser
]

@[derive parser.has_tokens parser.has_view]
def sort_app.parser : trailing_term_parser :=
do { l ← get_leading, guard (try_view sort l).is_some } *>
node! sort_app [fn: get_leading, arg: monad_lift (level.parser max_prec).run]

@[derive parser.has_tokens parser.has_view]
def app.parser : trailing_term_parser :=
node! app [fn: get_leading, arg: term.parser max_prec]

@[derive parser.has_tokens parser.has_view]
def arrow.parser : trailing_term_parser :=
node! arrow [dom: get_leading, op: unicode_symbol "→" "->" 25, range: term.parser 24]

@[derive parser.has_tokens parser.has_view]
def projection.parser : trailing_term_parser :=
/- Use max_prec + 1 so that it bind more tightly than application:
   `a (b).c` should be parsed as `a ((b).c)`. -/
node! projection [
  term: get_leading,
  ".":max_prec.succ,
  proj: node_choice! projection_spec {
    id: parser.ident.parser,
    num: number.parser,
  },
]

@[derive has_tokens]
def builtin_trailing_parsers : list trailing_term_parser := [
  arrow.parser,
  projection.parser
]

end term

def term_parser.run (p : term_parser) : command_parser :=
do cfg ← read,
   let trailing : trailing_term_parser := (longest_match cfg.trailing_term_parsers) <|>
     -- The application parsers should only be tried as a fall-back;
     -- e.g. `a + b` should not be parsed as `a (+ b)`.
     --TODO(Sebastian): We should be able to remove this workaround using
     -- the proposed more robust precedence handling
     any_of [term.sort_app.parser, term.app.parser],
   adapt_reader coe $ pratt_parser (longest_match cfg.leading_term_parsers) trailing p

end parser
end lean
