/*
 * mp4_context.h
 *
 *  Created on: 2016年1月8日
 *      Author: xie
 */

#ifndef _MP4_CONTEXT_H_
#define _MP4_CONTEXT_H_

#include <string.h>
#include <inttypes.h>

#include "mp4_transform_context.h"

class Mp4Context
{
public:
  Mp4Context(int64_t s, int64_t e) : start(s),end(e), cl(0), mtc(NULL), transform_added(false){};

  ~Mp4Context()
  {
    if (mtc) {
      delete mtc;
      mtc = NULL;
    }
  }

public:
  int64_t start;
  int64_t end;
  int64_t cl;
  Mp4TransformContext *mtc;

  bool transform_added;
};


#endif /* _MP4_CONTEXT_H_ */
