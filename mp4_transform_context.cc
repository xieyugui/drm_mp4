/*
 * mp4_transform_context.cc
 *
 *  Created on: 2016年1月8日
 *      Author: xie
 */

#include "mp4_transform_context.h"

int Mp4TransformContext::mp4_parse_meta(bool body_complete) {
	int ret;
	int64_t avail, bytes;
	TSIOBufferBlock blk;
	const char *data;

	avail = TSIOBufferReaderAvail(this->dup_reader);
	blk = TSIOBufferReaderStart(this->dup_reader);

	while (blk != NULL) { //将数据全部拷贝到meta_buffer中去
		data = TSIOBufferBlockReadStart(blk, this->dup_reader, &bytes);
		if (bytes > 0) {
			TSIOBufferWrite(this->mm.meta_buffer, data, bytes);
		}

		blk = TSIOBufferBlockNext(blk);
	}

	TSIOBufferReaderConsume(this->dup_reader, avail);
	ret = this->mm.parse_meta(body_complete); //body_complete 是否传输完成
	TSDebug(PLUGIN_NAME, "mp4_parse_meta ret = %d", ret);
	if (ret > 0) { // meta success
		this->tail = this->mm.start_pos; //start position of the new mp4 file
		this->end_tail = this->mm.end_pos;
		this->content_length = this->mm.content_length; //-183; //the size of the new mp4 file
		TSDebug(PLUGIN_NAME, "mp4_parse_meta  des_reader  tail= %ld end_tail=%ld content_length=%ld", this->tail,this->end_tail,this->content_length);
	}

	if (ret != 0) { //如果最后有结果了，不管成功还是失败 都销毁dup_reader
		TSIOBufferReaderFree(this->dup_reader);
		this->dup_reader = NULL;
	}

	return ret;
}

int Mp4TransformContext::copy_drm_or_origin_data(bool *write_down,int64_t *toread) {
	int64_t avail, drm_avail, meta_avail;
	avail = TSIOBufferReaderAvail(this->res_reader);
	if (this->raw_transform) {
		if (avail > 0) {
			TSIOBufferCopy(this->output.buffer, this->res_reader, avail, 0);
			TSIOBufferReaderConsume(this->res_reader, avail);
			this->total += avail;
			*write_down = true;
		}

	} else {
		drm_avail = TSIOBufferReaderAvail(this->mm.drm_reader);
		if (drm_avail >0 ) {
			TSIOBufferReaderConsume(this->res_reader, avail);
			//将meta_buffer 剩余的数据拷贝进去
			meta_avail = TSIOBufferReaderAvail(this->mm.meta_reader);
			TSDebug(PLUGIN_NAME, "copy_drm_or_origin_data meta_avail=%ld", meta_avail);//324264
			TSIOBufferCopy(this->res_buffer, this->mm.meta_reader, meta_avail, 0);
			TSIOBufferReaderConsume(this->mm.meta_reader, meta_avail);
			//整个文件的长度将从meta_buffer 消费结束地方开始
			this->pos = this->mm.tag_pos + this->mm.passed;
			TSDebug(PLUGIN_NAME, "copy_drm_or_origin_data pos=%ld, tag_pos= %ld, passed=%ld", this->pos, this->mm.tag_pos, this->mm.passed);

			// copy the new meta data

			TSIOBufferCopy(this->output.buffer, this->mm.drm_reader,drm_avail, 0);
			TSIOBufferReaderConsume(this->mm.drm_reader, drm_avail);
			this->total += drm_avail;
			*write_down = true;
		}

		// ignore useless part
		this->ignore_useless_part();
		this->copy_video_and_audio_data(write_down,toread);
	}
	return 0;
}

// ignore useless part
int Mp4TransformContext::ignore_useless_part() {
	int64_t avail, need;
	if (this->pos < this->tail) {
		avail = TSIOBufferReaderAvail(this->res_reader);
		need = this->tail - this->pos;
		if (need > avail) {
			need = avail;
		}

		if (need > 0) {
			TSIOBufferReaderConsume(this->res_reader, need);
			this->pos += need;
		}
	}
	return 0;
}

// copy the video & audio data
int Mp4TransformContext::copy_video_and_audio_data(bool *write_down,int64_t *toread) {
	int64_t avail, need, des_avail;
	int des_ret;

	if(this->end_tail) {//有end的流程
		if(this->pos >= this->end_tail) {
			u_char des_videoid[1024];
			uint32_t d_v_length;
			//提前结束
			avail = TSIOBufferReaderAvail(this->res_reader);
			if (avail > 0) {
				TSIOBufferReaderConsume(this->res_reader, avail);
			}
			*toread = 0;
			d_v_length = 0;
			this->mm.get_des_videoid(des_videoid, &d_v_length);
			//将videoid 加到结尾
			TSIOBufferWrite(this->output.buffer, des_videoid, d_v_length);
			this->total +=d_v_length;
			TSDebug(PLUGIN_NAME, "copy_video_and_audio_data d_v_length=%ld totail=%ld", d_v_length,this->total);
			*write_down = true;
			return 0;
		}
		if (this->pos < this->end_tail) {
			avail = TSIOBufferReaderAvail(this->res_reader);
			need = this->end_tail - this->pos;
			if (need > avail) {
				need = avail;
			}
			TSDebug(PLUGIN_NAME, "copy_video_and_audio_data need=%ld totail=%ld", need,this->total);
			if (need > 0) {
				des_ret = 0;
				if (!this->mm.is_des_body) {//是否已经进行过des加密了
					TSIOBufferCopy(this->mm.des_buffer, this->res_reader, need, 0);
					des_ret = this->mm.process_encrypt_mp4_body();
					if (des_ret > 0) {
						des_avail = TSIOBufferReaderAvail(this->mm.out_handle.reader);
						if (des_avail > 0) {
							TSIOBufferCopy(this->output.buffer,this->mm.out_handle.reader, des_avail, 0);
							TSIOBufferReaderConsume(this->mm.out_handle.reader,des_avail);
							*write_down = true;
							need = des_avail;
//							if(des_avail- need > 0)
//								this->total += des_avail- need;
//							TSDebug(PLUGIN_NAME, "copy_video_and_audio_data  totail=%ld",this->total);
						}
					}
				} else {
					TSIOBufferCopy(this->output.buffer, this->res_reader, need, 0);
					*write_down = true;
				}
				TSIOBufferReaderConsume(this->res_reader, need);
				this->pos += need;
				this->total += need;
				TSDebug(PLUGIN_NAME, "copy_video_and_audio_data  totail=%ld",this->total);

			}
		}

	} else {//没有end的流程
		if (this->pos >= this->tail) {
			avail = TSIOBufferReaderAvail(this->res_reader);
			if (avail > 0) {
				//这边开始处理des 加密数据
				des_ret = 0;
				if (!this->mm.is_des_body) {
					TSIOBufferCopy(this->mm.des_buffer, this->res_reader, avail, 0);
					des_ret = this->mm.process_encrypt_mp4_body();
					if (des_ret > 0) {
						des_avail = TSIOBufferReaderAvail(this->mm.out_handle.reader);
						if (des_avail > 0) {
							TSIOBufferCopy(this->output.buffer,this->mm.out_handle.reader, des_avail, 0);
							TSIOBufferReaderConsume(this->mm.out_handle.reader,des_avail);
							*write_down = true;
							avail = des_avail;
//							if(des_avail- avail > 0)
//								this->total += des_avail- avail;
						}
					}
				} else {
					TSIOBufferCopy(this->output.buffer, this->res_reader, avail, 0);
					*write_down = true;
				}
				TSIOBufferReaderConsume(this->res_reader, avail);
				this->pos += avail;
				this->total += avail;
			}
		}
	}
	return 0;
}

