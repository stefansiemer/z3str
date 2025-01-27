#include "tactic/tactical.h"
#include "tactic/str/ext_str_tactic.h"
#include "tactic/generic_model_converter.h"
#include "ast/ast_pp.h"
#include "ast/rewriter/expr_replacer.h"
#include "ast/rewriter/var_subst.h"
#include <iostream>
#include <map>
#include <set>

class ext_str_tactic : public tactic {
    struct imp {
        typedef generic_model_converter mc;

        ast_manager& m;
        seq_util u;
        arith_util m_autil;
        scoped_ptr<expr_replacer> m_replace;
        ptr_vector<expr> stack;

        //Maps for storing some information about expressions for PRE/POSTPROCESSING
        //std::map<expr* , expr*> const_var;
        //std::map<expr* , expr*> regex_agg;
        //~ std::map<expr* , expr*> neg_regex_agg;

        ref<mc> m_mc;
        bool m_produce_models;

        const rational str_to_int_finitization_upper_bound = rational(50);

        imp(ast_manager& _m, params_ref const& p) :
            m(_m),
            u(m),
            m_autil(m) {
            m_replace = mk_default_expr_replacer(m, false);
            updt_params(p);
        }

        void updt_params(params_ref const& p) {

        }

        // Returns a regular expression of the form [0-9]+.
        expr_ref re_one_or_more_digits() {
            //symbol zero("0");
            //symbol nine("9");
            expr_ref re_zero_to_nine(u.re.mk_range(u.str.mk_string("0"), u.str.mk_string("9")), m);
            return expr_ref(u.re.mk_plus(re_zero_to_nine), m);
        }

        // Returns a regular expression of the form (re.++ 0* (re.union 0 1 2 ... upper_bound))
        // Precondition: upper_bound >= 0
        expr_ref finitize_str_to_int(rational upper_bound) {
            if (upper_bound.is_neg()) {
                return expr_ref(m.mk_false(), m);
            }

            expr_ref_vector union_terms(m);
            for (rational i = rational::zero(); i <= upper_bound; i += 1) {
                //symbol i_string(i.to_string().c_str());
                expr_ref i_expr(u.re.mk_to_re(u.str.mk_string(i.to_string().c_str())), m);
                union_terms.push_back(i_expr);
            }

            //symbol zero("0");
            expr_ref re_leading_zeroes(u.re.mk_star(u.re.mk_to_re(u.str.mk_string("0"))), m);

            expr_ref re_union(union_terms.get(0), m);
            if (union_terms.size() > 1) {
                for (unsigned i = 1; i < union_terms.size(); ++i) {
                    re_union = expr_ref(u.re.mk_union(re_union, union_terms.get(i)), m);
                }
            }

            return expr_ref(u.re.mk_concat(re_leading_zeroes, re_union), m);
        }

		/* ####################################################################################
		 * REWRITE functions
		 * ####################################################################################
		 */

        void process_eq(expr* eq, goal_ref const& g, expr_substitution& sub) {
            if (sub.contains(eq)) return;

            expr* lhs;
            expr* rhs;

            m.is_eq(eq, lhs, rhs);

            // Rewrite: (= (str.to_int S) #const) and #const >= 0 --> (str.in_re S (0* ++ #const))
            {
                bool rewrite_applies = false;
                expr* string_subterm = nullptr;
                rational integer_constant;

                if (u.str.is_stoi(lhs, string_subterm)) {
                    if (m_autil.is_numeral(rhs, integer_constant)) {
                        rewrite_applies = true;
                    }
                } else if (u.str.is_stoi(rhs, string_subterm)) {
                    if (m_autil.is_numeral(lhs, integer_constant)) {
                        rewrite_applies = true;
                    }
                }

                if (rewrite_applies) {
                    if (integer_constant.is_nonneg()) {
                        TRACE("ext_str_tactic", tout << "str.to_int rewrite applies: " << mk_pp(string_subterm, m) << " in 0*" << integer_constant << std::endl;);
                        //symbol integer_constant_string(integer_constant.to_string().c_str());
                        //symbol zero("0");
                        expr_ref regex(u.re.mk_concat(u.re.mk_star(u.re.mk_to_re(u.str.mk_string("0"))), u.re.mk_to_re(u.str.mk_string(integer_constant.to_string().c_str()))), m);
                        expr_ref str_in_regex(u.re.mk_in_re(string_subterm, regex), m);
                        sub.insert(eq, str_in_regex);
                        stack.push_back(str_in_regex);
                        return;
                    }
                }
            }

            // Rewrite: (= (str.indexof H N 0) -1) --> not(str.contains H N)
            {
                expr * haystack = nullptr;
                expr * needle = nullptr;
                expr * index = nullptr;
                bool rewrite_applies = false;
                rational integer_constant;

                if (u.str.is_index(lhs, haystack, needle, index) && m_autil.is_numeral(index, integer_constant) && integer_constant.is_zero()) {
                    if (m_autil.is_numeral(rhs, integer_constant)) {
                        if (integer_constant.is_minus_one()) {
                            rewrite_applies = true;
                        }
                    }
                } else if (u.str.is_index(rhs, haystack, needle, index) && m_autil.is_numeral(index, integer_constant) && integer_constant.is_zero()) {
                    if (m_autil.is_numeral(lhs, integer_constant)) {
                        if (integer_constant.is_minus_one()) {
                            rewrite_applies = true;
                        }
                    }
                }

                if (rewrite_applies) {
                    TRACE("ext_str_tactic", tout << "str.indexof = -1 rewrite applies: " << mk_pp(haystack, m) << " does not contain " << mk_pp(needle, m) << std::endl;);
                    expr_ref h_not_in_n(m.mk_not(u.str.mk_contains(haystack, needle)), m);
                    sub.insert(eq, h_not_in_n);
                    stack.push_back(h_not_in_n);
                    return;
                }
            }

            // Rewrite 7: replace(x, y, x) = \epsilon -> x = \epsilon (apparently epsilon is always lhs from some other previous rewriting)
			{
				expr_ref emptyStr(u.str.mk_string(""), m);
                if (u.str.is_replace(rhs) && lhs == emptyStr) {
					expr* inthis; expr* replacethis; expr* bythis;
					u.str.is_replace(rhs, inthis, replacethis, bythis);
					if(inthis == bythis){
						//std::cout << "Rewrite 7: replace(x, y, x) = epsilon -> x = epsilon " <<  std::endl;
						expr_ref new_eq(m.mk_eq(inthis, lhs), m);
						//std::cout << mk_pp(new_eq, m) << std::endl;
						sub.insert(eq, new_eq);
						stack.push_back(new_eq);
						return;
                    }
                }
            }

			// Rewrite 8: replace(x, y, a) = epsilon -> x = epsilon and |y| >= 1 (a ist constant != epsilon)
			{
				zstring string_constant;
				expr_ref emptyStr(u.str.mk_string(""), m);
                if (u.str.is_replace(rhs) && lhs == emptyStr) {
					expr* inthis; expr* replacethis; expr* bythis;
					u.str.is_replace(rhs, inthis, replacethis, bythis);
					if(u.str.is_string(bythis, string_constant) && bythis != emptyStr){
						//std::cout << "Rewrite 8: replace(x, y, a) = epsilon -> x = epsilon and y != eps " <<  std::endl;
						expr_ref_vector thenItems(m);
						thenItems.push_back(m.mk_eq(inthis, lhs));
						thenItems.push_back(m.mk_not(m.mk_eq(replacethis, lhs)));
						expr_ref thenBranch(m.mk_and(thenItems), m);
						//std::cout << mk_pp(thenBranch, m) << std::endl;
						sub.insert(eq, thenBranch);
						stack.push_back(thenBranch);
						return;
                    }
                }
            }

			// Rewrite 9: replace(x, a, epsilon) = epsilon -> x = epsilon or x = a
			{
				zstring string_constant;
				expr_ref emptyStr(u.str.mk_string(""), m);
                if (u.str.is_replace(rhs) && lhs == emptyStr) {
					expr* inthis; expr* replacethis; expr* bythis;
					u.str.is_replace(rhs, inthis, replacethis, bythis);
					if(u.str.is_string(replacethis, string_constant) && replacethis != emptyStr && bythis == emptyStr){
						//std::cout << "Rewrite 9: replace(x, a, epsilon) = epsilon -> x = epsilon or x = a " <<  std::endl;
						expr_ref_vector thenItems(m);
						thenItems.push_back(m.mk_eq(inthis, lhs));
						thenItems.push_back(m.mk_eq(inthis, replacethis));
						expr_ref thenBranch(m.mk_or(thenItems), m);
						//std::cout << mk_pp(thenBranch, m) << std::endl;
						sub.insert(eq, thenBranch);
						stack.push_back(thenBranch);
						return;
                    }
                }
            }

			// Rewrite 13: a = replace(epsilon, x, y) -> x = epsilon and  y = a
			{
				zstring string_constant;
				expr_ref emptyStr(u.str.mk_string(""), m);
                if (u.str.is_replace(rhs) && lhs != emptyStr && u.str.is_string(lhs, string_constant)) {
					expr* inthis; expr* replacethis; expr* bythis;
					u.str.is_replace(rhs, inthis, replacethis, bythis);
					if(inthis == emptyStr){
						//std::cout << "Rewrite 13: a = replace(epsilon, x, y) -> x = epsilon and  y = a" <<  std::endl;
						expr_ref_vector thenItems(m);
						thenItems.push_back(m.mk_eq(replacethis, emptyStr));
						thenItems.push_back(m.mk_eq(bythis, lhs));
						expr_ref thenBranch(m.mk_and(thenItems), m);
						//std::cout << mk_pp(thenBranch, m) << std::endl;
						sub.insert(eq, thenBranch);
						stack.push_back(thenBranch);
						return;
                    }
                }

                if (u.str.is_replace(lhs) && rhs != emptyStr && u.str.is_string(rhs, string_constant)) {
					expr* inthis; expr* replacethis; expr* bythis;
					u.str.is_replace(lhs, inthis, replacethis, bythis);
					if(inthis == emptyStr){
						//std::cout << "Rewrite 13: a = replace(epsilon, x, y) -> x = epsilon and  y = a" <<  std::endl;
						expr_ref_vector thenItems(m);
						thenItems.push_back(m.mk_eq(replacethis, emptyStr));
						thenItems.push_back(m.mk_eq(bythis, rhs));
						expr_ref thenBranch(m.mk_and(thenItems), m);
						//std::cout << mk_pp(thenBranch, m) << std::endl;
						sub.insert(eq, thenBranch);
						stack.push_back(thenBranch);
						return;
                    }
                }
            }

            // Rewrite 14: x = replace(y, x, y) -> x = y
			{
                if (u.str.is_replace(rhs)) {
					expr* inthis; expr* replacethis; expr* bythis;
					u.str.is_replace(rhs, inthis, replacethis, bythis);
					if(lhs == replacethis && inthis == bythis){
						//std::cout << "Rewrite 14: x = replace(y, x, y) -> x = y " <<  std::endl;
						expr_ref new_eq(m.mk_eq(inthis, lhs), m);
						//std::cout << mk_pp(new_eq, m) << std::endl;
						sub.insert(eq, new_eq);
						stack.push_back(new_eq);
						return;
                    }
                }
				if (u.str.is_replace(lhs)) {
					expr* inthis; expr* replacethis; expr* bythis;
					u.str.is_replace(lhs, inthis, replacethis, bythis);
					if(rhs == replacethis && inthis == bythis){
						//std::cout << "Rewrite 14: x = replace(y, x, y) -> x = y " <<  std::endl;
						expr_ref new_eq(m.mk_eq(inthis, rhs), m);
						//std::cout << mk_pp(new_eq, m) << std::endl;
						sub.insert(eq, new_eq);
						stack.push_back(new_eq);
						return;
                    }
                }
            }


			// Rewrite 15: x = replace(x, a, b) -> not x in regex .* a .*
			{
				zstring a; zstring b;
                if (u.str.is_replace(rhs)) {
					expr* inthis; expr* replacethis; expr* bythis;
					u.str.is_replace(rhs, inthis, replacethis, bythis);
					if(lhs == inthis && u.str.is_string(replacethis, a) && u.str.is_string(bythis, b) && a != b){
						//std::cout << "Rewrite 15: x = replace(x, a, b) -> not x in regex .* a .* " <<  std::endl;
						expr_ref string_expr(u.str.mk_string(a), m);
						expr_ref string_expr_re(u.re.mk_to_re(string_expr), m);
						sort* re_str_sort = string_expr_re->get_sort();
						expr_ref re_any_string(u.re.mk_full_seq(re_str_sort), m);
						expr_ref regex(u.re.mk_concat(re_any_string, u.re.mk_concat(string_expr_re, re_any_string)), m);
						expr_ref str_in_regex(u.re.mk_in_re(lhs, regex), m);
						expr_ref str_notin_regex(m.mk_not(str_in_regex), m);
						//std::cout << mk_pp(str_notin_regex, m) << std::endl;
						sub.insert(eq, str_notin_regex);
						stack.push_back(str_notin_regex);
						return;
                    }
                }

				if (u.str.is_replace(lhs)) {
					expr* inthis; expr* replacethis; expr* bythis;
					u.str.is_replace(lhs, inthis, replacethis, bythis);
					if(rhs == inthis && u.str.is_string(replacethis, a) && u.str.is_string(bythis, b) && a != b){
						//std::cout << "Rewrite 15: x = replace(x, a, b) -> not x in regex .* a .* " <<  std::endl;
						expr_ref string_expr(u.str.mk_string(a), m);
						expr_ref string_expr_re(u.re.mk_to_re(string_expr), m);
						sort* re_str_sort = string_expr_re->get_sort();
						expr_ref re_any_string(u.re.mk_full_seq(re_str_sort), m);
						expr_ref regex(u.re.mk_concat(re_any_string, u.re.mk_concat(string_expr_re, re_any_string)), m);
						expr_ref str_in_regex(u.re.mk_in_re(rhs, regex), m);
						expr_ref str_notin_regex(m.mk_not(str_in_regex), m);
						//std::cout << mk_pp(str_notin_regex, m) << std::endl;
						sub.insert(eq, str_notin_regex);
						stack.push_back(str_notin_regex);
						return;
                    }
                }
			}

			// Rewrite 10: substring(x, 0, m) = epsilon and m > 0 -> x = epsilon
			//~ {
				//~ rational n_int; rational m_int;
				//~ expr_ref emptyStr(u.str.mk_string(""), m);
				//~ //expr_ref zeroInt(m.mk_int(0), m);
				//~ zstring string_constant;
				//~ if(u.str.is_extract(lhs) && rhs == emptyStr){
					//~ expr* inthis; expr* fromhere; expr* substrlen;
					//~ u.str.is_extract(lhs, inthis, fromhere, substrlen);
					//~ if(m_autil.is_numeral(fromhere, n_int) && m_autil.is_numeral(substrlen, m_int))
						//~ if(n_int == 0 && m_int > 0 ){
							//~ std::cout << "Rewrite 10: substring(x, 0, m) = epsilon and m > 0 -> x = epsilon" <<  std::endl;
							//~ expr_ref new_eq(m.mk_eq(inthis, emptyStr), m);
							//~ std::cout << mk_pp(new_eq, m) << std::endl;
							//~ sub.insert(eq, new_eq);
							//~ stack.push_back(new_eq);
							//~ return;
						//~ }
				//~ }
			//~ }

            stack.push_back(lhs);
            stack.push_back(rhs);
        }

        void process_regex_membership(expr * str_in_re, goal_ref const & g, expr_substitution & sub) {
            if (sub.contains(str_in_re)) return;

            expr * str_term;
            expr * re_term;

            u.str.is_in_re(str_in_re, str_term, re_term);

            // Rewrite: (str.in_re S re.allchar) --> (= (str.len S) 1)
            {
                if (u.re.is_full_char(re_term)) {
                    expr_ref subst(m.mk_eq(u.str.mk_length(str_term), m_autil.mk_numeral(rational::one(), true)), m);
                    sub.insert(str_in_re, subst);
                    stack.push_back(subst);
                    return;
                }
            }


            stack.push_back(str_term);
            stack.push_back(re_term);
        }

        void process_le(expr* le, goal_ref const& g, expr_substitution& sub) {
            if (sub.contains(le)) return;

            expr* lhs;
            expr* rhs;
            m_autil.is_le(le, lhs, rhs);

            // Rewrite: (<= (str.to.int S) #const) and #const is not too large ==> (str.to.int S) = -1 OR (str.in_re S (0* ++ (0, 1, 2, ..., #const)))
            {
                expr* string_subterm = nullptr;
                rational integer_constant;
                if (m_autil.is_numeral(rhs, integer_constant) && integer_constant.is_nonneg() && integer_constant <= str_to_int_finitization_upper_bound) {
                    if (u.str.is_stoi(lhs, string_subterm)) {
                        TRACE("ext_str_tactic", tout << "str.to_int finitization applies: " << mk_pp(rhs, m) << " <= " << integer_constant << std::endl;);
                        expr_ref re_finite = finitize_str_to_int(integer_constant);
                        expr_ref subst(m.mk_or(m.mk_not(u.re.mk_in_re(string_subterm, re_one_or_more_digits())), u.re.mk_in_re(string_subterm, re_finite)), m);
                        TRACE("ext_str_tactic", tout << mk_pp(subst, m) << std::endl;);
                        sub.insert(le, subst);
                    }
                }
            }

            // Rewrite: (str.indexof H N 0) <= -1 --> not(str.contains H N)
            {
                expr * haystack = nullptr;
                expr * needle = nullptr;
                expr * index = nullptr;
                bool rewrite_applies = false;
                rational integer_constant;

                if (u.str.is_index(lhs, haystack, needle, index) && m_autil.is_numeral(index, integer_constant) && integer_constant.is_zero()) {
                    if (m_autil.is_numeral(rhs, integer_constant)) {
                        if (integer_constant.is_minus_one()) {
                            rewrite_applies = true;
                        }
                    }
                }

                if (rewrite_applies) {
                    TRACE("ext_str_tactic", tout << "str.indexof <= -1 rewrite applies: " << mk_pp(haystack, m) << " does not contain " << mk_pp(needle, m) << std::endl;);
                    expr_ref h_not_in_n(m.mk_not(u.str.mk_contains(haystack, needle)), m);
                    sub.insert(le, h_not_in_n);
                }
            }

            stack.push_back(lhs);
            stack.push_back(rhs);
        }

        void process_ge(expr* ge, goal_ref const& g, expr_substitution& sub) {
            if (sub.contains(ge)) return;

            expr* lhs;
            expr* rhs;
            m_autil.is_ge(ge, lhs, rhs);

            // Rewrite: (>= #const (str.to.int S)) and #const is not too large ==> (str.to.int S) = -1 OR (str.in_re S (0* ++ (0, 1, 2, ..., #const)))
            {
                expr* string_subterm = nullptr;
                rational integer_constant;
                if (m_autil.is_numeral(lhs, integer_constant) && integer_constant.is_nonneg() && integer_constant <= str_to_int_finitization_upper_bound) {
                    if (u.str.is_stoi(rhs, string_subterm)) {
                        TRACE("ext_str_tactic", tout << "str.to_int finitization applies: " << mk_pp(rhs, m) << " <= " << integer_constant << std::endl;);
                        expr_ref re_finite = finitize_str_to_int(integer_constant);
                        expr_ref subst(m.mk_or(m.mk_not(u.re.mk_in_re(string_subterm, re_one_or_more_digits())), u.re.mk_in_re(string_subterm, re_finite)), m);
                        TRACE("ext_str_tactic", tout << mk_pp(subst, m) << std::endl;);
                        sub.insert(ge, subst);
                    }
                }
            }

            // Rewrite: (str.indexof H N 0) >= 0 --> (str.contains H N)
            {
                expr * haystack = nullptr;
                expr * needle = nullptr;
                expr * index = nullptr;
                bool rewrite_applies = false;
                rational integer_constant;


                if (u.str.is_index(lhs, haystack, needle, index) && m_autil.is_numeral(index, integer_constant) && integer_constant.is_zero()) {
                    if (m_autil.is_numeral(rhs, integer_constant)) {
                        if (integer_constant.is_zero()) {
                            rewrite_applies = true;
                        }
                    }
                }

                if (rewrite_applies) {
                    TRACE("ext_str_tactic", tout << "str.indexof >= 0 rewrite applies: " << mk_pp(haystack, m) << " contains " << mk_pp(needle, m) << std::endl;);
					          // std::cout << "Rewrite: (str.indexof H N 0) >= 0 --> (str.contains H N)" << std::endl;
                    expr_ref h_in_n(u.str.mk_contains(haystack, needle), m);
                    sub.insert(ge, h_in_n);
                }
            }

            stack.push_back(lhs);
            stack.push_back(rhs);
        }

        void process_lt(expr* lt, goal_ref const& g, expr_substitution& sub) {
            if (sub.contains(lt)) return;

            expr* lhs;
            expr* rhs;
            m_autil.is_lt(lt, lhs, rhs);

            // Rewrite: (< (str.to.int S) #const) and #const is not too large ==> (str.to.int S) = -1 OR (str.in_re S (0* ++ (0, 1, 2, ..., #const-1)))
            {
                expr* string_subterm = nullptr;
                rational integer_constant;
                if (m_autil.is_numeral(rhs, integer_constant) && integer_constant >= rational(1) && integer_constant <= str_to_int_finitization_upper_bound) {
                    if (u.str.is_stoi(lhs, string_subterm)) {
                        TRACE("ext_str_tactic", tout << "str.to_int finitization applies: " << mk_pp(rhs, m) << " <= " << integer_constant - 1 << std::endl;);
                        expr_ref re_finite = finitize_str_to_int(integer_constant - 1);
                        expr_ref subst(m.mk_or(m.mk_not(u.re.mk_in_re(string_subterm, re_one_or_more_digits())), u.re.mk_in_re(string_subterm, re_finite)), m);
                        TRACE("ext_str_tactic", tout << mk_pp(subst, m) << std::endl;);
                        sub.insert(lt, subst);
                    }
                }
            }

            // Rewrite: (str.indexof H N 0) < n < 0 --> not(str.contains H N)
            {
                expr * haystack = nullptr;
                expr * needle = nullptr;
                expr * index = nullptr;
                bool rewrite_applies = false;
                rational integer_constant;

                if (u.str.is_index(lhs, haystack, needle, index) && m_autil.is_numeral(index, integer_constant) && integer_constant.is_zero()) {
                    if (m_autil.is_numeral(rhs, integer_constant)) {
                        if (integer_constant.is_neg()) {
                            rewrite_applies = true;
                        }
                    }
                }

                if (rewrite_applies) {
                    TRACE("ext_str_tactic", tout << "str.indexof < negative rewrite applies: " << mk_pp(haystack, m) << " does not contain " << mk_pp(needle, m) << std::endl;);
                    expr_ref h_not_in_n(m.mk_not(u.str.mk_contains(haystack, needle)), m);
                    sub.insert(lt, h_not_in_n);
                }
            }

            stack.push_back(lhs);
            stack.push_back(rhs);
        }

        void process_gt(expr* gt, goal_ref const& g, expr_substitution& sub) {
            if (sub.contains(gt)) return;

            expr* lhs;
            expr* rhs;
            m_autil.is_gt(gt, lhs, rhs);

            // Rewrite: (> #const (str.to.int S)) and #const is not too large ==> (str.to.int S) = -1 OR (str.in_re S (0* ++ (0, 1, 2, ..., #const-1)))
            {
                expr* string_subterm = nullptr;
                rational integer_constant;
                if (m_autil.is_numeral(lhs, integer_constant) && integer_constant >= rational(1) && integer_constant <= str_to_int_finitization_upper_bound) {
                    if (u.str.is_stoi(rhs, string_subterm)) {
                        TRACE("ext_str_tactic", tout << "str.to_int finitization applies: " << mk_pp(rhs, m) << " <= " << integer_constant-1 << std::endl;);
                        expr_ref re_finite = finitize_str_to_int(integer_constant-1);
                        expr_ref subst(m.mk_or(m.mk_not(u.re.mk_in_re(string_subterm, re_one_or_more_digits())), u.re.mk_in_re(string_subterm, re_finite)), m);
                        TRACE("ext_str_tactic", tout << mk_pp(subst, m) << std::endl;);
                        sub.insert(gt, subst);
                    }
                }
            }

            // Rewrite: (str.indexof H N 0) > -1 --> (str.contains H N)
            {
                expr * haystack = nullptr;
                expr * needle = nullptr;
                expr * index = nullptr;
                bool rewrite_applies = false;
                rational integer_constant;

                if (u.str.is_index(lhs, haystack, needle, index) && m_autil.is_numeral(index, integer_constant) && integer_constant.is_zero()) {
                    if (m_autil.is_numeral(rhs, integer_constant)) {
                        if (integer_constant.is_minus_one()) {
                            rewrite_applies = true;
                        }
                    }
                }

                if (rewrite_applies) {
                    TRACE("ext_str_tactic", tout << "str.indexof > -1 rewrite applies: " << mk_pp(haystack, m) << " contains " << mk_pp(needle, m) << std::endl;);
                    expr_ref h_in_n(u.str.mk_contains(haystack, needle), m);
                    sub.insert(gt, h_in_n);
                }
            }

            stack.push_back(lhs);
            stack.push_back(rhs);
        }

        void process_prefix(expr* prefix, goal_ref const& g, expr_substitution& sub) {
            if (sub.contains(prefix)) return;

            expr* needle;
            expr* haystack;
            u.str.is_prefix(prefix, needle, haystack);

           //~ //Replace a Variable that has been marked as a constant before by a constant
            //~ std::map<expr*, expr*>::iterator it;
            //~ it = const_var.find(needle);
            //~ if(it != const_var.end()){
				//~ needle = it->second;
			//~ }

            // Rewrite: (str.prefixof "constant" S) --> (str.in_re S ("constant" ++ .*))
            {
                zstring string_constant;
                if (u.str.is_string(needle, string_constant)) {
                    TRACE("ext_str_tactic", tout << "str.prefixof rewrite applies: " << mk_pp(haystack, m) << " in " << string_constant << " .*" << std::endl;);
                    expr_ref string_expr(u.str.mk_string(string_constant), m);
                    expr_ref string_expr_re(u.re.mk_to_re(string_expr), m);
                    sort* re_str_sort = string_expr_re->get_sort();
                    expr_ref re_any_string(u.re.mk_full_seq(re_str_sort), m);
                    expr_ref regex(u.re.mk_concat(string_expr_re, re_any_string), m);
                    expr_ref str_in_regex(u.re.mk_in_re(haystack, regex), m);
                    sub.insert(prefix, str_in_regex);
                }
            }

            stack.push_back(needle);
            stack.push_back(haystack);
        }

        void process_suffix(expr* suffix, goal_ref const& g, expr_substitution& sub) {
            if (sub.contains(suffix)) return;

            expr* needle;
            expr* haystack;
            u.str.is_suffix(suffix, needle, haystack);


           //~ //Replace a Variable that has been marked as a constant before by a constant
            //~ std::map<expr*, expr*>::iterator it;
            //~ it = const_var.find(needle);
            //~ if(it != const_var.end()){
				//~ needle = it->second;
			//~ }

            // Rewrite: (str.suffixof "constant" S) --> (str.in_re S (.* ++ "constant"))
            {
                zstring string_constant;
                if (u.str.is_string(needle, string_constant)) {
                    TRACE("ext_str_tactic", tout << "str.suffixof rewrite applies: " << mk_pp(haystack, m) << " in .* " << string_constant << std::endl;);
                    expr_ref string_expr(u.str.mk_string(string_constant), m);
                    expr_ref string_expr_re(u.re.mk_to_re(string_expr), m);
                    sort* re_str_sort = string_expr_re->get_sort();
                    expr_ref re_any_string(u.re.mk_full_seq(re_str_sort), m);
                    expr_ref regex(u.re.mk_concat(re_any_string, string_expr_re), m);
                    expr_ref str_in_regex(u.re.mk_in_re(haystack, regex), m);
                    sub.insert(suffix, str_in_regex);
                }
            }

            stack.push_back(needle);
            stack.push_back(haystack);
        }

        void process_contains(expr* contains, goal_ref const& g, expr_substitution& sub) {
            if (sub.contains(contains)) return;

            expr * needle;
            expr * haystack;
            u.str.is_contains(contains, haystack, needle);


            //~ //Replace a Variable that has been marked as a constant before in rewrite-preprocessing
            //~ std::map<expr*, expr*>::iterator it;
            //~ it = const_var.find(needle);
            //~ if(it != const_var.end()){
				//~ needle = it->second;
			//~ }

            // Rewrite: (str.contains X "const") -> (str.in_re X (re.++ .* "const" .*))
            {
                zstring string_constant;
                if (u.str.is_string(needle, string_constant)) {
                    //TRACE("ext_str_tactic", tout << "str.contains rewrite applies: " << mk_pp(haystack, m) << " in .* \"" << string_constant << "\" .*" << std::endl;);
                    //std::cout << "Rewrite: (str.contains X const) -> (str.in_re X (re.++ .* const .*))" << std::endl;
                    expr_ref string_expr(u.str.mk_string(string_constant), m);
                    expr_ref string_expr_re(u.re.mk_to_re(string_expr), m);
                    sort* re_str_sort = string_expr_re->get_sort();
                    expr_ref re_any_string(u.re.mk_full_seq(re_str_sort), m);
                    expr_ref regex(u.re.mk_concat(re_any_string, u.re.mk_concat(string_expr_re, re_any_string)), m);
                    expr_ref str_in_regex(u.re.mk_in_re(haystack, regex), m);
                    sub.insert(contains, str_in_regex);

					//Put this here in case smth is different in other rewrites (Stefan)
					stack.push_back(str_in_regex);
					return;
                }
            }

			// Rewrite 28: contains(substring x n |y|)) y) -> y = substring (x,n,|y|)
			{
                if (u.str.is_extract(haystack)) {
					expr* inthis; expr* fromhere; expr* substrlen;
					u.str.is_extract(haystack, inthis, fromhere, substrlen);
					expr* substrlen_inner; u.str.is_length(substrlen, substrlen_inner);
					if(needle == substrlen_inner){
						//~ std::cout << "Rewrite 28: contains(substring x n |y|)) y) -> y = substring (x,n,|y|) " <<  std::endl;
						expr_ref new_eq(m.mk_eq(needle, haystack), m);
						//~ std::cout << mk_pp(new_eq, m) << std::endl;
						sub.insert(contains, new_eq);
						stack.push_back(new_eq);
						return;
                    }
                }
            }


            // Rewrite 29: contains(replace(x,y,x), y) -> contains(x,y)
			// Y and X does not need to be a var, but only the same expressions (==) seems to work as intended
			{
                if (u.str.is_replace(haystack)) {
					expr* inthis; expr* replacethis; expr* bythis;
					u.str.is_replace(haystack, inthis, replacethis, bythis);
					if(inthis == bythis && replacethis == needle){
						//~ std::cout << "Rewrite 29: contains(replace(x,y,x), y) -> contains(x,y) " <<  std::endl;
						expr_ref new_contains(u.str.mk_contains(inthis, needle), m);
						//std::cout << mk_pp(new_contains, m) << std::endl;
						sub.insert(contains, new_contains);
						stack.push_back(new_contains);
						return;
                    }
                }
            }

            // Rewrite 33: contains(x, replace(y,x,y)) -> contains(x,y)
			{
				if (u.str.is_replace(needle)) {
					expr* inthis; expr* replacethis; expr* bythis;
					u.str.is_replace(needle, inthis, replacethis, bythis);
					if(inthis == bythis && replacethis == haystack){
						//~ std::cout << "Rewrite 33: contains(x, replace(y,x,y)) -> contains(x,y) " <<  std::endl;
						expr_ref new_contains(u.str.mk_contains(haystack, inthis), m);
						//std::cout << mk_pp(new_contains, m) << std::endl;
						sub.insert(contains, new_contains);
						stack.push_back(new_contains);
						return;
                    }
                }
            }

			stack.push_back(needle);
			stack.push_back(haystack);

        }

		void process_replace(expr* replace, goal_ref const& g, expr_substitution& sub) {
            if (sub.contains(replace)) return;

            expr* replacebase;
            expr* replacefind;
            expr* replacesubs;
            u.str.is_replace(replace, replacebase, replacefind, replacesubs);

            // Rewrite 43 replace(x, replace(x,y,x), x) = x
            {
                if (u.str.is_replace(replacefind)) {
					expr* innerbase; expr* innerfind; expr* innersubs;
					u.str.is_replace(replacefind, innerbase, innerfind, innersubs);
					if(replacebase == innerbase && replacebase == innersubs && replacebase == replacesubs){
						//~ std::cout << "Rewrite 43: replace(x, replace(x,y,x), x) = x " <<  std::endl;
						//std::cout << mk_pp(replacebase, m) << std::endl;
						sub.insert(replace, replacebase);
						stack.push_back(replacebase);
						return;
                    }
                }
            }

            // Rewrite 44 replace(x, replace(y,x,y), w) = replace(x,y,w)
             {
                if (u.str.is_replace(replacefind)) {
					expr* innerbase; expr* innerfind; expr* innersubs;
					u.str.is_replace(replacefind, innerbase, innerfind, innersubs);
					if(replacebase == innerfind && innerbase == innersubs){
						//~ std::cout << "Rewrite 44: replace(x, replace(y,x,y), w) = replace(x,y,w) " <<  std::endl;
						expr_ref new_repl(u.str.mk_replace(replacebase, innerbase, replacesubs), m);
						//~ std::cout << mk_pp(new_repl, m) << std::endl;
						sub.insert(replace, new_repl);
						stack.push_back(new_repl);
						return;
                    }
                }
            }

            // Rewrite 48 replace(x, y, replace(y,x,y)) = x
			 {
                if (u.str.is_replace(replacesubs)) {
					expr* innerbase; expr* innerfind; expr* innersubs;
					u.str.is_replace(replacesubs, innerbase, innerfind, innersubs);
					if(replacebase == innerfind && replacefind == innerbase && replacefind == innersubs){
						//std::cout << "Rewrite 48: replace(x, y, replace(y,x,y)) = x " <<  std::endl;
						//std::cout << mk_pp(replacebase, m) << std::endl;
						sub.insert(replace, replacebase);
						stack.push_back(replacebase);
						return;
                    }
                }
            }

			stack.push_back(replacebase);
			stack.push_back(replacefind);
			stack.push_back(replacesubs);


			// This seems to be already in some simplifier before our rewrite rules.
			// Rewrite 37: replace( (++ X Y) X Z) -> (++ Z Y)
			//~ {
                //~ if (u.str.is_concat(replacebase)) {
					//~ expr* left; expr* right;
					//~ u.str.is_concat(replacebase, left, right);
					//~ if(left == replacefind){
						//~ std::cout << "Rewrite 37: replace( (++ X Y) X Z) -> (++ Z Y) " <<  std::endl;
						//~ expr_ref new_conc(u.str.mk_concat(replacesubs, right), m);
						//~ std::cout << mk_pp(new_conc, m) << std::endl;
						//~ sub.insert(replace, new_conc);
						//~ stack.push_back(replacesubs);
						//~ stack.push_back(right);
						//~ return;
                    //~ }
                //~ }
            //~ }
        }

		/* ####################################################################################
		 * PREPROCESSING functions
		 * ####################################################################################
		 */

		//~ void preprocess_eq(expr* eq) {
            //~ expr* lhs;
            //~ expr* rhs;

            //~ m.is_eq(eq, lhs, rhs);

			//~ //A variable is eq to a constant on the top level
			//~ {
				//~ zstring string_constant;
                //~ if (u.str.is_var(lhs) && u.str.is_string(rhs, string_constant)) {
					//~ //std::cout << "found string constant variable" << std::endl;
					//~ const_var[lhs] = rhs;
				//~ }
			//~ }

			//~ //A variable is eq to a constant on the top level
			//~ {
				//~ zstring string_constant;
                //~ if (u.str.is_var(rhs) && u.str.is_string(lhs, string_constant)) {
					//~ //std::cout << "found string constant variabe" << std::endl;
					//~ const_var[rhs] = lhs;
				//~ }
			//~ }
		//~ }


		/* ####################################################################################
		 * POSTPROCESSING functions
		 * ####################################################################################
		 */

		//~ void postprocess_regex_membership(expr* str_in_re) {

            //~ expr * str_term;
            //~ expr * re_term;

            //~ u.str.is_in_re(str_in_re, str_term, re_term);

            //~ {
				//~ std::map<expr*, expr*>::iterator it;
				//~ it = regex_agg.find(str_term);
				//~ if(it == regex_agg.end()){
					//~ regex_agg[str_term] = re_term;
				//~ } else {
					//~ std::cout << mk_pp(str_term, m) << std::endl;
					//~ std::cout << mk_pp(regex_agg[str_term], m) << std::endl;
					//~ expr* old_re_term = it->second;
                    //~ expr_ref new_re_term(u.re.mk_inter(old_re_term, re_term), m);
					//~ it->second = new_re_term;
				//~ }

			//~ }
        //~ }

		//~ void postprocess_not(expr* isnot) {

            //~ expr * inner_term;

            //~ m.is_not(isnot, inner_term);
            //std::cout << "Found inner negate" << std::endl;
            //std::cout << mk_pp(inner_term, m) << std::endl;

            //~ {
				//~ if(u.str.is_in_re(inner_term)){
					//~ expr* str_term;
					//~ expr* re_term;
					//~ u.str.is_in_re(inner_term, str_term, re_term);
					//~ std::map<expr*, expr*>::iterator it;
					//~ it = regex_agg.find(str_term);
					//~ if(it == regex_agg.end()){
						//~ zstring string_constant;
						//~ expr_ref string_expr(u.str.mk_string(string_constant), m);
						//~ expr_ref string_expr_re(u.re.mk_to_re(string_expr), m);
						//~ sort* re_str_sort = string_expr_re->get_sort();
						//~ expr_ref re_any_string(u.re.mk_full_seq(re_str_sort), m);
						//~ expr_ref new_re_term(u.re.mk_diff(re_any_string, re_term), m);
						//~ std::cout << mk_pp(new_re_term, m) << std::endl;
						//~ regex_agg.insert(std::make_pair(str_term, new_re_term));
						//~ regex_agg[str_term] = new_re_term;
					//~ }else {
						//~ expr* old_re_term = it->second;
						//~ expr_ref new_re_term(u.re.mk_diff(old_re_term, re_term), m);
						//~ it->second = new_re_term;
						//~ expr_ref new_re_term(u.re.mk_complement(re_term), m);
						//~ expr_ref inter_new_old(u.re.mk_inter(it->second, new_re_term), m);
						//~ it->second = inter_new_old;
					//~ }
				//~ }
			//~ }
        //~ }

		/* ####################################################################################
		 * TRAVERSING formula
		 * ####################################################################################
		 */
        void operator()(goal_ref const& g, goal_ref_buffer& result) {
            SASSERT(g->is_well_formed());
            tactic_report report("ext_str", *g);
            fail_if_proof_generation("ext_str", g);
            m_produce_models = g->models_enabled();
            if (m_produce_models) {
                m_mc = alloc(generic_model_converter, m, "ext_str");
            } else {
                m_mc = nullptr;
            }

            SASSERT(g->is_well_formed());

            result.reset();

            TRACE("ext_str_tactic", tout << "BEFORE:" << std::endl; g->display(tout););

            if (g->inconsistent()) {
                result.push_back(g.get());
                return;
            }


			//Preprocessing and collecting info before the rewrite process
			unsigned size = g->size();
            //~ for (unsigned idx = 0; idx < size; ++idx) {
                //~ if (g->inconsistent()) break;
                //~ expr* curr = g->form(idx);
                //~ if (!is_app(curr)) continue;

                //~ if (m.is_eq(curr)) {
					//~ preprocess_eq(curr);
				//~ }
			//~ }



            expr_substitution sub(m);
            //std::cout << size << std::endl;
            for (unsigned idx = 0; idx < size; ++idx) {
                if (g->inconsistent()) break;
                expr* curr = g->form(idx);
				//std::cout << mk_pp(curr, m) << std::endl;
                if (is_app(curr)) {
                    stack.reset();
                    stack.push_back(curr);

                    while (!stack.empty()) {
                        curr = stack.back();
                        stack.pop_back();

                        if (!is_app(curr)) continue;

                        TRACE("ext_str_tactic", tout << "curr: " << mk_pp(curr, m) << std::endl;);

                        if (m.is_eq(curr)) {
                            process_eq(curr, g, sub);
                        } else if (m_autil.is_le(curr)) {
                            process_le(curr, g, sub);
                        } else if (m_autil.is_ge(curr)) {
                            process_ge(curr, g, sub);
                        } else if (m_autil.is_lt(curr)) {
                            process_lt(curr, g, sub);
                        } else if (m_autil.is_gt(curr)) {
                            process_gt(curr, g, sub);
                        } else if (u.str.is_prefix(curr)) {
                            process_prefix(curr, g, sub);
                        } else if (u.str.is_suffix(curr)) {
                            process_suffix(curr, g, sub);
                        } else if (u.str.is_contains(curr)) {
                            process_contains(curr, g, sub);
						} else if (u.str.is_replace(curr)) {
                            process_replace(curr, g, sub);
                        } else if (u.str.is_in_re(curr)) {
                            process_regex_membership(curr, g, sub);
                        } else {
                            app * a = to_app(curr);
                            for (unsigned i = 0; i < a->get_num_args(); ++i) {
                                expr * arg = a->get_arg(i);
                                stack.push_back(arg);
                            }
                        }
                    }
                }
            }

            m_replace->set_substitution(&sub);

            for (unsigned i = 0; i < g->size(); ++i) {
                expr_ref new_curr(m);
                proof_ref new_pr(m);
                (*m_replace)(g->form(i), new_curr, new_pr);
                if (m.proofs_enabled() && !new_pr) {
                    new_pr = m.mk_rewrite(g->form(i), new_curr);
                    new_pr = m.mk_modus_ponens(g->pr(i), new_pr);
                }
                g->update(i, new_curr, new_pr, g->dep(i));
            }


			//~ //Postprocessing and aggregating after the rewrite process
            //~ for (unsigned idx = 0; idx < size; ++idx) {
                //~ if (g->inconsistent()) break;
                //~ expr* curr = g->form(idx);
                //~ if (!is_app(curr)) continue;

                //~ if (u.str.is_in_re(curr)) {
					//~ postprocess_regex_membership(curr);
				//~ } else if (m.is_not(curr)) {
					//~ postprocess_not(curr);
				//~ }
			//~ }


			//~ std::cout << "Aggregates Start" << std::endl;
			//~ //Print out the post-rewrite aggregated regex
			//~ for(std::map<expr*, expr*>::iterator it = regex_agg.begin(); it!=regex_agg.end(); it++){
				//~ std::cout << mk_pp(it->second, m) << std::endl;
			//~ }
			//~ std::cout << "Aggregates End" << std::endl;

            /*
			std::cout << "post rewrite ast ##################################" << std::endl;
			size = g->size();
			//Print out the post-rewrite formula
            for (unsigned idx = 0; idx < size; ++idx) {
                if (g->inconsistent()) break;
                expr* curr = g->form(idx);
				std::cout << mk_pp(curr, m) << std::endl;
			}*/


            /*
            for (auto e : m_delayed_assertions) {
                g->assert_expr(e);
            }

            if (m_mc) {
                for (auto v : m_fresh_vars) {
                    m_mc->hide(to_app(v)->get_decl());
                }
            }

            if (m_produce_models && !m_fresh_vars.empty()) {
                g->add(m_mc.get());
            }
            */
            g->inc_depth();
            result.push_back(g.get());
            SASSERT(g->is_well_formed());
            TRACE("ext_str_tactic", tout << "AFTER: " << std::endl; g->display(tout);
            if (g->mc()) g->mc()->display(tout); tout << std::endl;);
        }
    };

    imp* m_imp;
    params_ref m_params;

public:
    ext_str_tactic(ast_manager& m, params_ref const& p) :
        m_params(p) {
        m_imp = alloc(imp, m, p);
    }

    tactic* translate(ast_manager& m) override {
        return alloc(ext_str_tactic, m, m_params);
    }

    ~ext_str_tactic() override {
        dealloc(m_imp);
    }

    void updt_params(params_ref const& p) override {
	//m_params = p;
        m_params.append(p);
	m_imp->updt_params(p);
    }

    void operator()(goal_ref const& in, goal_ref_buffer& result) override {
        try {
            (*m_imp)(in, result);
        }
        catch (rewriter_exception& ex) {
            throw tactic_exception(ex.msg());
        }
    }

    void cleanup() override {
        imp* d = alloc(imp, m_imp->m, m_params);
        std::swap(d, m_imp);
        dealloc(d);
    }
    char const* name() const override { return "z3str3_tactic"; }
};

tactic * mk_ext_str_tactic(ast_manager & m, params_ref const & p) {
    return clean(alloc(ext_str_tactic, m, p));
}
