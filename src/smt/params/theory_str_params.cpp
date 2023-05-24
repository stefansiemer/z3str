/*++
Module Name:

    theory_str_params.cpp

Abstract:

    Parameters for string theory plugin

Author:

    Murphy Berzish (mtrberzi) 2016-12-13

Revision History:

--*/

#include "smt/params/theory_str_params.h"
#include "smt/params/smt_params_helper.hpp"

void theory_str_params::updt_params(params_ref const & _p) {
    smt_params_helper p(_p);
    m_StrongArrangements = p.str_strong_arrangements();
    m_AggressiveLengthTesting = p.str_aggressive_length_testing();
    m_AggressiveValueTesting = p.str_aggressive_value_testing();
    m_AggressiveUnrollTesting = p.str_aggressive_unroll_testing();
    m_UseFastLengthTesterCache = p.str_fast_length_tester_cache();
    m_UseFastValueTesterCache = p.str_fast_value_tester_cache();
    m_StringConstantCache = p.str_string_constant_cache();
    m_OverlapTheoryAwarePriority = p.str_overlap_priority();
    m_RegexAutomata_DifficultyThreshold = p.str_regex_automata_difficulty_threshold();
    m_RegexAutomata_IntersectionDifficultyThreshold = p.str_regex_automata_intersection_difficulty_threshold();
    m_RegexAutomata_FailedAutomatonThreshold = p.str_regex_automata_failed_automaton_threshold();
    m_RegexAutomata_FailedIntersectionThreshold = p.str_regex_automata_failed_intersection_threshold();
    m_RegexAutomata_LengthAttemptThreshold = p.str_regex_automata_length_attempt_threshold();
    m_FixedLengthRefinement = p.str_fixed_length_refinement();

    m_PreMilliseconds = p.str_pre_milliseconds();
    m_MidMilliseconds = p.str_mid_milliseconds();
    m_MultisetCheck = p.str_multiset_check();
    m_RewriterTactic = p.str_rewriter_tactic();
    m_StrTactic = p.str_tactic();

    m_FixedLengthNaiveCounterexamples = p.str_fixed_length_naive_cex();

    m_ShareConstraints = p.str_share_constraints();
    m_SearchOverlaps = p.str_search_overlaps();
    m_SearchOverlapsMilliseconds = p.str_search_overlaps_milliseconds();
    m_FixedLengthPreprocessing = p.str_fixed_length_preprocessing();
    m_FixedLengthIterations = p.str_fixed_length_iterations();
    m_UseRegexPrefixSuffixHeuristic = p.str_regex_prefix_heuristic();
    m_RegexAutomata_ConstructLinearLengthConstraints = p.str_regex_automata_construct_linear_length_constraints();
    m_RegexAutomata_ConstructBounds = p.str_regex_automata_construct_bounds();
}

#define DISPLAY_PARAM(X) out << #X"=" << X << '\n';

void theory_str_params::display(std::ostream & out) const {
    DISPLAY_PARAM(m_StrongArrangements);
    DISPLAY_PARAM(m_AggressiveLengthTesting);
    DISPLAY_PARAM(m_AggressiveValueTesting);
    DISPLAY_PARAM(m_AggressiveUnrollTesting);
    DISPLAY_PARAM(m_UseFastLengthTesterCache);
    DISPLAY_PARAM(m_UseFastValueTesterCache);
    DISPLAY_PARAM(m_StringConstantCache);
    DISPLAY_PARAM(m_OverlapTheoryAwarePriority);
    DISPLAY_PARAM(m_RegexAutomata_DifficultyThreshold);
    DISPLAY_PARAM(m_RegexAutomata_IntersectionDifficultyThreshold);
    DISPLAY_PARAM(m_RegexAutomata_FailedAutomatonThreshold);
    DISPLAY_PARAM(m_RegexAutomata_FailedIntersectionThreshold);
    DISPLAY_PARAM(m_RegexAutomata_LengthAttemptThreshold);
    DISPLAY_PARAM(m_FixedLengthNaiveCounterexamples);
}
