/*
  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#ifndef _MP4_META_H
#define _MP4_META_H

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <unistd.h>
#include <getopt.h>
#include <inttypes.h>

#include <ts/ts.h>

#include "des.h"

#define DEBUG_TAG "drm_mp4"
#define PLUGIN_NAME "drm_mp4"

#define MP4_MAX_TRAK_NUM 6
#define MP4_MAX_BUFFER_SIZE (15 * 1024 * 1024) //15M  稍微改大一点
#define MP4_MIN_BUFFER_SIZE 1024 //1K

#define MP4_NEED_DES_LENGTH 8176
#define MP4_DES_ADD_LENGTH 8
#define MP4_DES_LENGTH (MP4_NEED_DES_LENGTH + MP4_DES_ADD_LENGTH)
#define MP4_DES_MAX_COUNT 5

typedef enum { VIDEO_VERSION_1 = 1, VIDEO_VERSION_3 = 3, VIDEO_VERSION_4  = 4 } video_version;
typedef enum { FLV_VIDEO, VIDEO_PCF , VIDEO_PCM  } VideoType;

#define mp4_set_atom_name(p, n1, n2, n3, n4) \
  ((u_char *)(p))[4] = n1;                   \
  ((u_char *)(p))[5] = n2;                   \
  ((u_char *)(p))[6] = n3;                   \
  ((u_char *)(p))[7] = n4

#define mp4_get_32value(p) \
  (((uint32_t)((u_char *)(p))[0] << 24) + (((u_char *)(p))[1] << 16) + (((u_char *)(p))[2] << 8) + (((u_char *)(p))[3]))

#define mp4_set_32value(p, n)               \
  ((u_char *)(p))[0] = (u_char)((n) >> 24); \
  ((u_char *)(p))[1] = (u_char)((n) >> 16); \
  ((u_char *)(p))[2] = (u_char)((n) >> 8);  \
  ((u_char *)(p))[3] = (u_char)(n)

#define mp4_get_64value(p)                                                                                              \
  (((uint64_t)((u_char *)(p))[0] << 56) + ((uint64_t)((u_char *)(p))[1] << 48) + ((uint64_t)((u_char *)(p))[2] << 40) + \
   ((uint64_t)((u_char *)(p))[3] << 32) + ((uint64_t)((u_char *)(p))[4] << 24) + (((u_char *)(p))[5] << 16) +           \
   (((u_char *)(p))[6] << 8) + (((u_char *)(p))[7]))

#define mp4_set_64value(p, n)                         \
  ((u_char *)(p))[0] = (u_char)((uint64_t)(n) >> 56); \
  ((u_char *)(p))[1] = (u_char)((uint64_t)(n) >> 48); \
  ((u_char *)(p))[2] = (u_char)((uint64_t)(n) >> 40); \
  ((u_char *)(p))[3] = (u_char)((uint64_t)(n) >> 32); \
  ((u_char *)(p))[4] = (u_char)((n) >> 24);           \
  ((u_char *)(p))[5] = (u_char)((n) >> 16);           \
  ((u_char *)(p))[6] = (u_char)((n) >> 8);            \
  ((u_char *)(p))[7] = (u_char)(n)


typedef struct _drm_header {
	uint8_t signature[3]; //标志信息
	uint32_t version; //版本
	uint32_t videoid_size; //videoid 长度
} drm_header;


typedef enum {
  MP4_TRAK_ATOM = 0,//track box trak  “trak”也是一个container box，存放视频音频流的容器。其子box包含了该track的媒体数据引用和描述（hint track除外）
  //Track Header Box（tkhd）
  /**
   * 字段 字节数 意义
   * box size 4 box大小
   * box type 4 box类型
   * version 1  box版本，0或1，一般为0。（以下字节数均按version=0
   * flags 3  按位或操作结果值，预定义如下：
					0x000001 track_enabled，否则该track不被播放；
					0x000002 track_in_movie，表示该track在播放中被引用；
					0x000004 track_in_preview，表示该track在预览时被引用。
					一般该值为7，如果一个媒体所有track均未设置track_in_movie和track_in_preview，将被理解为所有track均设置了这两项；对于hint track，该值为0
   * creation time 4  创建时间（相对于UTC时间1904-01-01零点的秒数）
   * modification time 4  修改时间
   * track id 4  id号，不能重复且不能为0
   * reserved 4  保留位
   * duration 4 track的时间长度
   * reserved 8 保留位
   * layer  2  视频层，默认为0，值小的在上层
   * alternate group 2  track分组信息，默认为0表示该track未与其他track有群组关系
   * volume 2  [8.8] 格式，如果为音频track，1.0（0x0100）表示最大音量；否则为0
   * reserved 2 保留位
   * matrix 36  视频变换矩阵
   * width 4 宽
   * height 4  高，均为 [16.16] 格式值，与sample描述中的实际画面大小比值，用于播放时的展示宽高
   */
  MP4_TKHD_ATOM, //Track Header Box（tkhd）
  //Media Box（mdia）
  /**
   *“mdia”定义了track媒体类型以及sample数据，描述sample信息。一般“mdia”包含一个“mdhd”，
		一个“hdlr”和一个“minf”，其中“mdhd”为media header box，“hdlr”为handler reference box，
		“minf”为media information box。
   */
  MP4_MDIA_ATOM, //Media Box（mdia）
  //Media Header Box（mdhd）
  /**  定义了 timescale ，trak 需要通过 timescale 换算成真实时间
   * 字段 字节数 意义
   * box size 4 box大小
   * box type 4 box类型
   * version 1  box版本，0或1，一般为0。（以下字节数均按version=0
   * flags 3
   * creation time 4  创建时间（相对于UTC时间1904-01-01零点的秒数）
   * modification time 4  修改时间
   * time scale  4 文件媒体在1秒时间内的刻度值，可以理解为1秒长度的时间单元数
   * duration 4 track的时间长度
   * language 2 媒体语言码。最高位为0，后面15位为3个字符（见ISO 639-2/T标准中定义）
   * pre-defined 2
   *
   */
  MP4_MDHD_ATOM, //Media Header Box（mdhd）
  //Handler Reference Box
  /**  “hdlr”解释了媒体的播放过程信息，该box也可以被包含在meta box（meta）中
   * 字段 字节数 意义
   * box size 4 box大小
   * box type 4 box类型
   * version 1  box版本，0或1，一般为0。（以下字节数均按version=0
   * flags 3
   * pre-defined 4
   * handler type 4  在media box中，该值为4个字符： “vide”— video track “soun”— audio track “hint”— hint track
   * reserved 12
   * name 不定  track type name，以‘\0’结尾的字符串
   */
  MP4_HDLR_ATOM,//Handler Reference Box
  //Media Information Box
  /**
   * “minf”存储了解释track媒体数据的handler-specific信息，media handler用这些信息将媒体时间映射到媒体
      数据并进行处理。“minf”中的信息格式和内容与媒体类型以及解释媒体数据的media handler密切相关，其
      他media handler不知道如何解释这些信息。“minf”是一个container box，其实际内容由子box说明。

   * 一般情况下，“minf”包含一个header box，一个“dinf”和一个“stbl”，其中，header box根据track type
     （即media handler type）分为“vmhd”、“smhd”、“hmhd”和“nmhd”，“dinf”为data information box，“stbl”为sample table box。
   *
   */
  MP4_MINF_ATOM, //Media Information Box
  //Media Information Header Box（vmhd、smhd、hmhd、nmhd） ---Video Media Header Box
  /**
   * box size 4 box大小
   * box type 4 box类型
   * version 1  box版本，0或1，一般为0。（以下字节数均按version=0
   * flags 3
   * graphics mode 4  视频合成模式，为0时拷贝原始图像，否则与opcolor进行合成
   * opcolor 2 * 3  red，green，blue｝
   */
  MP4_VMHD_ATOM, //Video Media Header Box
  //Sound Media Header Box
  /**
   * box size 4 box大小
   * box type 4 box类型
   * version 1  box版本，0或1，一般为0。（以下字节数均按version=0
   * flags 3
   * balance 2  立体声平衡，[8.8] 格式值，一般为0，-1.0表示全部左声道，1.0表示全部右声道
   * reserved 2
   */
  MP4_SMHD_ATOM, //Sound Media Header Box
  //Data Information Box
  /**
   * “dinf”解释如何定位媒体信息，是一个container box。“dinf”一般包含一个“dref”，即data reference box；
     “dref”下会包含若干个“url”或“urn”，这些box组成一个表，用来定位track数据。简单的说，track可以被分成若干段，
      每一段都可以根据“url”或“urn”指向的地址来获取数据，sample描述中会用这些片段的序号将这些片段组成一个完整的track。
      一般情况下，当数据被完全包含在文件中时，“url”或“urn”中的定位字符串是空的。
   * box size 4 box大小
   * box type 4 box类型
   * version 1  box版本，0或1，一般为0。（以下字节数均按version=0
   * flags 3
   * entry count 4 “url”或“urn”表的元素个数
   * “url”或“urn”列表 不定
           “url”或“urn”都是box，“url”的内容为字符串（location string），“urn”的内容为一对字符串（name string and location string）。
            当“url”或“urn”的box flag为1时，字符串均为空。
   *
   */
  MP4_DINF_ATOM, //Data Information Box
  //Sample Table Box
  /**
   * “stbl”包含了关于track中sample所有时间和位置的信息，以及sample的编解码等信息。利用这个表，可以解释sample的时序、
      类型、大小以及在各自存储容器中的位置。“stbl”是一个container box，其子box包括：sample description box（stsd）、
      time to sample box（stts）、sample size box（stsz或stz2）、sample to chunk box（stsc）、chunk offset box（stco或co64）
      、composition time to sample box（ctts）、sync sample box（stss)
   */
  MP4_STBL_ATOM, //Sample Table Box
  /**
   * box header和version字段后会有一个entry count字段，根据entry的个数，每个entry会有type信息，如“vide”、“sund”等，
       根据type不同sample description会提供不同的信息，例如对于video track，会有“VisualSampleEntry”类型信息，
       对于audio track会有“AudioSampleEntry”类型信息。
     视频的编码类型、宽高、长度，音频的声道、采样等信息都会出现在这个box中。
   */
  MP4_STSD_ATOM, //Sample Description Box
  //Time To Sample Box
  /**
   *  “stts”存储了sample的duration，描述了sample时序的映射方法，我们通过它可以找到任何时间的sample。
       “stts”可以包含一个压缩的表来映射时间和sample序号，用其他的表来提供每个sample的长度和指针。
       表中每个条目提供了在同一个时间偏移量里面连续的sample序号，以及samples的偏移量。递增这些偏移量，
       就可以建立一个完整的time to sample表。
   */
  MP4_STTS_ATOM, //Time To Sample Box
  MP4_STTS_DATA,
  //Sync Sample Box
  /**
   * “stss”确定media中的关键帧。对于压缩媒体数据，关键帧是一系列压缩序列的开始帧，其解压缩时不依赖以前的帧，
      而后续帧的解压缩将依赖于这个关键帧。“stss”可以非常紧凑的标记媒体内的随机存取点，它包含一个sample序号表，
      表内的每一项严格按照sample的序号排列，说明了媒体中的哪一个sample是关键帧。如果此表不存在，
      说明每一个sample都是一个关键帧，是一个随机存取点。
   */
  MP4_STSS_ATOM, //Sync Sample Box
  MP4_STSS_DATA,

  MP4_CTTS_ATOM,
  MP4_CTTS_DATA,
  /**
   * 用chunk组织sample可以方便优化数据获取，一个thunk包含一个或多个sample。“stsc”中用一个表描述了sample
      与chunk的映射关系，查看这张表就可以找到包含指定sample的thunk，从而找到这个sample。
   */
  MP4_STSC_ATOM,//Sample To Chunk Box
  MP4_STSC_CHUNK,
  MP4_STSC_DATA,
  /**
   *“stsz” 定义了每个sample的大小，包含了媒体中全部sample的数目和一张给出每个sample大小的表。
   *“stsz” 这个box相对来说体积是比较大的。
   */
  MP4_STSZ_ATOM, //Sample Size Box
  MP4_STSZ_DATA,
  /**
   * “stco”定义了每个thunk在媒体流中的位置。位置有两种可能，32位的和64位的，后者对非常大的电影很有用。
   * 在一个表中只会有一种可能，这个位置是在整个文件中的，而不是在任何box中的，这样做就可以直接在文件中找到媒体数据，
   * 而不用解释box。需要注意的是一旦前面的box有了任何改变，这张表都要重新建立，因为位置信息已经改变了。
   */
  MP4_STCO_ATOM, //Chunk Offset Box
  MP4_STCO_DATA,
  MP4_CO64_ATOM, //64-bit chunk offset
  MP4_CO64_DATA,
  MP4_LAST_ATOM = MP4_CO64_DATA
} TSMp4AtomID;


typedef struct {
  u_char size[4];
  u_char name[4];
} mp4_atom_header;

typedef struct {
  u_char size[4];
  u_char name[4];
  u_char size64[8];
} mp4_atom_header64;

typedef struct {
  u_char size[4]; //box大小
  u_char name[4]; //box类型
  u_char version[1]; //box版本
  u_char flags[3];
  u_char creation_time[4]; //创建时间
  u_char modification_time[4]; //修改时间
  u_char timescale[4]; //文件媒体在1秒时间内的刻度值，可以理解为1秒长度的时间单元数
  u_char duration[4]; //该track的时间长度，用duration和time scale值可以计算track时长，比如audio track的time scale = 8000, duration = 560128，时长为70.016，video track的time scale = 600, duration = 42000，时长为70
  u_char rate[4];//推荐播放速率，高16位和低16位分别为小数点整数部分和小数部分，即[16.16] 格式，该值为1.0（0x00010000）表示正常前向播放
  u_char volume[2]; //[8.8] 格式，如果为音频track，1.0（0x0100）表示最大音量；否则为0
  u_char reserved[10]; //保留位
  u_char matrix[36]; //视频变换矩阵
  u_char preview_time[4];
  u_char preview_duration[4];
  u_char poster_time[4];
  u_char selection_time[4];
  u_char selection_duration[4];
  u_char current_time[4];
  u_char next_track_id[4]; //下一个track使用的id号
} mp4_mvhd_atom;//Movie Header Box

typedef struct {
  u_char size[4];
  u_char name[4];
  u_char version[1];
  u_char flags[3];
  u_char creation_time[8];
  u_char modification_time[8];
  u_char timescale[4];
  u_char duration[8];
  u_char rate[4];
  u_char volume[2];
  u_char reserved[10];
  u_char matrix[36];
  u_char preview_time[4];
  u_char preview_duration[4];
  u_char poster_time[4];
  u_char selection_time[4];
  u_char selection_duration[4];
  u_char current_time[4];
  u_char next_track_id[4];
} mp4_mvhd64_atom;//大文件的时候

typedef struct {
  u_char size[4];
  u_char name[4];
  u_char version[1];
  u_char flags[3];
  u_char creation_time[4];
  u_char modification_time[4];
  u_char track_id[4];
  u_char reserved1[4];//保留位
  u_char duration[4];//track的时间长度
  u_char reserved2[8];//保留位
  u_char layer[2]; //视频层，默认为0，值小的在上层
  u_char group[2];
  u_char volume[2];//[8.8] 格式，如果为音频track，1.0（0x0100）表示最大音量；否则为0
  u_char reverved3[2];
  u_char matrix[36];
  u_char width[4];
  u_char heigth[4];
} mp4_tkhd_atom;//Track Header Box

typedef struct {
  u_char size[4];
  u_char name[4];
  u_char version[1];
  u_char flags[3];
  u_char creation_time[8];
  u_char modification_time[8];
  u_char track_id[4];
  u_char reserved1[4];
  u_char duration[8];
  u_char reserved2[8];
  u_char layer[2];
  u_char group[2];
  u_char volume[2];
  u_char reverved3[2];
  u_char matrix[36];
  u_char width[4];
  u_char heigth[4];
} mp4_tkhd64_atom;

typedef struct {
  u_char size[4];
  u_char name[4];
  u_char version[1];
  u_char flags[3];
  u_char creation_time[4];
  u_char modification_time[4];
  u_char timescale[4];
  u_char duration[4];
  u_char language[2];
  u_char quality[2];
} mp4_mdhd_atom;//Media Header Box

typedef struct {
  u_char size[4];
  u_char name[4];
  u_char version[1];
  u_char flags[3];
  u_char creation_time[8];
  u_char modification_time[8];
  u_char timescale[4];
  u_char duration[8];
  u_char language[2];
  u_char quality[2];
} mp4_mdhd64_atom;

typedef struct {
  u_char size[4];
  u_char name[4];
  u_char version[1];
  u_char flags[3];
  u_char entries[4];

  u_char media_size[4];
  u_char media_name[4];
} mp4_stsd_atom;//Sample Description Box  视频的编码类型、宽高、长度，音频的声道、采样等信息都会出现在这个box中。

typedef struct {
  u_char size[4];
  u_char name[4];
  u_char version[1];
  u_char flags[3];
  u_char entries[4];
} mp4_stts_atom;//Time To Sample Box   “stts”存储了sample的duration，描述了sample时序的映射方法，我们通过它可 以找到任何时间的sample。

typedef struct {
  u_char count[4];
  u_char duration[4];
} mp4_stts_entry;

typedef struct {
  u_char size[4];
  u_char name[4];
  u_char version[1];
  u_char flags[3];
  u_char entries[4];
} mp4_stss_atom;//Sync Sample Box  “stss”确定media中的关键帧

typedef struct {
  u_char size[4];
  u_char name[4];
  u_char version[1];
  u_char flags[3];
  u_char entries[4];
} mp4_ctts_atom;

typedef struct {
  u_char count[4];
  u_char offset[4];
} mp4_ctts_entry;

typedef struct {
  u_char size[4];
  u_char name[4];
  u_char version[1];
  u_char flags[3];
  u_char entries[4];
} mp4_stsc_atom;//Sample To Chunk Box 用chunk组织sample可以方便优化数据获取，一
//个thunk包含一个或多个sample。“stsc”中用一个表描述了sample与chunk的映射关系，查看这张表就可以找到包含指定sample的thunk，从而找到这个sample。

typedef struct {
  u_char chunk[4];
  u_char samples[4];
  u_char id[4];
} mp4_stsc_entry;

typedef struct {
  u_char size[4];
  u_char name[4];
  u_char version[1];
  u_char flags[3];
  u_char uniform_size[4];
  u_char entries[4];
} mp4_stsz_atom;//Sample Size Box  “stsz” 定义了每个sample的大小，包含了媒体中全部sample的数目和一张给出每个sample大小的表。

typedef struct {
  u_char size[4];
  u_char name[4];
  u_char version[1];
  u_char flags[3];
  u_char entries[4];
} mp4_stco_atom;//Chunk Offset Box  “stco”定义了每个thunk在媒体流中的位置。位置有两种可能，32位的和64位的

typedef struct {
  u_char size[4];
  u_char name[4];
  u_char version[1];
  u_char flags[3];
  u_char entries[4];
} mp4_co64_atom;

class Mp4Meta;
typedef int (Mp4Meta::*Mp4AtomHandler)(int64_t atom_header_size, int64_t atom_data_size);

typedef int (Mp4Meta::*MPHandler) ();

typedef struct {
  const char *name;
  Mp4AtomHandler handler;
} mp4_atom_handler;


class BufferHandle
{
public:
  BufferHandle() : buffer(NULL), reader(NULL){};

  ~BufferHandle()
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
  TSIOBuffer buffer;
  TSIOBufferReader reader;
};

class Mp4Trak
{
public:
  Mp4Trak()
    : timescale(0), duration(0), time_to_sample_entries(0), sample_to_chunk_entries(0), sync_samples_entries(0),
      composition_offset_entries(0), sample_sizes_entries(0), chunks(0), start_sample(0), start_chunk(0), start_chunk_size(0),start_chunk_samples(0),
	  last_start_chunk_samples(0),
	  chunk_samples(0),chunk_samples_size(0), start_offset(0), tkhd_size(0), mdhd_size(0), hdlr_size(0), vmhd_size(0), smhd_size(0), dinf_size(0),
      size(0)
  {
    memset(&stsc_chunk_entry, 0, sizeof(mp4_stsc_entry));
  }

  ~Mp4Trak() {}

public:
  uint32_t timescale; //文件媒体在1秒时间内的刻度值，可以理解为1秒长度的时间单元数
  int64_t duration; //时长

  uint32_t time_to_sample_entries;     // stsc
  uint32_t sample_to_chunk_entries;    // stsc
  uint32_t sync_samples_entries;       // stss
  uint32_t composition_offset_entries; // ctts
  uint32_t sample_sizes_entries;       // stsz
  uint32_t chunks;                     // stco, co64

  uint32_t start_sample;
  uint32_t start_chunk;
  uint64_t start_chunk_size;
  uint32_t start_chunk_samples;
  uint32_t last_start_chunk_samples;//查找最接近的chunk 的sample个数
  uint32_t chunk_samples;
  uint64_t chunk_samples_size;
  off_t start_offset;

  size_t tkhd_size; //track header box size
  size_t mdhd_size; //media header box size
  size_t hdlr_size; // handler reference box size
  size_t vmhd_size; // video media header box size
  size_t smhd_size; //sound media header box size
  size_t dinf_size; // data information box size
  size_t size;

  BufferHandle atoms[MP4_LAST_ATOM + 1];

  mp4_stsc_entry stsc_chunk_entry;
};

class Mp4Meta
{
public:
  Mp4Meta()
    : start(0),end(0), cl(0), content_length(0), meta_atom_size(0), meta_avail(0), wait_next(0), need_size(0), rs(0),is_rs_find(false),rate(0),
      ftyp_size(0), moov_size(0), start_pos(0), end_pos(0) , timescale(0), trak_num(0), passed(0), meta_complete(false), tdes_key(NULL),
	  version(0),videoid_size(0),videoid(NULL), userid_size(0), userid(NULL),range_size(0),range_start(0),range_end(0),
	  original_file_size(0),section_size(0),section_count(0), old_section_count(0),section_length_arr(NULL),reserved_size(0), reserved(NULL),
	  drm_head_length(0),tag_pos(0),complete_parse_drm_header(false),duration_pos(0),drm_length(0),is_des_body(false),is_need_md(false),
	  small_des_add_length(0)
  {
    memset(trak_vec, 0, sizeof(trak_vec));

	drm_buffer = TSIOBufferCreate();
	drm_reader = TSIOBufferReaderAlloc(drm_buffer);
	des_buffer = TSIOBufferCreate();
	des_reader = TSIOBufferReaderAlloc(des_buffer);

    meta_buffer = TSIOBufferCreate();
    meta_reader = TSIOBufferReaderAlloc(meta_buffer);

    current_handler = &Mp4Meta::process_drm_header;
  }

  ~Mp4Meta()
  {
    uint32_t i;

    for (i = 0; i < trak_num; i++)
      delete trak_vec[i];

    if (drm_reader) {
        TSIOBufferReaderFree(drm_reader);
        drm_reader = NULL;
    }

    if (drm_buffer) {
        TSIOBufferDestroy(drm_buffer);
        drm_buffer = NULL;
    }

    if (des_reader) {
        TSIOBufferReaderFree(des_reader);
        des_reader = NULL;
    }
	if (des_buffer) {
		TSIOBufferDestroy(des_buffer);
		des_buffer = NULL;
	}

    if (meta_reader) {
      TSIOBufferReaderFree(meta_reader);
      meta_reader = NULL;
    }

    if (meta_buffer) {
      TSIOBufferDestroy(meta_buffer);
      meta_buffer = NULL;
    }

    if (videoid) {
    		TSfree(videoid);
    		videoid = NULL;
    }

    if (userid) {
    		TSfree(userid);
    		userid = NULL;
    }

    if(section_length_arr) {
    		TSfree(section_length_arr);
    		section_length_arr = NULL;
    }

    if (reserved) {
    		TSfree(reserved);
    		reserved = NULL;
    }

    tdes_key = NULL;
  }

  size_t get_drm_header_size();
  int read_drm_header(TSIOBufferReader readerp, drm_header * header);

  int process_drm_header();
  int process_drm_header_videoid();
  int process_drm_header_userid();
  int process_drm_header_range();
  int process_drm_header_sections();
  int process_drm_header_reserved();
  int process_decrypt_mp4_body();//根据头信息进行解密
  int process_encrypt_mp4_body();

  int parse_meta(bool body_complete);

  int post_process_meta();
  void mp4_meta_consume(int64_t size);
  int mp4_atom_next(int64_t atom_size, bool wait = false);

  int mp4_read_atom(mp4_atom_handler *atom, int64_t size);
  int parse_root_atoms();

  int mp4_read_ftyp_atom(int64_t header_size, int64_t data_size);
  int mp4_read_moov_atom(int64_t header_size, int64_t data_size);
  int mp4_read_mdat_atom(int64_t header_size, int64_t data_size);

  int mp4_read_mvhd_atom(int64_t header_size, int64_t data_size);
  int mp4_read_trak_atom(int64_t header_size, int64_t data_size);
  int mp4_read_cmov_atom(int64_t header_size, int64_t data_size);

  int mp4_read_tkhd_atom(int64_t header_size, int64_t data_size);
  int mp4_read_mdia_atom(int64_t header_size, int64_t data_size);

  int mp4_read_mdhd_atom(int64_t header_size, int64_t data_size);
  int mp4_read_hdlr_atom(int64_t header_size, int64_t data_size);
  int mp4_read_minf_atom(int64_t header_size, int64_t data_size);

  int mp4_read_vmhd_atom(int64_t header_size, int64_t data_size);
  int mp4_read_smhd_atom(int64_t header_size, int64_t data_size);
  int mp4_read_dinf_atom(int64_t header_size, int64_t data_size);
  int mp4_read_stbl_atom(int64_t header_size, int64_t data_size);

  int mp4_read_stsd_atom(int64_t header_size, int64_t data_size);
  int mp4_read_stts_atom(int64_t header_size, int64_t data_size);
  int mp4_read_stss_atom(int64_t header_size, int64_t data_size);
  int mp4_read_ctts_atom(int64_t header_size, int64_t data_size);
  int mp4_read_stsc_atom(int64_t header_size, int64_t data_size);
  int mp4_read_stsz_atom(int64_t header_size, int64_t data_size);
  int mp4_read_stco_atom(int64_t header_size, int64_t data_size);
  int mp4_read_co64_atom(int64_t header_size, int64_t data_size);

  int mp4_update_stts_atom(Mp4Trak *trak);
  int mp4_update_stss_atom(Mp4Trak *trak);
  int mp4_update_ctts_atom(Mp4Trak *trak);
  int mp4_update_stsc_atom(Mp4Trak *trak);
  int mp4_update_stsz_atom(Mp4Trak *trak);
  int mp4_update_co64_atom(Mp4Trak *trak);
  int mp4_update_stco_atom(Mp4Trak *trak);
  int mp4_update_stbl_atom(Mp4Trak *trak);
  int mp4_update_minf_atom(Mp4Trak *trak);
  int mp4_update_mdia_atom(Mp4Trak *trak);
  int mp4_update_trak_atom(Mp4Trak *trak);

  int mp4_get_start_sample(Mp4Trak *trak);
  int mp4_get_start_chunk_offset_co64(Mp4Trak *trak);
  int mp4_get_start_chunk_offset_stco(Mp4Trak *trak);

  int64_t mp4_update_mdat_atom(int64_t start_offset);
  int mp4_adjust_co64_atom(Mp4Trak *trak, off_t adjustment);
  int mp4_adjust_stco_atom(Mp4Trak *trak, int32_t adjustment);

  uint32_t mp4_find_key_sample(uint32_t start_sample, Mp4Trak *trak);
  void mp4_update_mvhd_duration();
  void mp4_update_tkhd_duration(Mp4Trak *trak);
  void mp4_update_mdhd_duration(Mp4Trak *trak);

  int change_drm_header(off_t start_offset, off_t adjustment);

  void get_des_videoid(u_char *des_videoid, uint32_t *d_v_length);
//  void get_des_null(u_char *des_null, uint32_t *d_n_length);

public:
  int64_t start;
  int64_t end;
  int64_t cl;             // the total size of the mp4 file
  int64_t content_length; // the size of the new mp4 file
  int64_t meta_atom_size;

  TSIOBuffer drm_buffer;
  TSIOBufferReader drm_reader;

  TSIOBuffer des_buffer;
  TSIOBufferReader des_reader;

  TSIOBuffer meta_buffer; // meta data to be parsed
  TSIOBufferReader meta_reader;

  MPHandler current_handler;

  int64_t meta_avail;
  int64_t wait_next;
  int64_t need_size;

  BufferHandle meta_atom;
  BufferHandle ftyp_atom;
  BufferHandle moov_atom;
  BufferHandle mvhd_atom;
  BufferHandle mdat_atom; //流媒体数据结构
  BufferHandle mdat_data; //流媒体数据
  BufferHandle out_handle;

  Mp4Trak *trak_vec[MP4_MAX_TRAK_NUM];

  double rs;
  bool is_rs_find;
  double rate;

  int64_t ftyp_size;
  int64_t moov_size;
  int64_t start_pos; // start position of the new mp4 file
  int64_t end_pos;
  uint32_t timescale;
  uint32_t trak_num;
  int64_t passed;

  u_char mdat_atom_header[16];
  bool meta_complete;
  u_char *tdes_key; //des key

  //----DRM header start
  //char signature[3]; 标志位
  uint32_t version;  //4

  uint32_t videoid_size;  //4 tail_length
  u_char *videoid; //视频 id 标签

  uint32_t userid_size; //4
  u_char *userid;  //用户 id 标签

  uint32_t range_size;
  uint64_t range_start;//同访问 mp4 时 http response header 的 Content-Range 字段的 start
  uint64_t range_end;//同访问 mp4 时 http response header 的 Content-Range 字段的 end
  uint64_t original_file_size;//mp4 文件字节数

  uint32_t section_size;//加密片段标签
  uint32_t section_count; // count > 0 && count <= 5
  uint32_t old_section_count;
  u_char *section_length_arr;// length > 0 && length <= 8184  enc 8176| dec 8184
  //若请求 mp4 文件的 Range 小于 5*8176
  //sections tag 的 count 字段 = range/8176 + 1;

  uint32_t reserved_size; //4
  u_char *reserved;
  int64_t drm_head_length;  //drm head 的长度
  int64_t tag_pos; //已经消费了多少字节
  bool complete_parse_drm_header;
  int64_t duration_pos;//MP4 从头开始计数，为了方便计算该丢弃多少数据
  int64_t drm_length;
  bool is_des_body;//是否已经加密过
  bool is_need_md;
  uint32_t small_des_add_length; //当des加密小于key 的长度的时候，加密出来就是key 的长度
};

#endif
