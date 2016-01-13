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

	while (blk != NULL) {
		data = TSIOBufferBlockReadStart(blk, this->dup_reader, &bytes);
		if (bytes > 0) {
			TSIOBufferWrite(this->mm.meta_buffer, data, bytes);
		}

		blk = TSIOBufferBlockNext(blk);
	}

	TSIOBufferReaderConsume(this->dup_reader, avail);
	ret = this->mm.parse_meta(body_complete);
	TSDebug(PLUGIN_NAME, "mp4_parse_meta ret = %d", ret);
	if (ret > 0) { // meta success
		this->tail = this->mm.start_pos; //start position of the new mp4 file
		this->end_tail = this->mm.end_pos;
		this->content_length = this->mm.content_length; //-183; //the size of the new mp4 file
	}

	if (ret != 0) {
		TSIOBufferReaderFree(this->dup_reader);
		this->dup_reader = NULL;
	}

	return ret;
}

int Mp4TransformContext::copy_drm_or_origin_data(bool *write_down,int64_t *toread) {
	int64_t avail;
	avail = TSIOBufferReaderAvail(this->res_reader);
	if (this->raw_transform) {
		if (avail > 0) {
			TSIOBufferCopy(this->output.buffer, this->res_reader, avail, 0);
			TSIOBufferReaderConsume(this->res_reader, avail);
			this->total += avail;
			*write_down = true;
		}

	} else {

		this->copy_drm_data(write_down);
		// ignore useless part
		this->ignore_useless_part();
		// copy the video & audio data
		this->copy_video_and_audio_data(write_down,toread);
	}
	return 0;
}


int Mp4TransformContext::copy_drm_data(bool *write_down) {
	int64_t drm_avail, meta_avail;
	drm_avail = TSIOBufferReaderAvail(this->mm.drm_reader);
	if (drm_avail >0 ) {
		TSIOBufferReaderConsume(this->res_reader, TSIOBufferReaderAvail(this->res_reader));

		meta_avail = TSIOBufferReaderAvail(this->mm.meta_reader);
		TSDebug(PLUGIN_NAME, "copy_drm_or_origin_data meta_avail=%ld", meta_avail);//324264
		TSIOBufferCopy(this->res_buffer, this->mm.meta_reader, meta_avail, 0);
		TSIOBufferReaderConsume(this->mm.meta_reader, meta_avail);

		this->pos = this->mm.tag_pos + this->mm.passed;
//		TSDebug(PLUGIN_NAME, "copy_drm_or_origin_data pos=%ld, tag_pos= %ld, passed=%ld", this->pos, this->mm.tag_pos, this->mm.passed);

		TSIOBufferCopy(this->output.buffer, this->mm.drm_reader,drm_avail, 0);
		TSIOBufferReaderConsume(this->mm.drm_reader, drm_avail);
		this->total += drm_avail;
		*write_down = true;
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

int Mp4TransformContext::copy_video_and_audio_data(bool *write_down,int64_t *toread) {
	if(this->end_tail) {//if range have end
		if(this->pos >= this->end_tail) {
			this->discard_after_end_data();
			*toread = 0;
			*write_down = true;
			return 0;
		}
		if (this->pos < this->end_tail) {
			this->copy_valuable_data(write_down);
		}

	} else {
		if (this->pos >= this->tail) {
			this->copy_valuable_data(write_down);
		}
	}
	return 0;
}

int Mp4TransformContext::discard_after_end_data() {
	int64_t avail;
	u_char des_videoid[1024];
	uint32_t d_v_length;
	avail = TSIOBufferReaderAvail(this->res_reader);
	if (avail > 0) {
		TSIOBufferReaderConsume(this->res_reader, avail);
	}
	d_v_length = 0;
	this->mm.get_des_videoid(des_videoid, &d_v_length);
	//last add des(videoid)
	TSIOBufferWrite(this->output.buffer, des_videoid, d_v_length);
	this->total +=d_v_length;
	TSDebug(PLUGIN_NAME, "copy_video_and_audio_data d_v_length=%d totail=%ld", d_v_length,this->total);
	return 0;
}

int Mp4TransformContext::copy_valuable_data(bool *write_down) {
	int64_t avail, need, des_avail;
	int des_ret;
	avail = TSIOBufferReaderAvail(this->res_reader);
	if(this->end_tail) {
		need = this->end_tail - this->pos;
		if (need <= avail) {
			avail = need;
		}
	}
	if (avail > 0) {
		des_ret = 0;
		if (!this->mm.is_des_body) {
			TSIOBufferCopy(this->mm.des_buffer, this->res_reader, avail, 0);
			des_ret = this->mm.process_encrypt_mp4_body();
		} else {
			TSIOBufferCopy(this->output.buffer, this->res_reader, avail, 0);
			*write_down = true;
		}
		TSIOBufferReaderConsume(this->res_reader, avail);
		this->pos += avail;
		this->total += avail;
		if (des_ret > 0) {
			des_avail = TSIOBufferReaderAvail(this->mm.out_handle.reader);
			if (des_avail > 0) {
				TSIOBufferCopy(this->output.buffer,this->mm.out_handle.reader, des_avail, 0);
				TSIOBufferReaderConsume(this->mm.out_handle.reader,des_avail);
				*write_down = true;
				if(des_avail- avail > 0)
					this->total += des_avail- avail;
			}
		}

	}
	return 0;
}

