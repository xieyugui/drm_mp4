/*
 * mp4_transform_context.h
 *
 *  Created on: 2016年1月8日
 *      Author: xie
 */

#ifndef _MP4_TRANSFORM_CONTEXT_H_
#define _MP4_TRANSFORM_CONTEXT_H_

#include <string.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>

#include <ts/ts.h>
#include <ts/experimental.h>
#include <ts/remap.h>

#include "mp4_meta.h"

class IOHandle
{
public:
  IOHandle() : vio(NULL), buffer(NULL), reader(NULL){};

  ~IOHandle()
  {
    if (reader) {
      TSIOBufferReaderFree(reader);
      reader = NULL;
    }

    if (buffer) {
      TSIOBufferDestroy(buffer);
      buffer = NULL;
    }
  }

public:
  TSVIO vio;
  TSIOBuffer buffer;
  TSIOBufferReader reader;
};

class Mp4TransformContext
{
public:
  Mp4TransformContext(int64_t offset, int64_t end, int64_t cl, u_char *des_key, bool is_n_md)
    : total(0), tail(0), end_tail(0),pos(0), content_length(0), meta_length(0), parse_over(false), raw_transform(false)
  {
    res_buffer = TSIOBufferCreate();
    res_reader = TSIOBufferReaderAlloc(res_buffer);
    dup_reader = TSIOBufferReaderAlloc(res_buffer);

    mm.start = offset; //起始长度
    mm.end = end;
    mm.cl = cl; //文件总长度
    mm.tdes_key = des_key;
    mm.is_need_md = is_n_md;
  }

  ~Mp4TransformContext()
  {
    if (res_reader) {
      TSIOBufferReaderFree(res_reader);
    }

    if (dup_reader) {
      TSIOBufferReaderFree(dup_reader);
    }

    if (res_buffer) {
      TSIOBufferDestroy(res_buffer);
    }
  }

  int mp4_parse_meta(bool body_complete);
  int copy_drm_or_origin_data(bool *write_down,int64_t *toread);
private:
  int ignore_useless_part();
  //拷贝drm 数据
  int copy_drm_data(bool *write_down);
  int copy_video_and_audio_data(bool *write_down,int64_t *toread);
  //丢弃end 之后的数据
  int discard_after_end_data();
  //拷贝有价值的数据
  int copy_valuable_data(bool *write_down);

public:
  IOHandle output;
  Mp4Meta mm; //meta data
  int64_t total;
  int64_t tail;
  int64_t end_tail;
  int64_t pos;
  int64_t content_length;
  int64_t meta_length;

  TSIOBuffer res_buffer;
  TSIOBufferReader res_reader;
  TSIOBufferReader dup_reader;

  bool parse_over; //是否解析过
  bool raw_transform;
};


#endif /* _MP4_TRANSFORM_CONTEXT_H_ */
