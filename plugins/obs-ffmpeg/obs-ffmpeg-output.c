/******************************************************************************
    Copyright (C) 2013 by Hugh Bailey <obs.jim@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include <obs.h>
#include "obs-ffmpeg-output.h"

/* TODO: remove these later */
#define FILENAME_TODO "D:\\test.mp4"
#define SPS_TODO      44100

static inline enum AVPixelFormat obs_to_ffmpeg_video_format(
		enum video_format format)
{
	switch (format) {
	case VIDEO_FORMAT_NONE: return AV_PIX_FMT_NONE;
	case VIDEO_FORMAT_I420: return AV_PIX_FMT_YUV420P;
	case VIDEO_FORMAT_NV12: return AV_PIX_FMT_NV12;
	case VIDEO_FORMAT_YVYU: return AV_PIX_FMT_NONE;
	case VIDEO_FORMAT_YUY2: return AV_PIX_FMT_YUYV422;
	case VIDEO_FORMAT_UYVY: return AV_PIX_FMT_UYVY422;
	case VIDEO_FORMAT_YUVX: return AV_PIX_FMT_NONE;
	case VIDEO_FORMAT_UYVX: return AV_PIX_FMT_NONE;
	case VIDEO_FORMAT_RGBA: return AV_PIX_FMT_RGBA;
	case VIDEO_FORMAT_BGRA: return AV_PIX_FMT_BGRA;
	case VIDEO_FORMAT_BGRX: return AV_PIX_FMT_BGRA;
	}

	return AV_PIX_FMT_NONE;
}

static bool new_stream(struct ffmpeg_data *data, AVStream **stream,
		AVCodec **codec, enum AVCoecID id)
{
	*codec = avcodec_find_encoder(id);
	if (!*codec) {
		blog(LOG_ERROR, "Couldn't find encoder '%s'",
				avcodec_get_name(id));
		return false;
	}

	*stream = avformat_new_stream(data->output, *codec);
	if (!*stream) {
		blog(LOG_ERROR, "Couldn't create stream for encoder '%s'",
				avcodec_get_name(id));
		return false;
	}

	(*stream)->id = data->output->nb_streams-1;
	return true;
}

static bool open_video_codec(struct ffmpeg_data *data,
		struct obs_video_info *ovi)
{
	AVCodecContext *context = data->video->codec;
	int ret;

	ret = avcodec_open2(context, data->vcodec, NULL);
	if (ret < 0) {
		blog(LOG_ERROR, "Failed to open video codec: %s",
				av_err2str(ret));
		return false;
	}

	data->vframe = av_frame_alloc();
	if (!data->vframe) {
		blog(LOG_ERROR, "Failed to allocate video frame");
		return false;
	}

	data->vframe->format = context->pix_fmt;
	data->vframe->width  = context->width;
	data->vframe->height = context->height;

	ret = avpicture_alloc(&data->dst_picture, context->pix_fmt,
			context->width, context->height);
	if (ret < 0) {
		blog(LOG_ERROR, "Failed to allocate dst_picture: %s",
				av_err2str(ret));
		return false;
	}

	if (context->pix_fmt != AV_PIX_FMT_YUV420P) {
		ret = avpicture_alloc(&data->src_picture, AV_PIX_FMT_YUV420P,
				context->width, context->height);
		if (ret < 0) {
			blog(LOG_ERROR, "Failed to allocate src_picture: %s",
					av_err2str(ret));
			return false;
		}
	}

	*((AVPicture*)data->vframe) = data->dst_picture;
	return true;
}

static bool create_video_stream(struct ffmpeg_data *data)
{
	AVCodecContext *context;
	struct obs_video_info ovi;

	if (!obs_get_video_info(&ovi)) {
		blog(LOG_ERROR, "No active video");
		return false;
	}
	if (!new_stream(data, &data->video, &data->vcodec,
				data->output->oformat->video_codec))
		return false;

	context                = data->video->codec;
	context->codec_id      = data->output->oformat->video_codec;
	context->bit_rate      = 6000000;
	context->width         = ovi.output_width;
	context->height        = ovi.output_height;
	context->time_base.num = ovi.fps_den;
	context->time_base.den = ovi.fps_num;
	context->gop_size      = 12;
	context->pix_fmt       = AV_PIX_FMT_YUV420P;

	if (data->output->oformat->flags & AVFMT_GLOBALHEADER)
		context->flags |= CODEC_FLAG_GLOBAL_HEADER;

	return open_video_codec(data, &ovi);
}

static bool open_audio_codec(struct ffmpeg_data *data,
		struct audio_output_info *aoi)
{
	AVCodecContext *context = data->audio->codec;
	int ret;

	data->aframe = av_frame_alloc();
	if (!data->aframe) {
		blog(LOG_ERROR, "Failed to allocate audio frame");
		return false;
	}

	ret = avcodec_open2(context, data->acodec, NULL);
	if (ret < 0) {
		blog(LOG_ERROR, "Failed to open audio codec: %s",
				av_err2str(ret));
		return false;
	}

	return true;
}

static bool create_audio_stream(struct ffmpeg_data *data)
{
	AVCodecContext *context;
	struct audio_output_info aoi;

	if (!obs_get_audio_info(&aoi)) {
		blog(LOG_ERROR, "No active audio");
		return false;
	}
	if (!new_stream(data, &data->audio, &data->acodec,
				data->output->oformat->audio_codec))
		return false;

	context = data->audio->codec;
	context->bit_rate    = 128000;
	context->channels    = get_audio_channels(aoi.speakers);
	context->sample_rate = aoi.samples_per_sec;

	if (data->output->oformat->flags & AVFMT_GLOBALHEADER)
		context->flags |= CODEC_FLAG_GLOBAL_HEADER;

	return open_audio_codec(data, &aoi);
}

static inline bool init_streams(struct ffmpeg_data *data)
{
	AVOutputFormat *format = data->output->oformat;

	if (format->video_codec != AV_CODEC_ID_NONE) {
		if (!create_video_stream(data))
			return false;
	}

	if (format->audio_codec != AV_CODEC_ID_NONE) {
		if (!create_audio_stream(data))
			return false;
	}

	return true;
}

static inline bool open_output_file(struct ffmpeg_data *data)
{
	AVOutputFormat *format = data->output->oformat;
	int ret;

	if ((format->flags & AVFMT_NOFILE) == 0) {
		ret = avio_open(&data->output->pb, FILENAME_TODO,
				AVIO_FLAG_WRITE);
		if (ret < 0) {
			blog(LOG_ERROR, "Couldn't open file '%s', %s",
					FILENAME_TODO, av_err2str(ret));
			return false;
		}
	}

	ret = avformat_write_header(data->output, NULL);
	if (ret < 0) {
		blog(LOG_ERROR, "Error opening file '%s': %s",
				FILENAME_TODO, av_err2str(ret));
		return false;
	}

	return true;
}

static void close_video(struct ffmpeg_data *data)
{
	avcodec_close(data->video->codec);
	av_free(data->src_picture.data[0]);
	av_free(data->dst_picture.data[0]);
	av_frame_free(&data->vframe);
}

static void close_audio(struct ffmpeg_data *data)
{
	avcodec_close(data->audio->codec);

	av_free(data->samples[0]);
	av_free(data->samples);
	av_frame_free(&data->aframe);
}

static void ffmpeg_data_free(struct ffmpeg_data *data)
{
	if (data->initialized)
		av_write_trailer(data->output);

	if (data->video)
		close_video(data);
	if (data->audio)
		close_audio(data);
	if ((data->output->oformat->flags & AVFMT_NOFILE) == 0)
		avio_close(data->output->pb);

	avformat_free_context(data->output);
}

static bool ffmpeg_data_init(struct ffmpeg_data *data)
{
	memset(data, 0, sizeof(struct ffmpeg_data));

	av_register_all();

	/* TODO: settings */
	avformat_alloc_output_context2(&data->output, NULL, NULL,
			"D:\\test.mp4");
	if (!data->output) {
		blog(LOG_ERROR, "Couldn't create avformat context");
		goto fail;
	}

	if (!init_streams(data))
		goto fail;
	if (!open_output_file(data))
		goto fail;

	data->initialized = true;
	return true;

fail:
	blog(LOG_ERROR, "ffmpeg_data_init failed");
	ffmpeg_data_free(data);
	return false;
}

/* ------------------------------------------------------------------------- */

const char *ffmpeg_output_getname(const char *locale)
{
	/* TODO: translation */
	return "FFmpeg file output";
}

struct ffmpeg_output *ffmpeg_output_create(obs_data_t settings,
		obs_output_t output)
{
	struct ffmpeg_output *data = bmalloc(sizeof(struct ffmpeg_output));
	memset(data, 0, sizeof(struct ffmpeg_output));

	data->output = output;

	return data;
}

void ffmpeg_output_destroy(struct ffmpeg_output *data)
{
	if (data) {
		ffmpeg_data_free(&data->ff_data);
		bfree(data);
	}
}

void ffmpeg_output_update(struct ffmpeg_output *data, obs_data_t settings)
{
}

static void receive_video(void *param, const struct video_frame *frame)
{
}

static void receive_audio(void *param, const struct audio_data *frame)
{
}

bool ffmpeg_output_start(struct ffmpeg_output *data)
{
	video_t video = obs_video();
	audio_t audio = obs_audio();

	if (!video || !audio) {
		blog(LOG_ERROR, "ffmpeg_output_start: audio and video must "
		                "both be active (at least as of this writing)");
		return false;
	}

	if (!ffmpeg_data_init(&data->ff_data))
		return false;

	struct audio_convert_info aci;
	aci.samples_per_sec = SPS_TODO;
	aci.format          = AUDIO_FORMAT_16BIT;
	aci.speakers        = SPEAKERS_STEREO;

	struct video_convert_info vci;
	vci.format          = VIDEO_FORMAT_I420;
	vci.width           = 0;
	vci.height          = 0;
	vci.row_align       = 1;

	video_output_connect(video, &vci, receive_video, data);
	audio_output_connect(audio, &aci, receive_audio, data);
	data->active = true;

	return true;
}

void ffmpeg_output_stop(struct ffmpeg_output *data)
{
	if (data->active) {
		data->active = false;
		video_output_disconnect(obs_video(), receive_video, data);
		audio_output_disconnect(obs_audio(), receive_audio, data);
		ffmpeg_data_free(&data->ff_data);
	}
}

bool ffmpeg_output_active(struct ffmpeg_output *data)
{
	return data->active;
}
