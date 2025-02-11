#ifndef _BRANCH_PREDICTOR_H
#define _BRANCH_PREDICTOR_H

#include "decode.h"

class branch_predictor_t {
public:
  virtual ~branch_predictor_t() = default;
  virtual void reset() = 0;
  virtual bool predict(reg_t pc, bool taken) = 0;
};

#endif
