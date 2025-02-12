#include "bp_double_saturating_counter.h"

bp_double_saturating_counter_t::bp_double_saturating_counter_t(int num_entries) {
  // allocate and initialize the counters
  this->counters = std::vector<branch_prediction_t>(num_entries, WEAK_NOT_TAKEN);
  this->num_entries = num_entries;
}

void bp_double_saturating_counter_t::reset() {
  // reset the counters
  std::fill(counters.begin(), counters.end(), WEAK_NOT_TAKEN);
}

bool bp_double_saturating_counter_t::predict(reg_t pc, bool taken) {
  // get the index of the counter for this pc
  int index = (pc >> 1) % num_entries;
  // get the current counter value
  enum branch_prediction_t counter_value = counters[index];
  // update the counter
  if (taken) {
    counters[index] = _increment(counter_value);
  } else {
    counters[index] = _decrement(counter_value);
  }
  // return whether the prediction hit or miss
  return is_taken(counter_value) == taken;
}

branch_prediction_t bp_double_saturating_counter_t::peek(reg_t pc) {
  // get the index of the counter for this pc
  int index = (pc >> 1) % num_entries;
  // return the current counter value
  return counters[index];
}

const branch_prediction_t bp_double_saturating_counter_t::increment_table[] = {
  WEAK_NOT_TAKEN,  // From STRONG_NOT_TAKEN
  WEAK_TAKEN,      // From WEAK_NOT_TAKEN
  STRONG_TAKEN,    // From WEAK_TAKEN
  STRONG_TAKEN     // From STRONG_TAKEN
};

enum branch_prediction_t bp_double_saturating_counter_t::_increment(enum branch_prediction_t counter_value) {
  return increment_table[counter_value];
}

const branch_prediction_t bp_double_saturating_counter_t::decrement_table[] = {
  STRONG_NOT_TAKEN, // From STRONG_NOT_TAKEN
  STRONG_NOT_TAKEN, // From WEAK_NOT_TAKEN
  WEAK_NOT_TAKEN,   // From WEAK_TAKEN
  WEAK_TAKEN        // From STRONG_TAKEN
};

enum branch_prediction_t bp_double_saturating_counter_t::_decrement(enum branch_prediction_t counter_value) {
  return decrement_table[counter_value];
}

bool bp_double_saturating_counter_t::is_taken(enum branch_prediction_t counter_value) {
  return counter_value == WEAK_TAKEN || counter_value == STRONG_TAKEN;
}
