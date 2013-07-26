/*++
Copyright (c) 2006 Microsoft Corporation

Module Name:

    dl_mk_interp_tail_simplifier.h

Abstract:

    Rule transformer which simplifies interpreted tails

Author:

    Krystof Hoder (t-khoder) 2011-10-01.

Revision History:

--*/

#ifndef _DL_MK_INTERP_TAIL_SIMPLIFIER_H_
#define _DL_MK_INTERP_TAIL_SIMPLIFIER_H_

#include "dl_context.h"
#include "dl_rule_transformer.h"
#include "unifier.h"
#include "substitution.h"
#include "arith_decl_plugin.h"

namespace datalog {

    class mk_interp_tail_simplifier : public rule_transformer::plugin {

        class rule_substitution
        {
            ast_manager& m;
            context& m_context;
            substitution m_subst;
            unifier m_unif;

            rule * m_rule;

            void apply(app * a, app_ref& res);
        public:
            rule_substitution(context & ctx)
                : m(ctx.get_manager()), m_context(ctx), m_subst(m), m_unif(m), m_rule(0) {}

            /**
            Reset substitution and get it ready for working with rule r.
            
            As long as this object is used without a reset, the rule r must exist.
            */
            void reset(rule * r);

            /** Reset subtitution and unify tail tgt_idx of the target rule and the head of the src rule */
            bool unify(expr * e1, expr * e2);

            void get_result(rule_ref & res);

            void display(std::ostream& stm) {
                m_subst.display(stm);
            }
        };

        ast_manager &     m;
        context &         m_context;
        th_rewriter &     m_simp;
        arith_util        a;
        rule_substitution m_rule_subst;

        class normalizer_cfg;

        void simplify_expr(app * a, expr_ref& res);

        /** return true if some propagation was done */
        bool propagate_variable_equivalences(rule * r, rule_ref& res);

        /** Return true if something was modified */
        bool transform_rules(const rule_set & orig, rule_set & tgt);
    public:
        mk_interp_tail_simplifier(context & ctx, unsigned priority=40000)
            : plugin(priority),
            m(ctx.get_manager()),
            m_context(ctx),
            m_simp(ctx.get_rewriter()),
            a(m),
            m_rule_subst(ctx) {}

        /**If rule should be retained, assign transformed version to res and return true;
        if rule can be deleted, return false.
        
        This method is kind of useful, so it's public to allow other rules to use it,
        e.g. on their intermediate results.
        */
        bool transform_rule(rule * r, rule_ref& res);

        rule_set * operator()(rule_set const & source, model_converter_ref& mc, proof_converter_ref& pc);
    };

};

#endif /* _DL_MK_INTERP_TAIL_SIMPLIFIER_H_ */

