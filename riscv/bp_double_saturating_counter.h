#ifndef _BP_DSC_H
#define _BP_DSC_H

#include "branch_predictor.h"
#include <vector>

enum branch_prediction_t {
  STRONG_NOT_TAKEN = 0,
  WEAK_NOT_TAKEN = 1,
  WEAK_TAKEN = 2,
  STRONG_TAKEN = 3
};



class bp_double_saturating_counter_t : public branch_predictor_t {
public:
  bp_double_saturating_counter_t(int num_entries);
  void reset();
  bool predict(reg_t pc, bool taken);

private:
  int num_entries;
  std::vector<branch_prediction_t> counters;
  enum branch_prediction_t _increment(enum branch_prediction_t counter_value);
  enum branch_prediction_t _decrement(enum branch_prediction_t counter_value);
  bool is_taken(enum branch_prediction_t counter_value);
  static const branch_prediction_t increment_table[];
  static const branch_prediction_t decrement_table[];
};

#endif